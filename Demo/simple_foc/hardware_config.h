/**
 * @file
 * @brief ESP32-S3控制板的引脚映射、功率板选择和FOC参数。
 * @details 按实际功率板设置POWER_BOARD后重新编译。板型决定分流电阻、母线保护范围和CAL引脚用法。
 */
#pragma once

namespace Hardware
{
enum class PowerBoard
{
  Drv8323Hr,
  Drv8353Hr
};

constexpr PowerBoard POWER_BOARD = PowerBoard::Drv8323Hr; // 使用DRV8323HR时改为PowerBoard::Drv8323Hr
static_assert(POWER_BOARD == PowerBoard::Drv8323Hr || POWER_BOARD == PowerBoard::Drv8353Hr,
              "Unsupported power board");
constexpr bool HAS_CAL_PIN = POWER_BOARD == PowerBoard::Drv8323Hr; // 仅DRV8323HR在P2-24引出硬件校准信号
constexpr float SHUNT_OHM = POWER_BOARD == PowerBoard::Drv8323Hr ? 0.004f : 0.002f; // 各相分流电阻R36/R37/R38，单位Ω
constexpr float VBUS_MIN = 12.0f; // 两块功率板的软件母线下限，单位V
constexpr float VBUS_MAX = POWER_BOARD == PowerBoard::Drv8323Hr ? 40.0f : 60.0f; // 超过此母线电压时关断驱动，单位V

constexpr int PWM_AH = 38, PWM_AL = 39;                       // P1/P2-25、26：INHA、INLA
constexpr int PWM_BH = 40, PWM_BL = 41;                       // P1/P2-27、28：INHB、INLB
constexpr int PWM_CH = 42, PWM_CL = 47;                       // P1/P2-29、30：INHC、INLC
constexpr int ENABLE = 7;                                     // P1/P2-23：DRV_EN，高有效
constexpr int CAL = 8;                                        // 仅DRV8323HR使用GPIO8，正常工作保持低
constexpr int FAULT = 9;                                      // P1/P2-14：nFAULT，板载上拉，低有效
constexpr int CURRENT_A = 4, CURRENT_B = 5, CURRENT_C = 6;    // ADC1，IOUTA/B/C
constexpr int VBUS = 17;                                      // P1/P2-11：母线分压，ADC2
constexpr int CS = 10, SCK = 11, MISO = 12, MOSI = 13;        // MT6835转子侧SPI
constexpr int UART_RX = 44, UART_TX = 43;                     // 板载CH343P连接UART0
constexpr float CSA_GAIN = 5.0f;                              // 相电流放大器增益，GAIN经R47接地，单位V/V
constexpr float VBUS_RATIO = 4700.0f / (124000.0f + 4700.0f); // ADC引脚电压与母线电压之比，R4/R6分压
constexpr long PWM_HZ = 10000;         // 六路PWM载波频率，单位Hz；电流环随loop运行
constexpr float DEAD_ZONE = 0.01f;     // 互补PWM死区参数，当前ESP32 MCPWM后端在10kHz下约为500ns
constexpr float CURRENT_LIMIT = 2.0f;  // Q轴目标电流限制为±2A；功率板的硬件过流保护独立工作
constexpr float VOLTAGE_LIMIT = 6.0f;  // D/Q电流PI各轴输出电压上限，单位V，与母线保护上限分开设置
constexpr float ALIGN_VOLTAGE = 0.5f;  // 启动时对齐编码器与相电流采样所用的激励电压，单位V
}

namespace MotorConfig
{
constexpr int POLE_PAIRS = 14;                       // 电机极对数，决定每圈机械转动对应的电角度周期数
constexpr float GEAR_RATIO = 8.0f;                   // 转子转速与输出轴转速之比
constexpr float PHASE_RESISTANCE_OHM = 0.22f;        // 电机单相等效电阻，单位Ω
constexpr float PHASE_INDUCTANCE_MH = 0.135f;       // 电机单相等效电感，单位mH
static_assert(PHASE_RESISTANCE_OHM > 0.0f && PHASE_INDUCTANCE_MH > 0.0f, "Motor phase R and L must be positive");
}

namespace CurrentLoopConfig
{
constexpr float BANDWIDTH_HZ = 200.0f;               // 电流PI的目标整定带宽，单位Hz
static_assert(BANDWIDTH_HZ > 0.0f, "Current loop bandwidth must be positive");
constexpr float BANDWIDTH_RAD_S = 6.28318530718f * BANDWIDTH_HZ; // 将带宽换算为rad/s，用于计算PI增益
constexpr float CURRENT_P = (MotorConfig::PHASE_INDUCTANCE_MH * 1e-3f) * BANDWIDTH_RAD_S; // 电感换算为H后计算比例增益，单位V/A
constexpr float CURRENT_I = MotorConfig::PHASE_RESISTANCE_OHM * BANDWIDTH_RAD_S; // D/Q轴积分增益，单位V/(A·s)
constexpr float CURRENT_FILTER_TF = 0.00031831f;     // D/Q电流反馈一阶低通时间常数，单位s，截止频率约500Hz
}
