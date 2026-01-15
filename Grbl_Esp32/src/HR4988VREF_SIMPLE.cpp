/*
  HR4988VREF.cpp - HR4988 VREF动态电流控制实现（统一版）
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
  
  统一的HR4988 VREF控制实现，整合了完整功能和简化版本的优势
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE
#ifdef HR4988_VREF_PIN

#include "HR4988VREF_SIMPLE.h"
#include "PaperChangeUtils.h"

// ================================================================================
// 辅助宏定义（如果未定义）
// ================================================================================

#ifndef constrain_f
#define constrain_f(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

#ifndef map_f
#define map_f(x, in_min, in_max, out_min, out_max) \
    ((x) - (in_min)) * ((out_max) - (out_min)) / ((in_max) - (in_min)) + (out_min)
#endif

// ================================================================================
// 全局VREF控制变量（统一结构体）
// ================================================================================

static hr4988_vref_ctrl_t vref_ctrl = {0};

// ================================================================================
// 内部辅助函数（统一版本）
// ================================================================================

/**
 * @brief 更新DAC输出
 * @param voltage 目标电压
 * @return true=成功, false=失败
 */
static bool update_dac_output(float voltage) {
    // 电压范围检查
    if (voltage < VREF_MIN_VOLTAGE || voltage > VREF_MAX_VOLTAGE) {
        LOG_ERROR_F("VREF voltage out of range: %.3fV", voltage);
        return false;
    }
    
    // 将0.0-3.3V电压映射到8位DAC值(0-255)
    uint8_t dac_value = (uint8_t)(voltage / VREF_MAX_VOLTAGE * 255.0f);
    
    // 使用ESP32内置的dacWrite函数
    dacWrite(HR4988_VREF_PIN, dac_value);
    
    // 更新状态
    vref_ctrl.target_voltage = voltage;
    vref_ctrl.current_amps = hr4988_simple_voltage_to_current(voltage);
    vref_ctrl.dac_value = dac_value;
    vref_ctrl.last_update_time = millis();
    
    return true;
}

/**
 * @brief 计算自适应电流
 * @param step_interval 步进间隔
 * @return 推荐电流
 */
static float calculate_adaptive_current(uint32_t step_interval) {
    if (step_interval < 300) {
        // 高速运行 (>3333 Hz)
        return CURRENT_HIGH_AMPS;
    } else if (step_interval < 800) {
        // 中速运行 (800-3333 Hz)
        return CURRENT_NORMAL_AMPS;
    } else if (step_interval < 2000) {
        // 低速运行 (500-800 Hz)
        return CURRENT_NORMAL_AMPS * 0.8f;
    } else {
        // 超低速运行 (<500 Hz)
        return CURRENT_NORMAL_AMPS * 0.5f;
    }
}

/**
 * @brief 计算温度补偿电流
 * @param base_current 基础电流
 * @param temperature 温度
 * @return 补偿后电流
 */
static float calculate_thermal_compensation(float base_current, float temperature) {
    if (temperature > TEMP_HIGH_THRESHOLD) {
        // 温度过高，降低电流
        float compensation = map_f(temperature, TEMP_HIGH_THRESHOLD, TEMP_CRITICAL_THRESHOLD, 1.0f, 0.5f);
        compensation = constrain_f(compensation, 0.5f, 1.0f);
        return base_current * compensation;
    } else if (temperature < TEMP_LOW_THRESHOLD) {
        // 温度过低，略微提高电流补偿粘性阻力
        float compensation = map_f(temperature, 10.0f, TEMP_LOW_THRESHOLD, 1.1f, 1.0f);
        compensation = constrain_f(compensation, 1.0f, 1.1f);
        return base_current * compensation;
    }
    return base_current;
}

// ================================================================================
// 核心VREF控制函数实现（统一版本）
// ================================================================================

bool hr4988_simple_init() {
    // 检查GPIO25是否可用于DAC
    if (HR4988_VREF_PIN != GPIO_NUM_25) {
        LOG_ERROR("HR4988 VREF must be on GPIO25 for DAC functionality");
        return false;
    }
    
    // 初始化控制结构
    vref_ctrl.target_voltage = 0.4f;              // 默认1.0A电流
    vref_ctrl.current_amps = 0.0f;
    vref_ctrl.mode = MOTOR_MODE_NORMAL;
    vref_ctrl.adaptive_enabled = true;               // 默认启用自适应
    vref_ctrl.thermal_protection_enabled = true;      // 默认启用温度保护
    vref_ctrl.last_activity_time = millis();
    vref_ctrl.motor_temperature = 25.0f;           // 假设室温
    vref_ctrl.last_update_time = millis();
    vref_ctrl.dac_value = 0;
    vref_ctrl.is_initialized = true;
    
    // 设置默认电流
    bool set_result = hr4988_simple_set_current(CURRENT_NORMAL_AMPS);
    
    LOG_MSG("HR4988 VREF (Unified) initialized: GPIO%d", HR4988_VREF_PIN);
    LOG_MSG_F("Default current: %.2fA (VREF=%.3fV)", 
              CURRENT_NORMAL_AMPS, hr4988_simple_current_to_voltage(CURRENT_NORMAL_AMPS));
    
    return set_result;
}

bool hr4988_simple_set_mode(motor_current_mode_t mode) {
    if (!vref_ctrl.is_initialized) {
        LOG_ERROR("VREF control not initialized");
        return false;
    }
    
    if (mode >= MOTOR_MODE_COUNT) {
        LOG_ERROR_F("Invalid motor mode: %d", mode);
        return false;
    }
    
    float target_current = 0.0f;
    
    switch (mode) {
        case MOTOR_MODE_STANDBY:
            target_current = CURRENT_NORMAL_AMPS * STANDBY_CURRENT_RATIO;
            break;
            
        case MOTOR_MODE_PRECISION:
            target_current = CURRENT_MIN_AMPS;
            break;
            
        case MOTOR_MODE_NORMAL:
            target_current = CURRENT_NORMAL_AMPS;
            break;
            
        case MOTOR_MODE_HIGH_TORQUE:
            target_current = CURRENT_HIGH_AMPS;
            break;
            
        case MOTOR_MODE_MAXIMUM:
            target_current = CURRENT_MAX_AMPS;
            break;
            
        case MOTOR_MODE_ADAPTIVE:
            // 自适应模式在后续update中处理
            vref_ctrl.mode = mode;
            LOG_MSG("HR4988 adaptive mode enabled");
            return true;
            
        default:
            LOG_ERROR_F("Unsupported motor mode: %d", mode);
            return false;
    }
    
    vref_ctrl.mode = mode;
    bool result = hr4988_simple_set_current(target_current);
    
    if (result) {
        LOG_MSG_F("HR4988 mode set to %s (%.2fA, %.3fV)", 
                 hr4988_simple_get_mode_name(mode), target_current, vref_ctrl.target_voltage);
    }
    
    return result;
}

bool hr4988_simple_set_current(float current) {
    if (!vref_ctrl.is_initialized) {
        LOG_ERROR("VREF control not initialized");
        return false;
    }
    
    // 电流范围检查
    if (current < CURRENT_MIN_AMPS || current > CURRENT_MAX_AMPS) {
        LOG_ERROR_F("Current out of range: %.3fA (expected %.1f-%.1fA)", 
                   current, CURRENT_MIN_AMPS, CURRENT_MAX_AMPS);
        return false;
    }
    
    // 计算对应VREF电压
    float voltage = hr4988_simple_current_to_voltage(current);
    
    // 应用温度补偿
    if (vref_ctrl.thermal_protection_enabled) {
        voltage = hr4988_simple_voltage_to_current(voltage);
        voltage = calculate_thermal_compensation(voltage, vref_ctrl.motor_temperature);
        voltage = hr4988_simple_current_to_voltage(voltage);
    }
    
    return update_dac_output(voltage);
}

const char* hr4988_simple_get_mode_name(motor_current_mode_t mode) {
    switch (mode) {
        case MOTOR_MODE_STANDBY:      return "STANDBY";
        case MOTOR_MODE_PRECISION:     return "PRECISION";
        case MOTOR_MODE_NORMAL:        return "NORMAL";
        case MOTOR_MODE_HIGH_TORQUE:   return "HIGH_TORQUE";
        case MOTOR_MODE_MAXIMUM:        return "MAXIMUM";
        case MOTOR_MODE_ADAPTIVE:      return "ADAPTIVE";
        default:                      return "UNKNOWN";
    }
}

void hr4988_simple_set_current_for_phase(paper_change_state_t phase) {
    motor_current_mode_t target_mode;
    
    switch (phase) {
        case PAPER_IDLE:
        case PAPER_COMPLETE:
        case PAPER_ERROR:
            target_mode = MOTOR_MODE_STANDBY;
            break;
            
        case PAPER_PRE_CHECK:
        case PAPER_UNCLAMP_FEED:
        case PAPER_CLAMP_FEED:
        case PAPER_REPOSITION:
            target_mode = MOTOR_MODE_PRECISION;
            break;
            
        case PAPER_EJECTING:
            target_mode = MOTOR_MODE_HIGH_TORQUE;
            break;
            
        case PAPER_FEEDING:
        case PAPER_FULL_FEED:
            target_mode = MOTOR_MODE_NORMAL;
            break;
            
        default:
            target_mode = MOTOR_MODE_NORMAL;
            break;
    }
    
    hr4988_simple_set_mode(target_mode);
}

float hr4988_simple_get_current() {
    if (!vref_ctrl.is_initialized) {
        return 0.0f;
    }
    
    return vref_ctrl.current_amps;
}

// ================================================================================
// 高级控制功能实现（从完整版迁移）
// ================================================================================

void hr4988_simple_set_adaptive_mode(bool enabled) {
    vref_ctrl.adaptive_enabled = enabled;
    LOG_MSG_F("HR4988 adaptive mode %s", enabled ? "enabled" : "disabled");
}

void hr4988_simple_set_thermal_protection(bool enabled) {
    vref_ctrl.thermal_protection_enabled = enabled;
    LOG_MSG_F("HR4988 thermal protection %s", enabled ? "enabled" : "disabled");
}

void hr4988_simple_adaptive_control(uint32_t step_interval) {
    if (!vref_ctrl.is_initialized || !vref_ctrl.adaptive_enabled) {
        return;
    }
    
    if (vref_ctrl.mode != MOTOR_MODE_ADAPTIVE) {
        return; // 非自适应模式不需要处理
    }
    
    float adaptive_current = calculate_adaptive_current(step_interval);
    
    // 应用温度补偿
    if (vref_ctrl.thermal_protection_enabled) {
        adaptive_current = calculate_thermal_compensation(adaptive_current, vref_ctrl.motor_temperature);
    }
    
    // 限制电流范围
    adaptive_current = constrain_f(adaptive_current, CURRENT_MIN_AMPS, CURRENT_HIGH_AMPS);
    
    hr4988_simple_set_current(adaptive_current);
    
    LOG_DEBUG("Adaptive current: %.2fA (interval=%dμs)", adaptive_current, step_interval);
}

void hr4988_simple_thermal_protection_control(float temperature) {
    if (!vref_ctrl.is_initialized || !vref_ctrl.thermal_protection_enabled) {
        return;
    }
    
    vref_ctrl.motor_temperature = temperature;
    
    if (temperature >= TEMP_CRITICAL_THRESHOLD) {
        // 危险温度，立即降低到最小电流
        LOG_ERROR_F("Motor critical temperature: %.1f°C, reducing current to minimum", temperature);
        hr4988_simple_set_current(CURRENT_MIN_AMPS);
        
    } else if (temperature >= TEMP_HIGH_THRESHOLD) {
        // 高温警告，逐步降低电流
        float current = hr4988_simple_get_current();
        float reduced_current = current * 0.8f; // 降低20%
        
        if (reduced_current < CURRENT_MIN_AMPS) {
            reduced_current = CURRENT_MIN_AMPS;
        }
        
        hr4988_simple_set_current(reduced_current);
        LOG_MSG_F("Motor high temperature: %.1f°C, current reduced to %.2fA", 
                 temperature, reduced_current);
    }
}

void hr4988_simple_energy_saving_control() {
    if (!vref_ctrl.is_initialized) {
        return;
    }
    
    uint32_t current_time = millis();
    
    if (current_time - vref_ctrl.last_activity_time > ENERGY_SAVE_TIMEOUT_MS) {
        // 超过5分钟无活动，进入节能模式
        if (vref_ctrl.mode != MOTOR_MODE_STANDBY) {
            LOG_MSG("HR4988 entering energy saving mode");
            hr4988_simple_set_mode(MOTOR_MODE_STANDBY);
        }
    }
}

void hr4988_simple_update_activity() {
    vref_ctrl.last_activity_time = millis();
}

void hr4988_simple_update() {
    if (!vref_ctrl.is_initialized) {
        return;
    }
    
    // 定期检查节能模式
    hr4988_simple_energy_saving_control();
}

// ================================================================================
// 公共工具函数
// ================================================================================

float hr4988_simple_current_to_voltage(float current) {
    return current * HR4988_CURRENT_FACTOR * HR4988_SENSE_RESISTOR;
}

float hr4988_simple_voltage_to_current(float voltage) {
    return voltage / (HR4988_CURRENT_FACTOR * HR4988_SENSE_RESISTOR);
}

hr4988_vref_ctrl_t* hr4988_simple_get_control() {
    return &vref_ctrl;
}

bool hr4988_simple_self_test() {
    LOG_MSG("=== HR4988 VREF Self Test ===");
    
    if (!hr4988_simple_init()) {
        LOG_ERROR("VREF initialization failed");
        return false;
    }
    
    // 测试不同电流设置
    float test_currents[] = {0.5f, 1.0f, 1.5f};
    int test_count = sizeof(test_currents) / sizeof(test_currents[0]);
    
    for (int i = 0; i < test_count; i++) {
        if (hr4988_simple_set_current(test_currents[i])) {
            LOG_MSG_F("Test %d: %.2fA - OK", i + 1, test_currents[i]);
            delay(100); // 等待DAC稳定
        } else {
            LOG_ERROR_F("Test %d: %.2fA - FAILED", i + 1, test_currents[i]);
            return false;
        }
    }
    
    // 恢复到默认电流
    bool restore_result = hr4988_simple_set_current(CURRENT_NORMAL_AMPS);
    
    if (restore_result) {
        LOG_MSG("HR4988 VREF self test PASSED");
        return true;
    } else {
        LOG_ERROR("HR4988 VREF restore failed");
        return false;
    }
}

void hr4988_simple_print_status() {
    if (!vref_ctrl.is_initialized) {
        LOG_MSG("HR4988 VREF not initialized");
        return;
    }
    
    float current = hr4988_simple_get_current();
    
    LOG_MSG("=== HR4988 VREF Status (Unified) ===");
    LOG_MSG_F("Mode: %s", hr4988_simple_get_mode_name(vref_ctrl.mode));
    LOG_MSG_F("Current: %.3fA", current);
    LOG_MSG_F("VREF: %.3fV", vref_ctrl.target_voltage);
    LOG_MSG_F("DAC: %d/255", vref_ctrl.dac_value);
    LOG_MSG_F("Adaptive: %s", vref_ctrl.adaptive_enabled ? "enabled" : "disabled");
    LOG_MSG_F("Thermal Protection: %s", vref_ctrl.thermal_protection_enabled ? "enabled" : "disabled");
    LOG_MSG_F("Temperature: %.1f°C", vref_ctrl.motor_temperature);
    LOG_MSG_F("GPIO: %d", HR4988_VREF_PIN);
    LOG_MSG("=====================================");
}

#endif // HR4988_VREF_PIN
#endif // AUTO_PAPER_CHANGE_ENABLE