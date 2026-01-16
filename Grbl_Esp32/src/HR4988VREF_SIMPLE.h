/*
  HR4988VREF.h - HR4988 VREF动态电流控制（统一版）
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
  
  统一的HR4988 VREF控制实现，整合了完整功能和简化版本的优势
  特性：
  - 基于ESP32 DAC的VREF电压控制
  - 智能电流管理和温度保护
  - 负载自适应控制
  - 节能模式
  - 与换纸系统深度集成
*/

#pragma once

#ifdef AUTO_PAPER_CHANGE_ENABLE
#ifdef HR4988_VREF_PIN

#include "Grbl.h"
#include "PaperChangeConfig.h"
#include "PaperChangeTypes.h"

// ================================================================================
// HR4988电流计算参数
// ================================================================================

#define HR4988_SENSE_RESISTOR        0.05f    // 检测电阻值 (Ω)
#define HR4988_CURRENT_FACTOR       8.0f     // 电流公式分母系数

// VREF电压限制 (使用现有DAC功能)
#define VREF_MIN_VOLTAGE            0.1f      // 最小VREF电压 (V)
#define VREF_MAX_VOLTAGE            3.3f      // 最大VREF电压 (V)

// 电流限制 (根据电机规格调整)
#define CURRENT_MIN_AMPS           0.1f      // 最小电流 (A)
#define CURRENT_NORMAL_AMPS         1.0f      // 正常工作电流 (A)
#define CURRENT_HIGH_AMPS          1.5f      // 高扭矩电流 (A)
#define CURRENT_MAX_AMPS          2.0f      // 最大电流 (A)

// 节能参数
#define ENERGY_SAVE_TIMEOUT_MS    300000     // 5分钟无操作进入节能
#define STANDBY_CURRENT_RATIO    0.3f       // 待机电流比例

// 温度保护参数（从完整版迁移）
#define TEMP_LOW_THRESHOLD         30.0f      // 低温阈值 (°C)
#define TEMP_HIGH_THRESHOLD        60.0f      // 高温阈值 (°C)
#define TEMP_CRITICAL_THRESHOLD   75.0f      // 危险温度阈值 (°C)

// 自适应控制参数
#define ADAPTIVE_MIN_INTERVAL_US  300        // 最小步进间隔 (μs)
#define ADAPTIVE_MAX_INTERVAL_US  5000       // 最大步进间隔 (μs)

// 延迟参数
#define DAC_STABILIZATION_DELAY_MS 100        // DAC稳定延时 (ms)
#define PAPER_MOTOR_STEP_DELAY_MS  100        // 换纸电机步进延时 (ms)
#define HC595_UPDATE_DELAY_US      10         // HC595更新延时 (μs)

// ================================================================================
// 电机工作模式枚举
// ================================================================================

typedef enum {
    MOTOR_MODE_STANDBY = 0,        // 待机模式
    MOTOR_MODE_PRECISION = 1,       // 精密模式
    MOTOR_MODE_NORMAL = 2,          // 正常模式
    MOTOR_MODE_HIGH_TORQUE = 3,    // 高扭矩模式
    MOTOR_MODE_MAXIMUM = 4,         // 最大模式
    MOTOR_MODE_ADAPTIVE = 5,       // 自适应模式
    MOTOR_MODE_COUNT               // 模式数量
} motor_current_mode_t;

// ================================================================================
// VREF控制结构体（整合版本）
// ================================================================================

typedef struct {
    // 基本状态
    bool is_initialized;
    motor_current_mode_t mode;
    
    // 电流控制
    float current_amps;
    float target_voltage;
    uint8_t dac_value;
    
    // 高级功能开关
    bool adaptive_enabled;
    bool thermal_protection_enabled;
    
    // 状态监控
    uint32_t last_activity_time;
    uint32_t last_update_time;
    float motor_temperature;
    
} hr4988_vref_ctrl_t;

// ================================================================================
// 完整的VREF控制接口（整合版本）
// ================================================================================

/**
 * @brief 初始化HR4988 VREF控制（简化版）
 * @return true=成功, false=失败
 */
bool hr4988_simple_init();

/**
 * @brief 设置电机工作模式
 * @param mode 工作模式
 * @return true=成功, false=失败
 */
bool hr4988_simple_set_mode(motor_current_mode_t mode);

/**
 * @brief 设置电机电流（直接值）
 * @param current 电流 (A)
 * @return true=成功, false=失败
 */
bool hr4988_simple_set_current(float current);

/**
 * @brief 获取模式名称
 * @param mode 工作模式
 * @return 模式名称字符串
 */
const char* hr4988_simple_get_mode_name(motor_current_mode_t mode);

/**
 * @brief 为换纸阶段设置合适电流
 * @param phase 换纸状态
 */
void hr4988_simple_set_current_for_phase(paper_change_state_t phase);

/**
 * @brief 获取当前设置的电流
 * @return 当前电流 (A)
 */
float hr4988_simple_get_current();

/**
 * @brief 打印VREF状态
 */
void hr4988_simple_print_status();

// ================================================================================
// 高级控制功能（从完整版迁移）
// ================================================================================

/**
 * @brief 启用/禁用自适应控制
 * @param enabled 是否启用
 */
void hr4988_simple_set_adaptive_mode(bool enabled);

/**
 * @brief 启用/禁用温度保护
 * @param enabled 是否启用
 */
void hr4988_simple_set_thermal_protection(bool enabled);

/**
 * @brief 自适应控制更新（根据步进间隔）
 * @param step_interval 步进间隔 (μs)
 */
void hr4988_simple_adaptive_control(uint32_t step_interval);

/**
 * @brief 温度保护控制
 * @param temperature 电机温度 (°C)
 */
void hr4988_simple_thermal_protection_control(float temperature);

/**
 * @brief 节能控制检查
 */
void hr4988_simple_energy_saving_control();

/**
 * @brief 更新活动时间
 */
void hr4988_simple_update_activity();

/**
 * @brief 主更新函数（在主循环中调用）
 */
void hr4988_simple_update();

/**
 * @brief 电流电压转换函数
 * @param current 电流 (A)
 * @return 对应的VREF电压 (V)
 */
float hr4988_simple_current_to_voltage(float current);

/**
 * @brief 电压电流转换函数
 * @param voltage VREF电压 (V)
 * @return 对应的电流 (A)
 */
float hr4988_simple_voltage_to_current(float voltage);

/**
 * @brief 获取控制结构体指针（调试用）
 * @return 控制结构体指针
 */
hr4988_vref_ctrl_t* hr4988_simple_get_control();

/**
 * @brief 自检函数
 * @return true=通过, false=失败
 */
bool hr4988_simple_self_test();

#endif // HR4988_VREF_PIN
#endif // AUTO_PAPER_CHANGE_ENABLE