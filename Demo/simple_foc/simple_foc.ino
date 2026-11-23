/**
 * @file
 * @brief 基于SimpleFOC官方current_control例程的ESP32-S3电流转矩控制，支持DRV8323HR与DRV8353HR功率板。
 * @details 在hardware_config.h选择实际功率板。启动会执行编码器和相电流对齐，电机可能短暂转动。
 * @note 串口115200波特率，发送T<安培值>并以换行结尾。T0将目标电流置零，驱动仍保持使能。
 * @note SimpleFOC的ESP32低侧采样后端按PWM周期轮询相电流，各相读数可能来自不同周期。
 */
#include <SimpleFOC.h>
#include <SimpleFOCDrivers.h>
#include <encoders/mt6835/MagneticSensorMT6835.h>
#include "hardware_config.h"

BLDCMotor motor(MotorConfig::POLE_PAIRS);
BLDCDriver6PWM driver(Hardware::PWM_AH,
                      Hardware::PWM_AL,
                      Hardware::PWM_BH,
                      Hardware::PWM_BL,
                      Hardware::PWM_CH,
                      Hardware::PWM_CL,
                      Hardware::ENABLE);
LowsideCurrentSense currentSense(
    Hardware::SHUNT_OHM, Hardware::CSA_GAIN, Hardware::CURRENT_A, Hardware::CURRENT_B, Hardware::CURRENT_C);
MagneticSensorMT6835 sensor(Hardware::CS); // MT6835安装在转子侧，为FOC提供转子位置
float targetCurrent = 0.0f; // Q轴目标电流，单位A
Commander command(Serial0);
bool ready = false;        // 通过初始化、对齐及板级检查后才接受电流目标
bool driverLinked = false; // 已关联驱动时，停机还需同步禁用SimpleFOC电机对象

/** @brief 读取ADC2的毫伏值，再按板上分压比还原母线电压，单位V。 */
float readBusVoltage()
{
  return analogReadMilliVolts(Hardware::VBUS) * 0.001f / Hardware::VBUS_RATIO;
}

/** @brief 关闭驱动并停止接收电流目标，复位后重新初始化。 */
void stopMotor(const char *reason)
{
  digitalWrite(Hardware::ENABLE, LOW);
  if (driverLinked)
  {
    motor.disable();
  }
  ready = false;
  Serial0.printf("[FAIL] %s; reset required.\n", reason);
}

/** @brief 解析T命令中的Q轴目标电流，单位A。正负值对应相反的转矩方向。 */
void doTarget(char *cmd)
{
  if (!ready)
  {
    return;
  }
  float target = targetCurrent;
  command.scalar(&target, cmd);
  if (!isfinite(target))
  {
    Serial0.println("Invalid target");
    return;
  }
  targetCurrent = constrain(target, -Hardware::CURRENT_LIMIT, Hardware::CURRENT_LIMIT);
}

/** @brief 每轮检查nFAULT，每10ms检查母线并更新PWM占空比换算基准。 */
bool checkBoard()
{
  if (digitalRead(Hardware::FAULT) == LOW)
  {
    stopMotor("DRIVER_FAULT");
    return false;
  }
  static uint32_t lastBusMs = 0;
  const uint32_t now = millis();
  if (now - lastBusMs >= 10)
  {
    lastBusMs = now;
    const float bus = readBusVoltage();
    if (!isfinite(bus) || bus < Hardware::VBUS_MIN || bus > Hardware::VBUS_MAX)
    {
      stopMotor("BUS_OUT_OF_RANGE");
      return false;
    }
    driver.voltage_power_supply = bus;
    driver.voltage_limit = bus;
  }
  return true;
}

void setup()
{
  pinMode(Hardware::ENABLE, OUTPUT);
  digitalWrite(Hardware::ENABLE, LOW);
  if (Hardware::HAS_CAL_PIN) // CAL保持低电平，软件电流零点校准由SimpleFOC执行
  {
    pinMode(Hardware::CAL, OUTPUT);
    digitalWrite(Hardware::CAL, LOW);
  }
  pinMode(Hardware::FAULT, INPUT); // 原理图已有3.3V上拉
  Serial0.begin(115200, SERIAL_8N1, Hardware::UART_RX, Hardware::UART_TX);
  SimpleFOCDebug::enable(&Serial0);
  analogReadResolution(12);
  analogSetPinAttenuation(Hardware::VBUS, ADC_11db);
  delay(100); // EN保持低电平，等待控制板供电稳定
  const float bus = readBusVoltage();
  if (!isfinite(bus) || bus < Hardware::VBUS_MIN || bus > Hardware::VBUS_MAX)
  {
    stopMotor("BUS_OUT_OF_RANGE");
    return;
  }

  SPI.begin(Hardware::SCK, Hardware::MISO, Hardware::MOSI, Hardware::CS);
  sensor.init(&SPI);
  motor.linkSensor(&sensor);
  driver.voltage_power_supply = bus;
  driver.voltage_limit = bus; // 半桥按实际母线换算占空比，电流PI限幅另由motor.voltage_limit设置
  driver.pwm_frequency = Hardware::PWM_HZ;
  driver.dead_zone = Hardware::DEAD_ZONE;
  if (!driver.init())
  {
    stopMotor("DRIVER_INIT");
    return;
  }
  motor.linkDriver(&driver);
  driverLinked = true;
  currentSense.linkDriver(&driver);
  motor.controller = MotionControlType::torque;
  motor.torque_controller = TorqueControlType::foc_current;
  motor.foc_modulation = FOCModulationType::SpaceVectorPWM;
  motor.voltage_limit = min(bus, Hardware::VOLTAGE_LIMIT);
  motor.current_limit = Hardware::CURRENT_LIMIT;
  motor.voltage_sensor_align = Hardware::ALIGN_VOLTAGE;
  motor.PID_current_q.P = motor.PID_current_d.P = CurrentLoopConfig::CURRENT_P;
  motor.PID_current_q.I = motor.PID_current_d.I = CurrentLoopConfig::CURRENT_I;
  motor.PID_current_q.D = motor.PID_current_d.D = 0.0f;
  motor.LPF_current_q.Tf = motor.LPF_current_d.Tf = CurrentLoopConfig::CURRENT_FILTER_TF;
  motor.useMonitoring(Serial0);
  if (!motor.init())
  {
    stopMotor("MOTOR_INIT");
    return;
  }
  delay(10); // motor.init拉高EN，等待驱动退出唤醒阶段后再检查nFAULT
  if (digitalRead(Hardware::FAULT) == LOW)
  {
    stopMotor("DRIVER_FAULT");
    return;
  }
  if (!currentSense.init())
  {
    stopMotor("CURRENT_SENSE_INIT");
    return;
  }
  motor.linkCurrentSense(&currentSense);
  if (!motor.initFOC() || !motor.pp_check_result)
  {
    stopMotor("FOC_ALIGNMENT");
    return;
  }
  // 对齐会向电机施加电压，结束后复查nFAULT和母线再开放T命令。
  if (digitalRead(Hardware::FAULT) == LOW)
  {
    stopMotor("DRIVER_FAULT");
    return;
  }
  const float alignedBus = readBusVoltage();
  if (!isfinite(alignedBus) || alignedBus < Hardware::VBUS_MIN || alignedBus > Hardware::VBUS_MAX)
  {
    stopMotor("BUS_OUT_OF_RANGE");
    return;
  }
  driver.voltage_power_supply = alignedBus;
  driver.voltage_limit = alignedBus;
  command.add('T', doTarget, "target current");
  ready = true;
  Serial0.println("Motor ready. Set target current with T<ampere>.");
}

void loop()
{
  if (ready && checkBoard())
  {
    motor.loopFOC();
    motor.move(targetCurrent);
  }
  command.run();
}
