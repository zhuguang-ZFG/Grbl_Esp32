/*
  PaperChangeUtils.cpp - 换纸系统工具函数实现
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE
#include "PaperChangeConfig.h"
#include "PaperChangeTypes.h"
#include "PaperChangeUtils.h"
#include "PaperChangeHardware.h"
#include "PaperChangeLogic.h"

// ================================================================================
// 时间管理工具实现
// ================================================================================

/**
 * @brief 检查是否到了执行步进的时间
 */
bool should_step_now(motor_timing_t* timing, uint32_t current_time) {
    if (!timing) return false;
    return (current_time - timing->last_step_time) >= timing->step_interval;
}

/**
 * @brief 安排下一次步进时间
 */
void schedule_next_step(motor_timing_t* timing, uint32_t current_time) {
    if (!timing) return;
    timing->last_step_time = current_time;
}

/**
 * @brief 初始化电机时间控制器
 */
void init_motor_timing() {
    motor_timing_t* panel_timing = get_panel_motor_timing();
    motor_timing_t* feed_timing = get_feed_motor_timing();
    motor_timing_t* clamp_timing = get_clamp_motor_timing();
    
    uint32_t current_time = micros();
    
    if (panel_timing) {
        panel_timing->last_step_time = current_time;
        panel_timing->step_interval = PAPER_PRE_CHECK_INTERVAL_US;
        panel_timing->step_pending = false;
    }
    
    if (feed_timing) {
        feed_timing->last_step_time = current_time;
        feed_timing->step_interval = PAPER_FEED_INTERVAL_US;
        feed_timing->step_pending = false;
    }
    
    if (clamp_timing) {
        clamp_timing->last_step_time = current_time;
        clamp_timing->step_interval = DEFAULT_STEP_PULSE_MICROSECONDS;
        clamp_timing->step_pending = false;
    }
}

// ================================================================================
// 传感器处理工具实现
// ================================================================================

/**
 * @brief 通用传感器检测函数
 */
sensor_result_t check_paper_sensor(bool check_edge, uint32_t min_steps) {
    sensor_result_t result = {0};
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return result;
    
    result.detected = read_paper_sensor_debounced();
    result.step_count = ctrl->step_counter;
    
    if (check_edge) {
        result.edge_detected = result.detected && !ctrl->last_paper_sensor_state;
    } else if (result.detected && result.step_count >= min_steps) {
        result.edge_detected = true;
    } else {
        result.edge_detected = false;
    }
    
    return result;
}

// ================================================================================
// 电机控制工具实现
// ================================================================================

/**
 * @brief 非阻塞式电机步进
 */
bool nonblocking_motor_step(const motor_config_t* config, bool forward) {
    if (!config || !config->timing) return false;
    
    uint32_t current_time = micros();
    
    if (config->step_interval > 0) {
        config->timing->step_interval = config->step_interval;
    }
    
    if (!should_step_now(config->timing, current_time)) {
        return false;
    }
    
    motor_step_control(config->step_bit, config->dir_bit, forward, config->timing->step_interval);
    schedule_next_step(config->timing, current_time);
    
    return true;
}

/**
 * @brief 面板电机非阻塞步进
 */
bool nonblocking_panel_step(bool forward) {
    motor_config_t config = {
        .step_bit = BIT_PANEL_MOTOR_STEP,
        .dir_bit = BIT_PANEL_MOTOR_DIR,
        .step_interval = 0,
        .name = "Panel",
        .timing = get_panel_motor_timing()
    };
    
    return nonblocking_motor_step(&config, forward);
}

/**
 * @brief 进纸电机非阻塞步进
 */
bool nonblocking_feed_step(bool forward) {
    motor_config_t config = {
        .step_bit = BIT_FEED_MOTOR_STEP,
        .dir_bit = BIT_FEED_MOTOR_DIR,
        .step_interval = PAPER_FEED_INTERVAL_US,
        .name = "Feed",
        .timing = get_feed_motor_timing()
    };
    
    return nonblocking_motor_step(&config, forward);
}

// ================================================================================
// 数据转换工具实现
// ================================================================================

/**
 * @brief 步数转换为毫米
 */
float steps_to_mm(uint32_t steps, float steps_per_mm) {
    if (steps_per_mm <= 0) return 0;
    return (float)steps / steps_per_mm;
}

/**
 * @brief 毫米转换为步数
 */
uint32_t mm_to_steps(float mm, float steps_per_mm) {
    if (steps_per_mm <= 0) return 0;
    return (uint32_t)(mm * steps_per_mm);
}

/**
 * @brief 获取错误类型名称
 */
const char* get_error_type_name(paper_error_type_t error_type) {
    switch (error_type) {
        case ERROR_NONE: return "None";
        case ERROR_SENSOR_TIMEOUT: return "Sensor Timeout";
        case ERROR_MOTOR_STALL: return "Motor Stall";
        case ERROR_STATE_TIMEOUT: return "State Timeout";
        case ERROR_PARAMETER_INVALID: return "Parameter Invalid";
        case ERROR_HARDWARE_FAULT: return "Hardware Fault";
        case ERROR_COMMUNICATION: return "Communication";
        case ERROR_EMERGENCY_STOP: return "Emergency Stop";
        case ERROR_UNKNOWN: 
        default: return "Unknown";
    }
}

/**
 * @brief 获取恢复策略名称
 */
const char* get_recovery_strategy_name(recovery_strategy_t strategy) {
    switch (strategy) {
        case RECOVERY_NONE: return "None";
        case RECOVERY_RETRY: return "Retry";
        case RECOVERY_RESET_MOTORS: return "Reset Motors";
        case RECOVERY_FULL_RESTART: return "Full Restart";
        case RECOVERY_SAFE_SHUTDOWN: return "Safe Shutdown";
        case RECOVERY_MANUAL_INTERVENTION: return "Manual Intervention";
        default: return "Unknown";
    }
}

// ================================================================================
// 安全检查工具实现
// ================================================================================

/**
 * @brief 系统参数验证
 */
bool validate_system_parameters() {
    bool all_valid = true;
    
    // 电机参数验证
    if (!VALIDATE_STEPS_PER_MM(DEFAULT_FEED_STEPS_PER_MM)) {
        LOG_ERROR("Invalid feed steps/mm");
        all_valid = false;
    }
    
    if (!VALIDATE_STEPS_PER_MM(DEFAULT_PANEL_STEPS_PER_MM)) {
        LOG_ERROR("Invalid panel steps/mm");
        all_valid = false;
    }
    
    if (!VALIDATE_MAX_RATE(DEFAULT_FEED_MAX_RATE)) {
        LOG_ERROR("Invalid feed max rate");
        all_valid = false;
    }
    
    if (!VALIDATE_MAX_RATE(DEFAULT_PANEL_MAX_RATE)) {
        LOG_ERROR("Invalid panel max rate");
        all_valid = false;
    }
    
    // 步数参数验证
    if (!VALIDATE_STEPS_COUNT(PAPER_EJECT_STEPS)) {
        LOG_ERROR("Invalid eject steps");
        all_valid = false;
    }
    
    if (!VALIDATE_STEPS_COUNT(PAPER_PRE_CHECK_STEPS)) {
        LOG_ERROR("Invalid pre-check steps");
        all_valid = false;
    }
    
    // 时间参数验证
    if (!VALIDATE_TIMEOUT_MS(PAPER_EJECT_INTERVAL_US)) {
        LOG_ERROR("Invalid eject interval");
        all_valid = false;
    }
    
    if (!VALIDATE_TIMEOUT_MS(PAPER_FEED_INTERVAL_US)) {
        LOG_ERROR("Invalid feed interval");
        all_valid = false;
    }
    
    // GPIO引脚验证
    if (!VALIDATE_GPIO_PIN(PAPER_SENSOR_PIN)) {
        LOG_ERROR("Invalid sensor pin");
        all_valid = false;
    }
    
    if (!VALIDATE_GPIO_PIN(HC595_DATA_PIN) || 
        !VALIDATE_GPIO_PIN(HC595_CLOCK_PIN) || 
        !VALIDATE_GPIO_PIN(HC595_LATCH_PIN)) {
        LOG_ERROR("Invalid HC595 pins");
        all_valid = false;
    }
    
    return all_valid;
}

/**
 * @brief 运行时安全检查
 */
bool runtime_safety_check(uint32_t step_counter, uint32_t max_steps, const char* operation_name) {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return false;
    
    // 步数溢出检查
    if (step_counter >= max_steps) {
        LOG_ERROR_F("Safety limit exceeded in %s: %lu/%lu", operation_name, step_counter, max_steps);
        STOP_ALL_MOTORS();
        enter_state(PAPER_ERROR);
        return false;
    }
    
    // 状态时间检查
    uint32_t state_duration = millis() - ctrl->state_timer;
    if (state_duration > PAPER_STATE_TIMEOUT_MS) {
        LOG_ERROR_F("State timeout in %s: %lums", operation_name, state_duration);
        STOP_ALL_MOTORS();
        enter_state(PAPER_ERROR);
        return false;
    }
    
    return true;
}

// ================================================================================
// 故障处理工具实现
// ================================================================================

/**
 * @brief 故障恢复策略决策
 */
recovery_strategy_t determine_recovery_strategy(paper_error_type_t error_type, uint8_t retry_count) {
    switch (error_type) {
        case ERROR_SENSOR_TIMEOUT:
            return (retry_count < PAPER_MAX_RETRY_COUNT) ? RECOVERY_RETRY : RECOVERY_MANUAL_INTERVENTION;
            
        case ERROR_MOTOR_STALL:
            return (retry_count < 2) ? RECOVERY_RESET_MOTORS : RECOVERY_MANUAL_INTERVENTION;
            
        case ERROR_STATE_TIMEOUT:
            return RECOVERY_FULL_RESTART;
            
        case ERROR_PARAMETER_INVALID:
            return RECOVERY_MANUAL_INTERVENTION;
            
        case ERROR_HARDWARE_FAULT:
            return RECOVERY_SAFE_SHUTDOWN;
            
        case ERROR_EMERGENCY_STOP:
            return RECOVERY_NONE;
            
        case ERROR_COMMUNICATION:
            return (retry_count < 5) ? RECOVERY_RETRY : RECOVERY_FULL_RESTART;
            
        default:
            return RECOVERY_NONE;
    }
}

/**
 * @brief 执行故障恢复操作
 */
bool execute_recovery(recovery_strategy_t strategy) {
    LOG_PROGRESS("Executing recovery strategy: %s", get_recovery_strategy_name(strategy));
    
    switch (strategy) {
        case RECOVERY_RETRY:
            {
                paper_change_ctrl_t* ctrl = get_paper_control();
                if (ctrl) {
                    ctrl->step_counter = 0;
                    ctrl->state_timer = millis();
                }
                LOG_MSG("Retry current operation");
                return true;
            }
            
        case RECOVERY_RESET_MOTORS:
            LOG_MSG("Resetting all motors");
            stop_all_motors();
            delay(100);
            init_motor_timing();
            {
                paper_change_ctrl_t* ctrl = get_paper_control();
                if (ctrl) ctrl->step_counter = 0;
            }
            return true;
            
        case RECOVERY_FULL_RESTART:
            LOG_MSG("Full system restart");
            stop_all_motors();
            enter_state(PAPER_IDLE);
            return true;
            
        case RECOVERY_SAFE_SHUTDOWN:
            LOG_MSG("Safe shutdown activated");
            stop_all_motors();
            {
                paper_change_ctrl_t* ctrl = get_paper_control();
                if (ctrl) ctrl->emergency_stop = true;
            }
            enter_state(PAPER_ERROR);
            return false;
            
        case RECOVERY_MANUAL_INTERVENTION:
            LOG_MSG("Manual intervention required");
            stop_all_motors();
            enter_state(PAPER_ERROR);
            return false;
            
        default:
            LOG_ERROR("Unknown recovery strategy");
            return false;
    }
}

/**
 * @brief 错误处理函数
 */
void handle_error(paper_error_type_t error_type, const char* description, paper_change_state_t current_state) {
    error_info_t* error_info = get_error_info();
    paper_change_ctrl_t* ctrl = get_paper_control();
    
    if (!error_info || !ctrl) return;
    
    // 记录错误信息
    error_info->error_type = error_type;
    error_info->error_time = millis();
    error_info->error_state = current_state;
    error_info->step_count_at_error = ctrl->step_counter;
    strncpy(error_info->error_description, description, sizeof(error_info->error_description) - 1);
    error_info->error_description[sizeof(error_info->error_description) - 1] = '\0';
    
    // 发送错误报告
    grbl_sendf(CLIENT_ALL, "[MSG: ERROR - %s (State: %s, Steps: %lu, Retry: %u)]\r\n",
               description,
               GET_STATE_NAME(current_state),
               error_info->step_count_at_error,
               error_info->retry_count);
    
    // 确定恢复策略
    recovery_strategy_t strategy = determine_recovery_strategy(error_type, error_info->retry_count);
    
    // 执行恢复操作
    if (execute_recovery(strategy)) {
        error_info->retry_count++;
        LOG_PROGRESS("Recovery attempt %u completed", error_info->retry_count);
    } else {
        LOG_ERROR("Recovery failed, requires manual intervention");
        error_info->auto_recovery_enabled = false;
    }
}

/**
 * @brief 清除错误状态
 */
void clear_error_state() {
    error_info_t* error_info = get_error_info();
    paper_change_ctrl_t* ctrl = get_paper_control();
    
    if (error_info) {
        memset(error_info, 0, sizeof(error_info_t));
        error_info->error_type = ERROR_NONE;
    }
    
    if (ctrl) {
        ctrl->emergency_stop = false;
    }
    
    LOG_MSG("Error state cleared, system ready");
}

/**
 * @brief 获取状态名称字符串
 */
const char* get_state_name_string(paper_change_state_t state) {
    if (state >= 0 && state < PAPER_ERROR + 1) {
        return STATE_NAMES[state];
    }
    return "UNKNOWN";
}

#endif // AUTO_PAPER_CHANGE_ENABLE