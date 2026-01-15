/*
  PaperChange.cpp - ESP32写字机自动换纸系统主模块
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
  版本: v4.0 (模块化重构版)
  更新日期: 2026-01-14
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE
#include "PaperChange.h"
#include "PaperChangeConfig.h"
#include "PaperChangeTypes.h"
#include "PaperChangeHardware.h"
#include "PaperChangeLogic.h"
#include "PaperChangeUtils.h"
#include "SmartM0.h"
#include "Report.h"

// ================================================================================
// 全局变量定义 - 系统核心状态管理
// ================================================================================

// 主控制结构
static paper_change_ctrl_t paper_ctrl;

// 错误信息结构
static error_info_t last_error;

// 电机时序控制结构
static motor_timing_t panel_motor_timing = {0, 0, false};
static motor_timing_t feed_motor_timing = {0, 0, false};
static motor_timing_t clamp_motor_timing = {0, 0, false};

// 静态标志 - 用于状态间传递信息
static bool positioning_init = false;
static bool eject_detected = false;
static bool reverse_complete = false;

// ================================================================================
// 模块间接口函数实现 - 供其他模块调用
// ================================================================================

/**
 * @brief 获取主控制结构
 */
paper_change_ctrl_t* get_paper_control() {
    return &paper_ctrl;
}

/**
 * @brief 获取错误信息结构
 */
error_info_t* get_error_info() {
    return &last_error;
}

/**
 * @brief 获取面板电机时序控制
 */
motor_timing_t* get_panel_motor_timing() {
    return &panel_motor_timing;
}

/**
 * @brief 获取进纸电机时序控制
 */
motor_timing_t* get_feed_motor_timing() {
    return &feed_motor_timing;
}

/**
 * @brief 获取夹紧电机时序控制
 */
motor_timing_t* get_clamp_motor_timing() {
    return &clamp_motor_timing;
}

/**
 * @brief 获取静态标志
 */
void get_static_flags(bool* positioning_init_val, bool* eject_detected_val, bool* reverse_complete_val) {
    if (positioning_init_val) *positioning_init_val = positioning_init;
    if (eject_detected_val) *eject_detected_val = eject_detected;
    if (reverse_complete_val) *reverse_complete_val = reverse_complete;
}

/**
 * @brief 设置静态标志
 */
void set_static_flags(bool positioning_init_val, bool eject_detected_val, bool reverse_complete_val) {
    positioning_init = positioning_init_val;
    eject_detected = eject_detected_val;
    reverse_complete = reverse_complete_val;
}

// ================================================================================
// 公共接口函数实现 - 供外部调用
// ================================================================================

/**
 * @brief 初始化换纸系统
 */
// ================================================================================
// 初始化辅助函数 - 提高代码可读性
// ================================================================================

/**
 * @brief 硬件系统初始化
 * @return true=成功, false=失败
 */
static bool init_hardware_system() {
    LOG_MSG("Initializing hardware system");
    
    // 硬件初始化
    hc595_init();
    sensor_system_init();
    
    // HR4988 VREF动态电流控制初始化
    if (!init_hr4988_vref()) {
        LOG_WARNING("HR4988 VREF initialization failed, using default current");
    }
    
    return true;
}

/**
 * @brief 系统验证和自检
 * @return true=通过, false=失败
 */
static bool validate_and_selftest() {
    LOG_MSG("Validating system parameters and running self-test");
    
    // 系统参数验证
    if (!validate_system_parameters()) {
        LOG_ERROR("System parameters validation failed");
        return false;
    }
    
    // 硬件自检
    if (!hardware_self_test()) {
        LOG_ERROR("Hardware self-test failed");
        return false;
    }
    
    return true;
}

/**
 * @brief 初始化控制结构和数据
 */
static void init_control_structures() {
    LOG_MSG("Initializing control structures");
    
    // 初始化控制结构
    memset(&paper_ctrl, 0, sizeof(paper_change_ctrl_t));
    paper_ctrl.state = PAPER_IDLE;
    paper_ctrl.state_timer = millis();
    paper_ctrl.hc595_output = 0;
    
    // 初始化错误信息
    memset(&last_error, 0, sizeof(error_info_t));
    last_error.error_type = ERROR_NONE;
    last_error.auto_recovery_enabled = true;
    
    // 初始化静态标志
    positioning_init = false;
    eject_detected = false;
    reverse_complete = false;
}

/**
 * @brief 完成系统初始化
 */
static void complete_initialization() {
    // 启用换纸电机（系统初始化完成）
    enable_paper_motors();
    LOG_MSG("Paper change system initialization complete");
    
    // 初始化电机时序
    init_motor_timing();
}

// ================================================================================
// 主初始化函数 - 简化版本
// ================================================================================

void paper_change_init() {
    LOG_MSG("Initializing Paper Change System v4.0");
    
    // 硬件系统初始化
    if (!init_hardware_system()) {
        LOG_ERROR("Hardware initialization failed");
        return;
    }
    
    // 系统验证和自检
    if (!validate_and_selftest()) {
        LOG_ERROR("System validation failed");
        return;
    }
    
    // 初始化控制结构
    init_control_structures();
    
    // 完成初始化
    complete_initialization();
    
    LOG_MSG("Paper Change System initialized successfully");
}

/**
 * @brief 启动自动换纸序列
 */
void paper_change_start() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    if (ctrl->state != PAPER_IDLE) {
        LOG_ERROR("Paper change already in progress");
        return;
    }
    
    LOG_MSG("Starting automatic paper change sequence");
    ctrl->one_click_active = true;
    enter_state(PAPER_PRE_CHECK);
}

/**
 * @brief 一键换纸功能
 */
void paper_change_one_click() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    if (ctrl->state != PAPER_IDLE) {
        LOG_ERROR("Paper change already in progress");
        return;
    }
    
    LOG_MSG("One-click paper change activated");
    ctrl->one_click_active = true;
    enter_state(PAPER_PRE_CHECK);
}

/**
 * @brief 紧急停止换纸操作
 */
void paper_change_stop() {
    LOG_MSG("Emergency stop activated");
    
    STOP_ALL_MOTORS();
    
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (ctrl) {
        ctrl->emergency_stop = true;
        ctrl->one_click_active = false;
        enter_state(PAPER_ERROR);
    }
}

/**
 * @brief 更新换纸状态机 - 在主循环中调用
 * 
 * 这是换纸系统的主循环更新函数，负责：
 * 1. 传感器状态采样和跳变检测
 * 2. 状态机逻辑更新
 * 3. 用户按钮状态检查
 * 
 * @note 调用频率：由主循环决定，通常每几毫秒调用一次
 * @warning 必须按顺序执行：先更新传感器，再更新状态机
 * 
 * @example 执行流程：
 * 读取传感器 → 检测跳变 → 更新状态机 → 处理按钮 → 等待下次调用
 */
void paper_change_update() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) {
        LOG_ERROR("Control structure not available for update");
        return;
    }
    
    // 步骤1：传感器状态更新和跳变检测
    // 顺序很重要：先保存上一状态，再读取当前状态
    ctrl->last_paper_sensor_state = ctrl->paper_sensor_state;  // 保存上一状态（用于跳变检测）
    ctrl->paper_sensor_state = read_paper_sensor_debounced();   // 读取当前去抖动状态
    
    // 步骤2：状态机逻辑更新
    // 基于传感器状态和当前状态，执行相应的业务逻辑
    paper_state_machine_update();
    
    // 步骤2.5：HR4988 VREF控制更新
    // 更新动态电流控制状态
    update_hr4988_vref();
    
    // 步骤3：用户交互检查
    // 检查一键换纸按钮是否被按下
    check_button_press();
}

/**
 * @brief 检查换纸系统是否活跃
 */
bool paper_change_is_active() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    return (ctrl && ctrl->state != PAPER_IDLE && ctrl->state != PAPER_ERROR);
}

/**
 * @brief 获取当前换纸状态
 */
const char* paper_change_get_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return "UNKNOWN";
    return GET_STATE_NAME(ctrl->state);
}

/**
 * @brief 重置换纸系统到初始状态
 */
void paper_change_reset() {
    LOG_MSG("Resetting paper change system");
    
    STOP_ALL_MOTORS();
    
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (ctrl) {
        ctrl->state = PAPER_IDLE;
        ctrl->state_timer = millis();
        ctrl->step_counter = 0;
        ctrl->one_click_active = false;
        ctrl->emergency_stop = false;
        ctrl->hc595_output = 0;
    }
    
    clear_error_state();
    init_motor_timing();
    
    // 重置静态标志
    positioning_init = false;
    eject_detected = false;
    reverse_complete = false;
}

// ================================================================================
// 调试函数实现 - 手动测试接口
// ================================================================================

/**
 * @brief 调试进纸电机 (M800)
 */
void debug_feed_motor(int steps, uint32_t delay_us) {
    if (!validate_system_parameters()) {
        LOG_ERROR("System parameters invalid");
        return;
    }
    
    motor_config_t config = {
        .step_bit = BIT_FEED_MOTOR_STEP,
        .dir_bit = BIT_FEED_MOTOR_DIR,
        .step_interval = delay_us,
        .name = "Feed Debug",
        .timing = get_feed_motor_timing()
    };
    
    LOG_PROGRESS("Debug feed motor: %d steps, %u us interval", steps, delay_us);
    
    for (int i = 0; i < abs(steps); i++) {
        nonblocking_motor_step(&config, steps > 0);
        delayMicroseconds(delay_us);
    }
    
    STOP_ALL_MOTORS();
}

/**
 * @brief 调试面板电机 (M801)
 */
void debug_panel_motor(int steps, uint32_t delay_us) {
    if (!validate_system_parameters()) {
        LOG_ERROR("System parameters invalid");
        return;
    }
    
    motor_config_t config = {
        .step_bit = BIT_PANEL_MOTOR_STEP,
        .dir_bit = BIT_PANEL_MOTOR_DIR,
        .step_interval = delay_us,
        .name = "Panel Debug",
        .timing = get_panel_motor_timing()
    };
    
    LOG_PROGRESS("Debug panel motor: %d steps, %u us interval", steps, delay_us);
    
    for (int i = 0; i < abs(steps); i++) {
        nonblocking_motor_step(&config, steps > 0);
        delayMicroseconds(delay_us);
    }
    
    STOP_ALL_MOTORS();
}

/**
 * @brief 调试夹紧电机 (M802)
 */
void debug_clamp_motor(int steps, uint32_t delay_us) {
    if (!validate_system_parameters()) {
        LOG_ERROR("System parameters invalid");
        return;
    }
    
    motor_config_t config = {
        .step_bit = BIT_PAPER_CLAMP_STEP,
        .dir_bit = BIT_PAPER_CLAMP_DIR,
        .step_interval = delay_us,
        .name = "Clamp Debug",
        .timing = get_clamp_motor_timing()
    };
    
    LOG_PROGRESS("Debug clamp motor: %d steps, %u us interval", steps, delay_us);
    
    for (int i = 0; i < abs(steps); i++) {
        nonblocking_motor_step(&config, steps > 0);
        delayMicroseconds(delay_us);
    }
    
    STOP_ALL_MOTORS();
}

/**
 * @brief 读取所有传感器状态 (M803)
 */
void debug_read_sensors() {
    LOG_MSG("=== Sensor Status ===");
    
    bool paper_raw = read_paper_sensor_raw();
    bool paper_debounced = read_paper_sensor_debounced();
    bool button_state = read_button_state();
    
    LOG_PROGRESS("Paper sensor (raw): %s", paper_raw ? "DETECTED" : "CLEAR");
    LOG_PROGRESS("Paper sensor (debounced): %s", paper_debounced ? "DETECTED" : "CLEAR");
    LOG_PROGRESS("Button state: %s", button_state ? "PRESSED" : "RELEASED");
    
    const char* state = paper_change_get_state();
    LOG_PROGRESS("Paper change state: %s", state);
    
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (ctrl) {
        LOG_PROGRESS("Step counter: %lu", ctrl->step_counter);
        LOG_PROGRESS("HC595 output: 0x%02X", ctrl->hc595_output);
    }
    
    LOG_MSG("=== End Sensor Status ===");
}

/**
 * @brief 紧急停止所有电机 (M804)
 */
void debug_emergency_stop() {
    LOG_MSG("Debug emergency stop");
    paper_change_stop();
}

/**
 * @brief 复位紧急停止标志 (M805)
 */
void debug_reset_emergency() {
    LOG_MSG("Debug reset emergency");
    
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (ctrl) {
        ctrl->emergency_stop = false;
        if (ctrl->state == PAPER_ERROR) {
            enter_state(PAPER_IDLE);
        }
    }
    
    clear_error_state();
}

/**
 * @brief 直接HC595控制 (M806)
 */
void debug_hc595_direct(uint8_t data) {
    LOG_PROGRESS("Debug HC595 direct write: 0x%02X", data);
    hc595_write(data);
    
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (ctrl) {
        ctrl->hc595_output = data;
    }
}

#endif // AUTO_PAPER_CHANGE_ENABLE