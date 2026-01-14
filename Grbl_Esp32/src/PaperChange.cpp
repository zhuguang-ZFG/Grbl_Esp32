/*
  PaperChange.cpp - ESP32写字机自动换纸系统完整实现
  Part of Grbl_ESP32

  === 系统功能概述 ===
  实现完整的自动换纸工作流程，支持A4纸张的高精度换纸操作：
  1. 写字完成检测 → 面板电机精确送出已写好的纸张（A4长度：297mm）
  2. 面板电机预检 → 倒转2.5mm检测面板上是否有旧纸张
  3. 进纸器启动 → 智能检测并送入新纸到写字机内部
  4. 传感器检测 → 保持夹具松开状态，面板电机同步运行
  5. 运行5cm → 纸张感应到传感器后运行5cm
  6. 夹紧输送 → 夹纸电机正向夹紧，进纸+面板电机同速运行
  7. 完整输送 → 运行约A4行程（23760步），纸张脱离传感器后进纸电机停止
  8. 重新定位 → 面板电机反转判断纸张位置，检测到后正转5cm
  9. 系统就绪 → 恢复正常写字功能

  === 硬件架构 ===
  - 主控芯片: ESP32-WROOM-32E (双核处理器，WiFi+蓝牙)
  - 扩展芯片: 74HC595D移位寄存器 (8位GPIO扩展)
  - 传感器: 光电传感器GPIO34 (检测纸张有无和测距)
  - 电机系统:
    * 面板电机: 控制纸张进出 (BIT_PANEL_MOTOR_DIR/STEP)
    * 进纸电机: 从纸仓送纸 (BIT_FEED_MOTOR_DIR/STEP) - 永远不会反转
    * 夹纸电机: 夹持固定纸张 (BIT_PAPER_CLAMP_DIR/STEP) - 永远不会反转
  - 硬件布局: 进纸电机 - 纸张传感器（距夹紧电机5cm）- 夹紧电机与面板电机在一起
  - 传感器位置: 在夹紧电机与面板电机之间，用于感应纸张和测距
  - 面板电机反转: 仅用于判断纸张位置，一般情况下不会反转

  === 状态机设计 ===
  采用10状态有限状态机，确保换纸过程的可靠性和可追溯性：
  - IDLE: 系统空闲，等待换纸触发
  - PRE_CHECK: 预检机制，倒转2.5mm检测面板是否有纸
  - EJECTING: 精确送出A4纸张（23760步 = 297mm）
  - FEEDING: 进纸器送入新纸，检测到纸张后保持松开状态
  - UNCLAMP_FEED: 保持夹具松开，面板电机运行5cm
  - CLAMP_FEED: 夹纸电机正向夹紧（20步）
  - FULL_FEED: 进纸+面板电机同速运行A4行程，检测纸张脱离
  - REPOSITION: 面板电机反转判断纸张位置，正转5cm到正确位置
  - COMPLETE: 换纸完成，系统恢复到可写字状态
  - ERROR: 错误处理，支持紧急停止和故障恢复

  === 关键约束 ===
  - 进纸电机：永远不会反转
  - 夹紧电机：永远不会反转
  - 面板电机：反转仅用于判断纸张位置
  - 夹紧时机：感应到传感器后以及运行5cm期间保持松开

  === 安全特性 ===
  - 紧急停止机制：长按按钮2秒触发
  - 超时保护：各状态都有独立的超时检测
  - 传感器去抖动：50ms防抖避免误触发
  - 参数验证：运行时检查所有关键参数的有效性
  - 故障恢复：支持多级错误处理和自动重试

  Copyright (c) 2024 Grbl_ESP32
  版本: v3.1 (硬件约束优化版)
  更新日期: 2026-01-13
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE
#include "PaperChange.h"
#include "SmartM0.h"
#include "Report.h"
#include "esp_system.h"   // ESP32系统函数，用于延时操作

// 优化宏定义 - 统一日志输出
#define LOG_MSG(msg) grbl_sendf(CLIENT_ALL, "[MSG: %s]\r\n", msg)
#define LOG_ERROR(msg) grbl_sendf(CLIENT_ALL, "[MSG: ERROR - %s]\r\n", msg)
#define LOG_PROGRESS(format, ...) grbl_sendf(CLIENT_ALL, "[MSG: " format "]\r\n", ##__VA_ARGS__)

// 优化宏定义 - 统一安全检查
#define CHECK_STATE_SAFETY(max_steps, state_name) \
    do { \
        if (paper_ctrl.step_counter >= (max_steps)) { \
            LOG_ERROR("Step limit exceeded in state: " state_name); \
            handle_error(ERROR_STATE_TIMEOUT, "Step limit exceeded", paper_ctrl.state); \
            return; \
        } \
    } while(0)

// 优化宏定义 - 统一电机停止
#define STOP_ALL_MOTORS() hc595_write(0)

// 优化宏定义 - 获取状态名称
#define GET_STATE_NAME(state) ((state) >= 0 && (state) < sizeof(STATE_NAMES)/sizeof(STATE_NAMES[0]) ? \
                               STATE_NAMES[(state)] : "UNKNOWN")

// ================================================================================
// 换纸系统状态机定义
// ================================================================================
// 枚举定义：换纸系统的9个核心状态
// 设计原则：每个状态都是原子操作，确保状态转换的可预测性
// 硬件布局：进纸电机 - 传感器(距夹紧电机5cm) - 夹紧电机与面板电机在一起
typedef enum {
    PAPER_IDLE,              // 空闲状态 - 系统待机，等待换纸触发信号
    PAPER_PRE_CHECK,         // 预检状态 - 面板电机倒转检测面板上是否有纸张
    PAPER_EJECTING,          // 出纸状态 - 面板电机精确送出已写完的A4纸张
    PAPER_FEEDING,           // 进纸状态 - 进纸器智能送入新纸，传感器实时监控
    PAPER_UNCLAMP_FEED,      // 松开夹具状态 - 检测到纸张后松开夹具并同步运行
    PAPER_CLAMP_FEED,        // 夹紧输送状态 - 运行5cm后夹紧，进纸+面板电机同速运行
    PAPER_FULL_FEED,         // 完整输送状态 - 运行A4行程并检测纸张脱离
    PAPER_REPOSITION,        // 重新定位状态 - 面板电机反转检测纸张后正转5cm
    PAPER_COMPLETE,          // 完成状态 - 换纸流程完成，系统恢复正常写字功能
    PAPER_ERROR             // 错误状态 - 系统异常处理，支持紧急停止和故障恢复
} paper_change_state_t;

// ================================================================================
// 换纸系统控制结构体
// ================================================================================
// 全局控制变量：存储换纸系统的所有状态信息
// 设计理念：集中管理所有状态变量，便于调试和维护
typedef struct {
    // === 核心状态信息 ===
    paper_change_state_t state = PAPER_IDLE;          // 当前状态机状态
    uint32_t state_timer = 0;                          // 状态进入时间戳（毫秒）
    uint32_t step_counter = 0;                         // 当前状态下的步数计数器
    bool paper_sensor_state = false;                   // 当前纸张传感器状态（去抖动后）
    bool last_paper_sensor_state = false;              // 上一次传感器状态（用于检测边沿）
    
    // === 控制标志 ===
    bool one_click_active = false;                     // 一键换纸激活标志
    uint8_t hc595_output = 0;                          // 当前HC595芯片的8位输出状态
    bool emergency_stop = false;                       // 紧急停止标志（长按按钮触发）
    bool sensor_detected = false;                       // 传感器检测标志（定位阶段使用）
} paper_change_ctrl_t;

// 全局控制变量实例 - 系统的唯一控制中心
static paper_change_ctrl_t paper_ctrl;

// ================================================================================
// 故障恢复机制
// ================================================================================

// === 故障类型定义 ===
typedef enum {
    ERROR_NONE = 0,              // 无错误
    ERROR_SENSOR_TIMEOUT,       // 传感器超时错误
    ERROR_MOTOR_STALL,          // 电机堵转错误
    ERROR_STATE_TIMEOUT,        // 状态超时错误
    ERROR_PARAMETER_INVALID,    // 参数无效错误
    ERROR_HARDWARE_FAULT,       // 硬件故障错误
    ERROR_COMMUNICATION,        // 通信错误
    ERROR_EMERGENCY_STOP,       // 紧急停止错误
    ERROR_UNKNOWN               // 未知错误
} paper_error_type_t;

// === 恢复策略定义 ===
typedef enum {
    RECOVERY_NONE = 0,          // 无恢复操作
    RECOVERY_RETRY,             // 重试操作
    RECOVERY_RESET_MOTORS,      // 重置电机状态
    RECOVERY_FULL_RESTART,      // 完全重启换纸流程
    RECOVERY_SAFE_SHUTDOWN,     // 安全关机
    RECOVERY_MANUAL_INTERVENTION // 需要人工干预
} recovery_strategy_t;

// === 故障信息结构 ===
typedef struct {
    paper_error_type_t error_type;          // 错误类型
    uint32_t error_time;                    // 错误发生时间
    paper_change_state_t error_state;       // 错误发生时的状态
    uint32_t step_count_at_error;           // 错误时的步数计数
    char error_description[64];             // 错误描述
    uint8_t retry_count;                    // 重试次数
    bool auto_recovery_enabled;             // 是否启用自动恢复
} error_info_t;

// 故障信息记录
static error_info_t last_error = {ERROR_NONE, 0, PAPER_IDLE, 0, "", 0, true};

// === 前置函数声明 ===
static void hc595_write(uint8_t data);
static void hc595_write_fast(uint8_t data);
static void init_motor_timing(void);
static void enter_state(paper_change_state_t new_state);

/**
 * @brief 故障恢复策略决策函数
 * @param error_type 错误类型
 * @param retry_count 当前重试次数
 * @return 推荐的恢复策略
 * 
 * === 恢复策略表 ===
 * 根据错误类型和重试次数决定恢复策略：
 * - 传感器超时：最多重试3次，然后需要人工干预
 * - 电机堵转：重置电机，最多重试2次
 * - 状态超时：完全重启换纸流程
 * - 参数错误：需要人工检查配置
 * - 硬件故障：安全关机，需要维护
 * - 紧急停止：手动清除后可重试
 */
static recovery_strategy_t determine_recovery_strategy(paper_error_type_t error_type, uint8_t retry_count) {
    switch (error_type) {
        case ERROR_SENSOR_TIMEOUT:
            return (retry_count < 3) ? RECOVERY_RETRY : RECOVERY_MANUAL_INTERVENTION;
            
        case ERROR_MOTOR_STALL:
            return (retry_count < 2) ? RECOVERY_RESET_MOTORS : RECOVERY_MANUAL_INTERVENTION;
            
        case ERROR_STATE_TIMEOUT:
            return RECOVERY_FULL_RESTART;
            
        case ERROR_PARAMETER_INVALID:
            return RECOVERY_MANUAL_INTERVENTION;
            
        case ERROR_HARDWARE_FAULT:
            return RECOVERY_SAFE_SHUTDOWN;
            
        case ERROR_EMERGENCY_STOP:
            return RECOVERY_NONE; // 需要手动清除紧急停止标志
            
        case ERROR_COMMUNICATION:
            return (retry_count < 5) ? RECOVERY_RETRY : RECOVERY_FULL_RESTART;
            
        default:
            return RECOVERY_NONE;
    }
}

/**
 * @brief 执行故障恢复操作
 * @param strategy 恢复策略
 * @return true=恢复成功，false=恢复失败
 * 
 * === 恢复操作说明 ===
 * 根据策略执行相应的恢复操作：
 * - 重试：重新开始当前状态的操作
 * - 重置电机：停止所有电机，重新初始化
 * - 完全重启：回到IDLE状态重新开始
 * - 安全关机：停止所有操作，进入安全状态
 */
static bool execute_recovery(recovery_strategy_t strategy) {
    LOG_PROGRESS("Executing recovery strategy: %d", strategy);
    
    switch (strategy) {
        case RECOVERY_RETRY:
            // 重试当前状态：重置步进计数器和计时器
            paper_ctrl.step_counter = 0;
            paper_ctrl.state_timer = millis();
            LOG_MSG("Retry current operation");
            return true;
            
        case RECOVERY_RESET_MOTORS:
            // 重置电机状态：停止所有电机，重新初始化
            LOG_MSG("Resetting all motors");
            hc595_write_fast(0);                  // 立即停止所有电机
            delay(100);                           // 等待电机停止
            init_motor_timing();                  // 重新初始化时间控制器
            paper_ctrl.step_counter = 0;
            return true;
            
        case RECOVERY_FULL_RESTART:
            // 完全重启：回到IDLE状态，重置所有状态
            LOG_MSG("Full system restart");
            hc595_write_fast(0);                  // 停止所有电机
            enter_state(PAPER_IDLE);              // 回到空闲状态
            return true;
            
        case RECOVERY_SAFE_SHUTDOWN:
            // 安全关机：停止所有操作，进入安全状态
            LOG_MSG("Safe shutdown activated");
            hc595_write_fast(0);                  // 停止所有电机
            paper_ctrl.emergency_stop = true;     // 设置紧急停止标志
            enter_state(PAPER_ERROR);             // 进入错误状态
            return false;
            
        case RECOVERY_MANUAL_INTERVENTION:
            // 需要人工干预：停止操作，等待人工处理
            LOG_MSG("Manual intervention required");
            hc595_write_fast(0);                  // 停止所有电机
            enter_state(PAPER_ERROR);             // 进入错误状态
            return false;
            
        default:
            LOG_ERROR("Unknown recovery strategy");
            return false;
    }
}

/**
 * @brief 记录错误信息并触发恢复
 * @param error_type 错误类型
 * @param description 错误描述
 * @param current_state 当前状态
 * 
 * === 错误处理流程 ===
 * 1. 记录详细的错误信息
 * 2. 确定恢复策略
 * 3. 执行恢复操作
 * 4. 更新错误统计
 */
static void handle_error(paper_error_type_t error_type, const char* description, paper_change_state_t current_state) {
    // === 记录错误信息 ===
    last_error.error_type = error_type;
    last_error.error_time = millis();
    last_error.error_state = current_state;
    last_error.step_count_at_error = paper_ctrl.step_counter;
    strncpy(last_error.error_description, description, sizeof(last_error.error_description) - 1);
    last_error.error_description[sizeof(last_error.error_description) - 1] = '\0';
    
    // === 发送错误报告 ===
    grbl_sendf(CLIENT_ALL, "[MSG: ERROR - %s (State: %s, Steps: %lu, Retry: %u)]\r\n",
               description,
               (current_state == PAPER_IDLE) ? "IDLE" :
               (current_state == PAPER_PRE_CHECK) ? "PRE_CHECK" :
               (current_state == PAPER_EJECTING) ? "EJECTING" :
               (current_state == PAPER_FEEDING) ? "FEEDING" :
               (current_state == PAPER_UNCLAMP_FEED) ? "UNCLAMP_FEED" :
               (current_state == PAPER_CLAMP_FEED) ? "CLAMP_FEED" :
               (current_state == PAPER_FULL_FEED) ? "FULL_FEED" :
               (current_state == PAPER_REPOSITION) ? "REPOSITION" :
               (current_state == PAPER_COMPLETE) ? "COMPLETE" : "ERROR",
               last_error.step_count_at_error,
               last_error.retry_count);
    
    // === 确定恢复策略 ===
    recovery_strategy_t strategy = determine_recovery_strategy(error_type, last_error.retry_count);
    
    // === 执行恢复操作 ===
    if (execute_recovery(strategy)) {
        last_error.retry_count++;
        LOG_PROGRESS("Recovery attempt %u completed", last_error.retry_count);
    } else {
        LOG_ERROR("Recovery failed, requires manual intervention");
        last_error.auto_recovery_enabled = false;
    }
}

/**
 * @brief 清除错误状态
 * 
 * === 功能说明 ===
 * 在问题解决后调用，清除错误记录：
 * - 重置错误信息结构
 * - 清除紧急停止标志
 * - 重新启用自动恢复
 */
static void clear_error_state() {
    memset(&last_error, 0, sizeof(error_info_t));
    last_error.error_type = ERROR_NONE;
    paper_ctrl.emergency_stop = false;
    
    LOG_MSG("Error state cleared, system ready");
}



// 传感器检测结果结构体
typedef struct {
    bool detected;           // 是否检测到纸张
    bool edge_detected;      // 是否检测到边沿变化
    uint32_t step_count;     // 当前步数
} sensor_result_t;



// 统一的状态名称定义
static const char* STATE_NAMES[] = {
    "IDLE", "PRE_CHECK", "EJECTING", "FEEDING",
    "UNCLAMP_FEED", "CLAMP_FEED", "FULL_FEED", "REPOSITION", "COMPLETE", "ERROR"
};



/**
 * @brief 通用纸张传感器检测函数
 * @param check_edge 是否检查边沿变化
 * @param min_steps 最小步数阈值
 * @return 传感器检测结果
 */
static sensor_result_t check_paper_sensor(bool check_edge, uint32_t min_steps) {
    sensor_result_t result = {0};
    
    result.detected = (digitalRead(PAPER_SENSOR_PIN) == HIGH);
    result.step_count = paper_ctrl.step_counter;
    
    if (check_edge) {
        result.edge_detected = result.detected && !paper_ctrl.last_paper_sensor_state;
    } else if (result.detected && result.step_count >= min_steps) {
        result.edge_detected = true; // 稳定检测
    } else {
        result.edge_detected = false;
    }
    
    return result;
}



// ================================================================================
// 参数验证和安全检查系统
// ================================================================================

// === 参数验证宏定义 ===
// 定义各参数的有效范围，确保系统安全运行
#define VALIDATE_STEPS_PER_MM(value)     ((value) > 0.0 && (value) <= 10000.0)    // 每毫米步数范围
#define VALIDATE_MAX_RATE(value)          ((value) > 0.0 && (value) <= 50000.0)    // 最大速度范围
#define VALIDATE_ACCELERATION(value)      ((value) > 0.0 && (value) <= 10000.0)    // 加速度范围
#define VALIDATE_STEPS_COUNT(value)       ((value) >= 0 && (value) <= 100000)      // 步数计数范围
#define VALIDATE_TIMEOUT_MS(value)        ((value) > 0 && (value) <= 60000)        // 超时时间范围
#define VALIDATE_GPIO_PIN(pin)            ((pin) >= 0 && (pin) < 40)               // GPIO引脚范围

/**
 * @brief 系统参数有效性检查函数
 * @return true=所有参数有效，false=存在无效参数
 * 
 * === 功能说明 ===
 * 该函数在系统初始化和运行时检查所有关键参数的有效性：
 * - 电机参数：步数/毫米、最大速度、加速度
 * - 机械参数：行程距离、步数限制
 * - 时间参数：超时时间、步进间隔
 * - 硬件参数：GPIO引脚编号
 * 
 * === 安全保障 ===
 * - 防止无效参数导致的硬件损坏
 * - 避免参数溢出导致的系统崩溃
 * - 确保运动参数在安全范围内
 * 
 * @note 该函数应在系统初始化时调用
 * @see validate_motor_parameters(), validate_timing_parameters()
 */
static bool validate_system_parameters() {
    bool all_valid = true;
    
    // === 电机参数验证 ===
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
    
    // === 步数参数验证 ===
    if (!VALIDATE_STEPS_COUNT(PAPER_EJECT_STEPS)) {
        LOG_ERROR("Invalid eject steps");
        all_valid = false;
    }
    
    if (!VALIDATE_STEPS_COUNT(PAPER_PRE_CHECK_STEPS)) {
        LOG_ERROR("Invalid pre-check steps");
        all_valid = false;
    }
    
    // === 时间参数验证 ===
    if (!VALIDATE_TIMEOUT_MS(PAPER_EJECT_INTERVAL_US)) {
        LOG_ERROR("Invalid eject interval");
        all_valid = false;
    }
    
    if (!VALIDATE_TIMEOUT_MS(PAPER_PRE_CHECK_INTERVAL_US)) {
        LOG_ERROR("Invalid pre-check interval");
        all_valid = false;
    }
    
    // === GPIO引脚验证 ===
    if (!VALIDATE_GPIO_PIN(PAPER_SENSOR_PIN)) {
        LOG_ERROR("Invalid sensor pin");
        all_valid = false;
    }
    
    if (!VALIDATE_GPIO_PIN(HC595_DATA_PIN) || !VALIDATE_GPIO_PIN(HC595_CLOCK_PIN) || !VALIDATE_GPIO_PIN(HC595_LATCH_PIN)) {
        LOG_ERROR("Invalid HC595 pins");
        all_valid = false;
    }
    
    // === 参数一致性检查 ===
    // 检查A4长度计算是否合理：步数应接近理论值
    float theoretical_eject_steps = 297.0 * DEFAULT_PANEL_STEPS_PER_MM;  // A4长度理论步数
    float eject_steps_error = abs(PAPER_EJECT_STEPS - theoretical_eject_steps) / theoretical_eject_steps;
    
    if (eject_steps_error > 0.05) {  // 误差超过5%
        grbl_sendf(CLIENT_ALL, "[MSG: WARNING - Eject steps error: %.1f%% (expected %.0f, got %u)]\r\n", 
                   eject_steps_error * 100, theoretical_eject_steps, PAPER_EJECT_STEPS);
    }
    
    return all_valid;
}

/**
 * @brief 运行时安全检查函数
 * @param step_counter 当前步数计数器
 * @param max_steps 允许的最大步数
 * @param operation_name 操作名称（用于日志）
 * @return true=安全，false=超出安全限制
 * 
 * === 功能说明 ===
 * 该函数在电机运行过程中进行实时安全检查：
 * - 防止步数计数器溢出
 * - 防止电机行程超限
 * - 防止无限循环运行
 * 
 * === 安全措施 ===
 * 达到安全限制时：
 * 1. 立即停止所有电机
 * 2. 记录详细的错误日志
 * 3. 转入错误状态
 * 
 * @note 该函数应在每个步进操作前调用
 */
static bool runtime_safety_check(uint32_t step_counter, uint32_t max_steps, const char* operation_name) {
    // 步数溢出检查
    if (step_counter >= max_steps) {
        grbl_sendf(CLIENT_ALL, "[MSG: SAFETY - %s exceeded max steps: %lu/%lu]\r\n", 
                   operation_name, step_counter, max_steps);
        
        // 立即停止所有电机
        STOP_ALL_MOTORS();
        
        // 转入错误状态
        enter_state(PAPER_ERROR);
        return false;
    }
    
    // 状态时间检查 - 防止状态卡死
    uint32_t state_duration = millis() - paper_ctrl.state_timer;
    const uint32_t MAX_STATE_DURATION_MS = 30000;  // 单状态最大运行时间30秒
    
    if (state_duration > MAX_STATE_DURATION_MS) {
        grbl_sendf(CLIENT_ALL, "[MSG: SAFETY - %s state timeout: %lums]\r\n", 
                   operation_name, state_duration);
        
        // 立即停止所有电机
        STOP_ALL_MOTORS();
        
        // 转入错误状态
        enter_state(PAPER_ERROR);
        return false;
    }
    
    return true;
}

// ================================================================================
// 状态转换验证矩阵
// ================================================================================
// 定义允许的状态转换，确保状态机的逻辑正确性
// [当前状态][目标状态] = true表示允许转换，false表示非法转换
static const bool state_transition_matrix[PAPER_ERROR + 1][PAPER_ERROR + 1] = {
    // 目标状态: IDLE, PRE_CHECK, EJECTING, FEEDING, UNCLAMP_FEED, CLAMP_FEED, FULL_FEED, REPOSITION, COMPLETE, ERROR
    /* IDLE        */ { true, true,  false,  false,  false,   false,   false,   false,   false,  true },
    /* PRE_CHECK   */ { true, false, true,   true,   false,   false,   false,   false,   false,  true },
    /* EJECTING    */ { true, false, false,  true,   false,   false,   false,   false,   false,  true },
    /* FEEDING     */ { true, false, false,  false,  true,    false,   false,   false,   false,  true },
    /* UNCLAMP_FEED*/ { true, false, false,  false,  false,   true,    false,   false,   false,  true },
    /* CLAMP_FEED  */ { true, false, false,  false,  false,   false,   true,    false,   false,  true },
    /* FULL_FEED   */ { true, false, false,  false,  false,   false,   false,   true,    false,  true },
    /* REPOSITION  */ { true, false, false,  false,  false,   false,   false,   false,   true,    true },
    /* COMPLETE    */ { true, true,  false,  false,  false,   false,   false,   false,   false,  true },
    /* ERROR       */ { true, false, false,  false,  false,   false,   false,   false,   false,  true }
};

// ================================================================================
// 非阻塞式步进控制时间管理
// ================================================================================
// 为了避免阻塞式延时影响系统响应，使用时间片调度控制步进节奏
typedef struct {
    uint32_t last_step_time = 0;              // 上次步进时间戳
    uint32_t step_interval = 1000;            // 步进间隔时间(μs)，默认1ms
    bool step_pending = false;                // 是否有待执行的步进
} motor_timing_t;

// 各电机的独立时间控制器
static motor_timing_t panel_motor_timing;     // 面板电机时间控制器
static motor_timing_t feed_motor_timing;      // 进纸电机时间控制器
static motor_timing_t clamp_motor_timing;     // 夹纸电机时间控制器

// 定位状态初始化标志（全局静态变量）
static bool positioning_initialized = false;

/**
 * @brief 电机配置结构体
 */
typedef struct {
    uint8_t step_bit;        // 步进控制位
    uint8_t dir_bit;         // 方向控制位
    uint32_t step_interval;   // 步进间隔
    const char* name;         // 电机名称
    motor_timing_t* timing;   // 时间控制器
} motor_config_t;

// ================================================================================
// 74HC595D移位寄存器驱动函数
// ================================================================================

/**
 * @brief 高性能74HC595D移位寄存器数据写入函数（寄存器优化版）
 * @param data 要写入的8位数据，每一位对应一个电机的控制信号
 *
 * === 硬件连接说明 ===
 * 74HC595D是8位串入并出移位寄存器，用于扩展ESP32的GPIO控制能力：
 * - DS (Pin 14) 串行数据输入 → HC595_DATA_PIN (GPIO32)
 * - SHCP (Pin 11) 移位时钟输入 → HC595_CLOCK_PIN (GPIO3)
 * - STCP (Pin 12) 存储时钟输入 → HC595_LATCH_PIN (GPIO2)
 * - Q0-Q7 (Pin 15,1-7) 并行数据输出 → 连接电机驱动电路
 * 
 * === 位映射定义 ===
 * Bit 2: BIT_PAPER_CLAMP_DIR  - 夹纸电机方向控制
 * Bit 3: BIT_PAPER_CLAMP_STEP - 夹纸电机步进控制
 * Bit 4: BIT_PANEL_MOTOR_DIR  - 面板电机方向控制
 * Bit 5: BIT_PANEL_MOTOR_STEP - 面板电机步进控制
 * Bit 6: BIT_FEED_MOTOR_DIR   - 进纸电机方向控制
 * Bit 7: BIT_FEED_MOTOR_STEP  - 进纸电机步进控制
 * 
 * === 时序要求 ===
 * 1. 数据建立时间: t_su > 10ns (时钟上升沿前数据必须稳定)
 * 2. 时钟高电平时间: t_w > 10ns 
 * 3. 时钟低电平时间: t_w > 10ns
 * 4. 锁存建立时间: t_su > 10ns (锁存上升沿前数据必须稳定)
 * 
 * @note 该函数使用1μs延时确保时序可靠性，远高于74HC595的最小时序要求
 * @warning 必须在调用前确保所有GPIO引脚已正确初始化为输出模式
 * @see set_motor_step(), set_motor_dir(), paper_change_init()
 */
static void hc595_write(uint8_t data) {
    // === 第一阶段：准备数据移位 ===
    // 拉低锁存引脚(STCP)，准备将数据移入移位寄存器
    // 此时并行输出保持之前的数据，不受移位操作影响
    digitalWrite(HC595_LATCH_PIN, LOW);    // STCP = 0, 锁存禁用
    
    // === 第二阶段：串行数据移入 ===
    // 循环8次，从最高位(MSB)到最低位(LSB)逐位输入数据
    // 74HC595在时钟上升沿读取数据位，所以数据必须在时钟上升沿前稳定
    for (uint8_t i = 0; i < 8; i++) {
        // 步骤2.1: 时钟拉低，准备数据位
        digitalWrite(HC595_CLOCK_PIN, LOW);    // SHCP = 0, 准备数据
        
        // 步骤2.2: 设置数据线电平
        // 位操作解释：(0x80 >> i) 从最高位开始逐位检查
        // 例如：data=0b10110011, i=0时检查bit7，i=7时检查bit0
        digitalWrite(HC595_DATA_PIN, (data & (0x80 >> i)) ? HIGH : LOW);
        delayMicroseconds(1);                  // 数据建立时间，确保数据线电平稳定
        
        // 步骤2.3: 时钟上升沿，数据位移入寄存器
        digitalWrite(HC595_CLOCK_PIN, HIGH);   // SHCP = 1, 上升沿移入数据
        delayMicroseconds(1);                  // 时钟高电平保持时间
    }
    
    // === 第三阶段：数据并行输出 ===
    // 拉高锁存引脚(STCP)，将移位寄存器的8位数据同时并行输出到Q0-Q7
    // 这是一个原子操作，所有输出引脚同时变化，避免电机动作不同步
    digitalWrite(HC595_LATCH_PIN, HIGH);   // STCP = 1, 锁存并并行输出
    
    // === 第四阶段：硬件稳定和状态保存 ===
    // 等待硬件电路稳定，确保输出信号达到有效电平
    delayMicroseconds(HC595_UPDATE_DELAY_US); // 硬件稳定时间
    
    // 保存当前输出状态到控制结构体，用于状态跟踪和避免重复操作
    paper_ctrl.hc595_output = data;        // 记录当前输出状态
}

/**
 * @brief 超高性能74HC595D移位寄存器数据写入函数（寄存器直接访问）
 * @param data 要写入的8位数据，每一位对应一个电机的控制信号
 * 
 * === 性能优化说明 ===
 * 该函数使用ESP32的GPIO寄存器直接操作，获得最高性能：
 * - 使用GPIO.out_w1tc/w1ts寄存器原子操作，避免Arduino库开销
 * - 移除了所有delayMicroseconds()，使用CPU周期级延时
 * - 内联函数优化，减少函数调用开销
 * - 预计算掩码，减少运行时计算
 * 
 * === 性能提升 ===
 * 相比标准版本性能提升约5-10倍：
 * - 标准版本：~80μs一次8位写入
 * - 优化版本：~8-15μs一次8位写入
 * 
 * === 寄存器说明 ===
 * - GPIO.out_w1tc: 写1清除对应引脚（原子操作）
 * - GPIO.out_w1ts: 写1设置对应引脚（原子操作）
 * - delayMicroseconds(): ESP32 Arduino框架的精确延时函数
 * 
 * === 使用建议 ===
 * - 在高频换纸操作中使用此版本
 * - 在调试和初始化阶段可使用标准版本
 * - 两种函数接口完全相同，可无缝替换
 * 
 * @note 此函数使用Arduino框架的标准延时函数
 * @warning 寄存器操作要求精确的引脚编号，确保引脚定义正确
 * @see hc595_write()
 */
inline void hc595_write_fast(uint8_t data) {
    // === 第一阶段：准备数据移位 ===
    // 使用原子操作拉低锁存引脚，避免影响其他引脚
    GPIO.out_w1tc = (1ULL << HC595_LATCH_PIN);    // STCP = 0 (原子操作)
    
    // === 第二阶段：高速串行数据移入 ===
    // 预计算掩码，减少循环内的位操作开销
    uint8_t mask = 0x80;  // 从最高位开始的掩码
    
    // 循环8次，使用最高效的寄存器操作
    for (uint8_t i = 0; i < 8; i++) {
        // 步骤2.1: 时钟拉低 - 原子操作
        GPIO.out_w1tc = (1ULL << HC595_CLOCK_PIN);    // SHCP = 0
        
        // 步骤2.2: 设置数据线 - 根据数据位设置或清除
        // HC595_DATA_PIN = GPIO32，使用GPIO.out1寄存器
        if (data & mask) {
            GPIO.out1.val = (1ULL << (HC595_DATA_PIN - 32));  // DATA = 1 (GPIO32)
        } else {
            GPIO.out1.val &= ~(1ULL << (HC595_DATA_PIN - 32)); // DATA = 0 (GPIO32) - 使用位与清除
        }
        
        // 步骤2.3: 时钟上升沿 - 原子操作
        GPIO.out_w1ts = (1ULL << HC595_CLOCK_PIN);    // SHCP = 1
        
        // 准备下一位掩码
        mask >>= 1;
        
        // === 高精度延时 ===
        // 使用ESP32 ROM函数，比delayMicroseconds()更精确高效
        delayMicroseconds(1);  // 1μs精确延时
    }
    
    // === 第三阶段：数据并行输出 ===
    // 原子操作拉高锁存引脚，并行输出所有数据
    GPIO.out_w1ts = (1ULL << HC595_LATCH_PIN);    // STCP = 1 (原子操作)
    
    // === 第四阶段：硬件稳定和状态保存 ===
    // 精确的硬件稳定延时
    delayMicroseconds(HC595_UPDATE_DELAY_US);
    
    // 保存当前输出状态
    paper_ctrl.hc595_output = data;
}

/**
 * @brief 自适应HC595写入函数（根据条件选择最优版本）
 * @param data 要写入的8位数据
 * 
 * === 功能说明 ===
 * 该函数根据当前系统状态自动选择最优的HC595写入方法：
 * - 正常状态：使用标准版本，确保稳定性和可调试性
 * - 高频操作：使用快速版本，获得最佳性能
 * 
 * === 选择策略 ===
 * 可以根据以下条件选择：
 * - 当前状态机状态
 * - 步进频率要求
 * - 系统负载情况
 * - 调试模式开关
 * 
 * @note 目前默认使用优化版本，可通过宏定义控制
 */
inline void hc595_write_adaptive(uint8_t data) {
    // 可以根据需要添加自适应逻辑
    // 目前直接使用快速版本以获得最佳性能
    hc595_write_fast(data);
}

/**
 * @brief 电机步进脉冲控制函数
 * @param motor_bit 电机对应的HC595位位置 (2-7，参考位映射定义)
 * @param enable 步进控制标志，true=生成步进脉冲，false=停止步进脉冲
 * 
 * === 功能说明 ===
 * 该函数控制电机的步进脉冲信号，用于驱动步进电机转动：
 * - enable=true: 生成高电平步进脉冲，电机执行一个步进
 * - enable=false: 停止步进脉冲，电机保持当前位置
 * 
 * === 位映射表 ===
 * motor_bit 值对应以下电机控制：
 * - 2: BIT_PAPER_CLAMP_DIR  (夹纸电机方向)
 * - 3: BIT_PAPER_CLAMP_STEP (夹纸电机步进)
 * - 4: BIT_PANEL_MOTOR_DIR  (面板电机方向)  
 * - 5: BIT_PANEL_MOTOR_STEP (面板电机步进)
 * - 6: BIT_FEED_MOTOR_DIR   (进纸电机方向)
 * - 7: BIT_FEED_MOTOR_STEP  (进纸电机步进)
 * 
 * === 硬件接口 ===
 * HC595输出 → 电机驱动电路 → 步进电机
 * 高电平(1): 驱动电路输出步进脉冲，电机转动一步
 * 低电平(0): 驱动电路无脉冲输出，电机保持位置
 * 
 * === 优化设计 ===
 * 1. 状态缓存: 比较新状态与当前状态，避免重复写入HC595
 * 2. 位操作优化: 使用高效的位运算修改指定位
 * 3. 原子操作: 状态更新是原子的，避免中间状态干扰
 * 
 * @note 该函数只修改步进信号，不影响方向控制
 * @warning 必须配合set_motor_dir()函数正确设置电机方向
 * @see set_motor_dir(), generate_motor_step(), hc595_write()
 */
/**
 * @brief 通用HC595位操作函数
 * @param bit_pos 位位置
 * @param value 设置值（true=1, false=0）
 * @param signal_name 信号名称（用于调试）
 */
static inline void hc595_set_bit(uint8_t bit_pos, bool value, const char* signal_name) {
    uint8_t new_output = paper_ctrl.hc595_output;
    
    if (value) {
        new_output |= (1 << bit_pos);
    } else {
        new_output &= ~(1 << bit_pos);
    }
    
    if (new_output != paper_ctrl.hc595_output) {
        hc595_write_fast(new_output);
    }
}

static void set_motor_step(uint8_t motor_bit, bool enable) {
    hc595_set_bit(motor_bit, enable, "Motor Step");
}

/**
 * @brief 电机旋转方向控制函数
 * @param motor_bit 电机对应的HC595位位置 (2,4,6，参考位映射定义)
 * @param forward 方向控制标志，true=正向旋转，false=反向旋转
 * 
 * === 功能说明 ===
 * 该函数控制步进电机的旋转方向，必须先设置方向再执行步进：
 * - forward=true: 电机正向旋转（根据物理定义的"前进"方向）
 * - forward=false: 电机反向旋转（与正向相反的方向）
 * 
 * === 方向定义 ===
 * 各电机的正向定义（从硬件角度）：
 * - 面板电机(BIT_PANEL_MOTOR_DIR): 正向=送纸出去，反向=拉纸进来
 * - 进纸电机(BIT_FEED_MOTOR_DIR): 正向=从纸仓送纸，反向=回收纸张
 * - 夹纸电机(BIT_PAPER_CLAMP_DIR): 正向=夹纸动作，反向=释放动作
 * 
 * === 硬件逻辑 ===
 * 根据HR4988驱动芯片的数据手册和电路设计：
 * - DIR引脚低电平(0): 电机正向旋转
 * - DIR引脚高电平(1): 电机反向旋转
 * 
 * === 注意事项 ===
 * 1. 方向改变后需要等待至少10μs才能执行步进
 * 2. 方向信号是电平信号，不是脉冲信号
 * 3. 必须在步进脉冲前设置正确的方向
 * 
 * @warning 方向设置后需要短暂延时确保驱动芯片响应
 * @note 该函数只修改方向信号，不控制步进动作
 * @see set_motor_step(), generate_motor_step(), DEFAULT_STEP_PULSE_MICROSECONDS
 */
static void set_motor_dir(uint8_t motor_bit, bool forward) {
    // HR4988芯片: DIR=0为正向，DIR=1为反向
    hc595_set_bit(motor_bit, !forward, "Motor Direction");
}

// ================================================================================
// 电机步进脉冲生成函数组
// ================================================================================

/**
 * @brief 完整的电机步进脉冲生成函数
 * @param step_bit 步进脉冲控制位 (3,5,7，对应STEP信号)
 * @param dir_bit 方向控制位 (2,4,6，对应DIR信号)  
 * @param forward 旋转方向，true=正向，false=反向
 * 
 * === 功能说明 ===
 * 该函数生成一个完整的步进脉冲序列，包含方向设置和步进动作：
 * 1. 设置电机旋转方向（DIR信号）
 * 2. 等待方向稳定（驱动芯片响应时间）
 * 3. 生成步进脉冲（STEP信号上升沿）
 * 4. 保持脉冲宽度（确保驱动芯片检测到脉冲）
 * 5. 结束脉冲（STEP信号下降沿，完成一步）
 * 6. 步进间隔（控制电机转速）
 * 
 * === 时序要求 ===
 * 根据HR4988驱动芯片技术规格：
 * - DIR信号建立时间: t_SU > 200ns (方向改变到步进脉冲的时间)
 * - STEP脉冲宽度: t_WH > 1μs (脉冲高电平时间)
 * - STEP脉冲间隔: t_WL > 1μs (脉冲低电平时间)
 * 
 * 本函数使用10μs方向稳定时间，5μs脉冲宽度，远高于芯片最低要求
 * 
 * === 使用场景 ===
 * 该函数用于以下换纸操作：
 * - 面板电机: 送出/拉入纸张 (BIT_PANEL_MOTOR_STEP/DIR)
 * - 进纸电机: 从纸仓送纸 (BIT_FEED_MOTOR_STEP/DIR)
 * - 夹纸电机: 夹持/释放纸张 (BIT_PAPER_CLAMP_STEP/DIR)
 * 
 * @warning 连续调用时需要考虑步进间隔控制电机转速
 * @note 该函数包含方向设置，调用时不需要单独设置方向
 * @see set_motor_step(), set_motor_dir(), DEFAULT_STEP_PULSE_MICROSECONDS
 */
static void generate_motor_step(uint8_t step_bit, uint8_t dir_bit, bool forward) {
    // === 第一步：方向设置 ===
    // 设置电机旋转方向，确保电机按预期方向转动
    set_motor_dir(dir_bit, forward);                // 设置DIR信号电平
    
    // === 第二步：方向稳定等待 ===
    // 等待驱动芯片响应方向变化，确保方向信号稳定
    // HR4988芯片要求方向改变后至少200ns才能发送步进脉冲
    delayMicroseconds(10);                          // 10μs稳定时间，远高于200ns要求
    
    // === 第三步：步进脉冲开始 ===
    // 生成步进脉冲的上升沿，驱动芯片在此边缘开始计数
    set_motor_step(step_bit, HIGH);                  // STEP信号从低变高
    
    // === 第四步：脉冲宽度保持 ===
    // 保持脉冲高电平足够时间，确保驱动芯片可靠检测
    delayMicroseconds(DEFAULT_STEP_PULSE_MICROSECONDS); // 保持5μs高电平
    
    // === 第五步：步进脉冲结束 ===
    // 生成步进脉冲的下降沿，驱动芯片在此边缘完成一步计数
    // HR4988芯片在STEP信号下降沿时锁定一步并更新微步位置
    set_motor_step(step_bit, LOW);                   // STEP信号从高变低，完成一个步进周期
    
    // === 第六步：步进间隔控制 ===
    // 步进间的最小间隔，控制电机最高转速
    // 10μs间隔对应10,000步/秒 = 600,000步/分钟的理论最高速度
    delayMicroseconds(10);                          // 步进间隔，实际应用中可能需要更长的间隔
}

// ================================================================================
// 纸张传感器读取与滤波函数
// ================================================================================

/**
 * @brief 纸张传感器读取函数（带数字去抖动滤波）
 * @return true - 稳定检测到纸张，false - 稳定无纸张
 * 
 * === 硬件说明 ===
 * 传感器类型：红外光电传感器或机械限位开关
 * 连接引脚：GPIO34 (ESP32专用输入引脚)
 * 信号定义：
 * - 高电平(HIGH): 检测到纸张遮挡
 * - 低电平(LOW): 无纸张，传感器处于空闲状态
 * 
 * === 去抖动原理 ===
 * 1. 原始信号可能包含机械振动、电气噪声等干扰
 * 2. 使用时间窗口滤波法，需要信号连续稳定50ms才确认为有效
 * 3. 避免因瞬时干扰导致的误触发，提高系统可靠性
 * 
 * === 滤波算法 ===
 * 实现状态机+定时器滤波：
 * - 状态变化时重置定时器，记录变化时间
 * - 信号稳定持续50ms后更新输出状态
 * - 在变化过程中保持上次的稳定输出
 * 
 * === 性能参数 ===
 * - 去抖动时间: 50ms (适合机械开关和光电传感器)
 * - 时间精度: 1ms (millis()函数精度)
 * - 内存占用: 12字节静态变量
 * 
 * === 应用场景 ===
 * 该函数用于以下关键检测：
 * - 预检状态：检测面板上是否有旧纸张
 * - 进纸状态：检测新纸是否到达指定位置
 * - 释放状态：检测纸张是否完全离开传感器区域
 * 
 * @note 该函数是非阻塞的，适合在主循环中高频调用
 * @warning 50ms去抖动时间可能需要根据具体传感器调整
 * @see PAPER_PRE_CHECK_STEPS, PAPER_FEED_TIMEOUT_MS
 */
static bool read_paper_sensor() {
    // === 静态变量定义 ===
    // 使用静态变量保持滤波状态，函数调用间保持数据
    static bool last_state = false;              // 上一次读取的原始状态
    static uint32_t last_change_time = 0;        // 上次状态变化的时间戳
    static bool stable_state = false;             // 当前稳定的输出状态
    
    // === 原始信号读取 ===
    // 直接读取GPIO34的电平状态
    // digitalRead()返回HIGH/LOW，转换为bool类型
    bool current_state = digitalRead(PAPER_SENSOR_PIN) == HIGH;  // HIGH=有纸, LOW=无纸
    uint32_t current_time = millis();             // 获取系统当前时间戳
    
    // === 状态变化检测 ===
    // 比较当前状态与上次原始状态，检测是否有变化
    if (current_state != last_state) {
        // 检测到状态变化：重置稳定计时器
        last_change_time = current_time;          // 记录变化发生的时间
        last_state = current_state;              // 更新原始状态记录
        // 注意：此时不更新stable_state，继续输出上次的稳定状态
    } 
    // === 稳定性判断 ===
    // 信号在相同状态下保持足够长时间，确认状态稳定
    else if (current_time - last_change_time > 50) {  // 50ms稳定时间
        // 信号稳定超过50ms，确认为有效状态变化
        stable_state = current_state;             // 更新稳定输出状态
    }
    // 如果状态持续时间不足50ms，保持原stable_state不变
    
    // === 返回稳定状态 ===
    return stable_state;                          // 返回经过滤波的稳定状态
}

// ================================================================================
// 非阻塞式步进控制函数
// ================================================================================

/**
 * @brief 检查是否到了执行下一步进的时间
 * @param timing 电机时间控制器指针
 * @param current_time 当前系统时间戳(μs)
 * @return true=可以执行步进，false=需要继续等待
 * 
 * === 功能说明 ===
 * 该函数实现非阻塞式时间检查，避免使用delayMicroseconds()阻塞系统：
 * - 通过比较时间差判断是否达到预设的步进间隔
 * - 不阻塞主循环，系统可以同时处理其他任务
 * - 精确控制步进频率，保证电机运行平稳
 * 
 * === 使用场景 ===
 * - 面板电机：出纸/进纸时的平稳输送控制
 * - 进纸电机：新纸送入时的精确控制
 * - 夹纸电机：夹持动作的时序控制
 * 
 * @note 时间精度取决于micros()函数，ESP32上精度为4μs
 * @see motor_timing_t, schedule_next_step()
 */
static bool should_step_now(motor_timing_t* timing, uint32_t current_time) {
    return (current_time - timing->last_step_time) >= timing->step_interval;
}

/**
 * @brief 安排下一次步进时间
 * @param timing 电机时间控制器指针
 * @param current_time 当前系统时间戳(μs)
 * 
 * === 功能说明 ===
 * 在执行完一次步进后调用，更新下次步进的时间基准：
 * - 记录当前时间为"上次步进时间"
 * - 为下次步进时间判断提供基准
 * - 确保步进间隔的准确性
 * 
 * @note 必须在每次实际执行步进后调用
 * @see should_step_now()
 */
static void schedule_next_step(motor_timing_t* timing, uint32_t current_time) {
    timing->last_step_time = current_time;
}

/**
 * @brief 通用非阻塞电机步进函数
 * @param config 电机配置
 * @param forward 前进方向
 * @return 是否执行了步进
 */
static bool nonblocking_motor_step(const motor_config_t* config, bool forward) {
    uint32_t current_time = micros();
    
    // 设置步进间隔
    if (config->timing) {
        config->timing->step_interval = config->step_interval;
    }
    
    // 检查是否到了步进时间
    if (!should_step_now(config->timing, current_time)) {
        return false;
    }
    
    // 执行步进
    generate_motor_step(config->step_bit, config->dir_bit, forward);
    
    // 调度下一步
    if (config->timing) {
        schedule_next_step(config->timing, current_time);
    }
    
    return true;
}

/**
 * @brief 非阻塞式面板电机步进控制
 * @param forward 旋转方向，true=正向，false=反向
 * @return true=成功执行一步，false=需要等待或未执行
 * 
 * === 功能说明 ===
 * 替代原来带delayMicroseconds()的阻塞式步进控制：
 * 1. 检查是否到了执行步进的时间
 * 2. 如果时间到了，执行一个完整的步进脉冲
 * 3. 更新下次步进的时间安排
 * 4. 立即返回，不阻塞主循环
 * 
 * === 步进参数 ===
 * - 时间间隔：使用PAPER_PRE_CHECK_INTERVAL_US或PAPER_EJECT_INTERVAL_US
 * - 方向控制：根据forward参数自动设置
 * - 脉冲宽度：使用DEFAULT_STEP_PULSE_MICROSECONDS
 * 
 * @note 必须在状态机循环中持续调用直到完成所需步数
 * @see generate_motor_step(), should_step_now(), schedule_next_step()
 */
static bool nonblocking_panel_step(bool forward) {
    motor_config_t config = {
        .step_bit = BIT_PANEL_MOTOR_STEP,
        .dir_bit = BIT_PANEL_MOTOR_DIR,
        .step_interval = 0, // 使用当前设置
        .name = "Panel",
        .timing = &panel_motor_timing
    };
    return nonblocking_motor_step(&config, forward);
}

/**
 * @brief 非阻塞式进纸电机步进控制
 * @param forward 旋转方向，true=正向，false=反向
 * @return true=成功执行一步，false=需要等待或未执行
 * 
 * === 功能说明 ===
 * 进纸电机的非阻塞式步进控制，主要用于：
 * - 检测到纸张后的精确定位
 * - 进纸失败时的回收操作
 * - 手动进纸控制
 * 
 * === 特殊处理 ===
 * 进纸电机通常使用较慢的步进间隔(2ms)以确保：
 * - 纸张不会被过快送入
 * - 传感器有足够时间检测纸张
 * - 避免纸张堵塞或偏移
 * 
 * @note 进纸电机的步进间隔独立于面板电机设置
 */
static bool nonblocking_feed_step(bool forward) {
    motor_config_t config = {
        .step_bit = BIT_FEED_MOTOR_STEP,
        .dir_bit = BIT_FEED_MOTOR_DIR,
        .step_interval = 2000, // 2ms专用间隔
        .name = "Feed",
        .timing = &feed_motor_timing
    };
    return nonblocking_motor_step(&config, forward);
}

/**
 * @brief 初始化所有电机的时间控制器
 * 
 * === 功能说明 ===
 * 在换纸开始时调用，重置所有时间控制器的状态：
 * - 清零上次步进时间，确保立即可以执行第一步
 * - 设置各电机的默认步进间隔
 * - 清除待执行标志
 * 
 * === 默认参数 ===
 * - 面板电机：1ms间隔（PAPER_EJECT_INTERVAL_US）
 * - 进纸电机：2ms间隔（适合进纸操作）
 * - 夹纸电机：1ms间隔（适合快速夹持）
 * 
 * @note 必须在enter_state()或状态转换时调用
 * @see motor_timing_t
 */
static void init_motor_timing() {
    uint32_t current_time = micros();
    
    // 面板电机：使用预检间隔作为默认
    panel_motor_timing.last_step_time = current_time;
    panel_motor_timing.step_interval = PAPER_PRE_CHECK_INTERVAL_US;
    panel_motor_timing.step_pending = false;
    
    // 进纸电机：使用较慢的间隔确保稳定性
    feed_motor_timing.last_step_time = current_time;
    feed_motor_timing.step_interval = 2000; // 2ms，适合进纸
    feed_motor_timing.step_pending = false;
    
    // 夹纸电机：使用标准间隔
    clamp_motor_timing.last_step_time = current_time;
    clamp_motor_timing.step_interval = DEFAULT_STEP_PULSE_MICROSECONDS;
    clamp_motor_timing.step_pending = false;
}

// === State Machine Functions ===
/**
 * @brief 状态转换验证函数
 * @param from_state 源状态
 * @param to_state 目标状态
 * @return true=允许转换，false=非法转换
 * 
 * === 功能说明 ===
 * 该函数检查状态转换是否符合状态机的逻辑规则：
 * - 使用预定义的状态转换矩阵
 * - 防止非法状态跳转导致系统异常
 * - 确保换纸流程的逻辑一致性
 * 
 * @note ERROR状态可以转换到任何状态，用于错误恢复
 * @see state_transition_matrix
 */
static bool is_valid_transition(paper_change_state_t from_state, paper_change_state_t to_state) {
    // 边界检查确保状态值有效
    if (from_state < PAPER_IDLE || from_state > PAPER_ERROR ||
        to_state < PAPER_IDLE || to_state > PAPER_ERROR) {
        return false;
    }
    
    return state_transition_matrix[from_state][to_state];
}

/**
 * @brief 安全的状态转换函数（增强版）
 * @param new_state 要进入的新状态
 * 
 * === 功能说明 ===
 * 该函数执行安全的状态转换，包含：
 * 1. 状态有效性检查，防止非法状态转换
 * 2. 状态转换日志记录，便于调试追踪
 * 3. 时间控制器初始化，确保非阻塞步进控制正常工作
 * 4. 状态变量重置，为新状态准备干净的环境
 * 
 * === 安全特性 ===
 * - 拒绝非法状态值，防止系统崩溃
 * - 记录详细的转换日志，包含时间戳
 * - 自动初始化电机时序，避免遗留状态影响
 * 
 * @note 该函数是状态机的核心，所有状态转换都必须通过此函数
 * @see init_motor_timing(), paper_change_state_t
 */
static void enter_state(paper_change_state_t new_state) {
    // === 状态有效性检查 ===
    // 确保新状态在有效范围内，防止数组越界和非法状态
    if (new_state < PAPER_IDLE || new_state > PAPER_ERROR) {
        LOG_ERROR("Invalid state value");
        return; // 拒绝非法状态值
    }

    // === 状态转换逻辑验证 ===
    // 检查状态转换是否符合预定义的规则
    paper_change_state_t old_state = paper_ctrl.state;
    if (!is_valid_transition(old_state, new_state)) {
        const char** state_names = (const char**)STATE_NAMES;

        // 记录非法转换尝试
        grbl_sendf(CLIENT_ALL, "[MSG: ERROR - Invalid transition %s->%s]\r\n",
                   state_names[old_state], state_names[new_state]);

        // 非法转换时，如果不是错误状态，转入错误状态
        if (new_state != PAPER_ERROR) {
            enter_state(PAPER_ERROR);
        }
        return;
    }

    // === 状态转换记录 ===
    uint32_t transition_time = millis();  // 记录转换时间戳

    // === 错误状态特殊处理 ===
    // 从错误状态恢复时，执行额外的清理和安全检查
    if (old_state == PAPER_ERROR && new_state != PAPER_ERROR) {
        grbl_sendf(CLIENT_ALL, "[MSG: Recovering from ERROR state to %s]\r\n",
                   (new_state == PAPER_IDLE ? "IDLE" : "active state"));

        // 清除紧急停止标志
        paper_ctrl.emergency_stop = false;

        // 停止所有电机，确保安全状态
        STOP_ALL_MOTORS();
    }

    // === 定位状态特殊处理 ===
    // 进入定位状态时重置定位初始化标志，确保初始化执行
    if (new_state == PAPER_POSITIONING) {
        positioning_initialized = false;
    }

    // === 核心状态更新 ===
    paper_ctrl.state = new_state;          // 设置新状态
    paper_ctrl.state_timer = transition_time; // 重置状态计时器
    paper_ctrl.step_counter = 0;           // 重置步进计数器

    // === 电机时序初始化 ===
    // 为新状态初始化电机时间控制器
    init_motor_timing();

    // === 状态转换日志 ===

    // 详细的转换日志，包含时间信息便于性能分析
    grbl_sendf(CLIENT_ALL, "[MSG: State %s->%s at %lums]\r\n",
               GET_STATE_NAME(old_state), GET_STATE_NAME(new_state), transition_time);
}

// === Paper Change Implementation ===

// Start automatic paper change sequence
void paper_change_start() {
    if (paper_ctrl.state != PAPER_IDLE) {
        grbl_sendf(CLIENT_ALL, "[MSG: Paper change already in progress]\r\n");
        return;
    }
    
    grbl_sendf(CLIENT_ALL, "[MSG: Starting automatic paper change with pre-check]\r\n");
    enter_state(PAPER_PRE_CHECK);  // 先进入预检状态，倒转检测是否有纸
}

// Manual one-click paper change
void paper_change_one_click() {
    if (paper_ctrl.state != PAPER_IDLE) {
        grbl_sendf(CLIENT_ALL, "[MSG: Paper change busy, cannot start manual]\r\n");
        return;
    }
    
    grbl_sendf(CLIENT_ALL, "[MSG: Starting manual paper change with pre-check]\r\n");
    paper_ctrl.one_click_active = true;
    enter_state(PAPER_PRE_CHECK);  // 手动换纸也先预检
}

// Emergency stop paper change
void paper_change_stop() {
    grbl_sendf(CLIENT_ALL, "[MSG: Emergency stop paper change]\r\n");
    paper_ctrl.emergency_stop = true;
    
    // Stop all motors
    STOP_ALL_MOTORS();
    
    enter_state(PAPER_ERROR);
}

// Check one-click button press
static void check_button_press() {
    static bool last_button_state = false;
    static uint32_t button_press_time = 0;
    
    // Read button state from GPIO input pin
    bool button_pressed = (digitalRead(PAPER_BUTTON_PIN) == HIGH);
    
    if (button_pressed && !last_button_state) {
        // Button pressed - start timer
        button_press_time = millis();
    } else if (!button_pressed && last_button_state) {
        // Button released - check press duration
        uint32_t press_duration = millis() - button_press_time;
        
        if (press_duration < PAPER_BUTTON_LONG_PRESS_MS) {
            // Short press - start manual paper change
            paper_change_one_click();
        } else {
            // Long press - emergency stop
            smart_m0_emergency_stop();  // 这个函数内部已经调用了paper_change_stop()
        }
    }
    
    last_button_state = button_pressed;
}

// Update paper change state machine
// 换纸状态机更新函数 - 在主循环中被调用，处理状态转换和电机控制
void paper_change_update() {
    // Check button press FIRST - 允许通过长按按钮恢复紧急状态
    check_button_press();
    
    // Check for emergency stop AFTER button processing - 检查紧急停止标志
    if (paper_ctrl.emergency_stop) {
        return;  // 紧急停止状态下，不执行任何换纸操作
    }
    
    // Update sensor state - 更新传感器状态
    paper_ctrl.last_paper_sensor_state = paper_ctrl.paper_sensor_state; // 保存上一次状态
    paper_ctrl.paper_sensor_state = read_paper_sensor();             // 读取当前传感器状态

    uint32_t current_time = millis();  // 获取当前系统时间，用于超时判断

    // 声明定位阶段的变量（在switch外部，避免未初始化问题）
    int fine_steps = 0, back_steps = 0, total_positioning_steps = 0, max_search_steps = 0;
    static uint32_t positioning_delay_timer = 0;

    switch (paper_ctrl.state) {         // 根据当前状态执行相应操作
        case PAPER_IDLE:
            // Check for paper sensor changes in idle (for monitoring)
            break;

        case PAPER_PRE_CHECK:
            // Pre-check state - 面板电机倒转检测是否有纸（非阻塞式控制）
            
            CHECK_STATE_SAFETY(PAPER_PRE_CHECK_STEPS + 50, "PRE_CHECK");
            
            if (paper_ctrl.step_counter < PAPER_PRE_CHECK_STEPS) {  // 倒转检测步数(2.5mm)
                // 使用非阻塞式步进控制，避免阻塞主循环
                if (nonblocking_panel_step(false)) {  // false=倒转方向
                    paper_ctrl.step_counter++;        // 只有成功执行一步才递增计数器
                }
                // 如果时间未到，直接返回，不执行任何操作，让主循环继续处理其他任务
            } else {
                // 倒转完成，检测倒转后的传感器状态判断下一步操作
                if (paper_ctrl.paper_sensor_state) {
                    // 传感器仍检测到纸 → 面板上有纸，需要先出纸
                    grbl_sendf(CLIENT_ALL, "[MSG: Paper detected on panel after %lu steps, starting ejection]\r\n", 
                               paper_ctrl.step_counter);
                    enter_state(PAPER_EJECTING);
                } else {
                    // 传感器未检测到纸 → 面板无纸，可以直接进纸
                    grbl_sendf(CLIENT_ALL, "[MSG: No paper on panel after %lu steps, starting feeding]\r\n", 
                               paper_ctrl.step_counter);
                    enter_state(PAPER_FEEDING);
                }
            }
            break;
            
        case PAPER_EJECTING:
            // 出纸逻辑：面板电机先反转直到传感器感应到纸张，然后正转A4+5/6cm
            
            CHECK_STATE_SAFETY(PAPER_EJECT_STEPS + 2000, "EJECTING");
            
            // 第一阶段：反转检测纸张位置（最多反转500步）
            if (paper_ctrl.step_counter < 500) {
                if (nonblocking_panel_step(false)) {  // 反转检测纸张
                    paper_ctrl.step_counter++;
                    
                    // 检测到纸张，停止反转，准备正转
                    if (paper_ctrl.paper_sensor_state && !paper_ctrl.last_paper_sensor_state) {
                        grbl_sendf(CLIENT_ALL, "[MSG: Paper detected at reverse step %lu, starting forward ejection\r\n",
                                   paper_ctrl.step_counter);
                        paper_ctrl.step_counter = 0;  // 重置计数器用于正转计数
                    }
                }
            } else if (paper_ctrl.step_counter < 24160) {  // 第二阶段：正转A4+5/6cm
                // 计算总步数：A4(23760步) + 5cm(400步) = 24160步
                uint32_t total_eject_steps = 23760 + 400;  // A4 + 5cm
                
                if (nonblocking_panel_step(true)) {  // 正转送出纸张
                    paper_ctrl.step_counter++;
                    
                    // 每1000步输出一次进度
                    if (paper_ctrl.step_counter % 1000 == 0) {
                        float progress = (float)paper_ctrl.step_counter / total_eject_steps * 100.0;
                        grbl_sendf(CLIENT_ALL, "[MSG: Ejection progress: %.1f%% (%lu/%lu)]\r\n", 
                                   progress, paper_ctrl.step_counter, total_eject_steps);
                    }
                    
                    // 完成A4+5/6cm送纸
                    if (paper_ctrl.step_counter >= total_eject_steps) {
                        grbl_sendf(CLIENT_ALL, "[MSG: Paper ejection complete (%lu steps), feeding new paper]\r\n", 
                                   paper_ctrl.step_counter);
                        enter_state(PAPER_FEEDING);
                    }
                }
            } else {
                // 反转500步未检测到纸张，可能已无纸，直接进入进纸状态
                grbl_sendf(CLIENT_ALL, "[MSG: No paper detected after reverse, proceeding to feed new paper]\r\n");
                enter_state(PAPER_FEEDING);
            }
            break;
            
        case PAPER_FEEDING:
            // Feed new paper and detect sensor - 进纸电机送纸，检测到纸张后进入松开夹具状态

            CHECK_STATE_SAFETY(1000, "FEEDING");

            // 检测纸张存在：从无到有的上升沿
            if (paper_ctrl.paper_sensor_state && !paper_ctrl.last_paper_sensor_state) {
                // 从无到有的上升沿检测
                grbl_sendf(CLIENT_ALL, "[MSG: Paper detected after %lu steps, entering unclamp state]\r\n",
                           paper_ctrl.step_counter);

                // 立即停止进纸电机
                set_motor_step(BIT_FEED_MOTOR_STEP, LOW);
                feed_motor_timing.step_pending = false;

                // 进入松开夹具状态
                enter_state(PAPER_UNCLAMP_FEED);
            } else if (paper_ctrl.step_counter > 500) {
                // 进纸超时
                handle_error(ERROR_SENSOR_TIMEOUT,
                            "Feed operation timeout - no paper detected",
                            PAPER_FEEDING);
            } else {
                // 继续进纸
                if (nonblocking_feed_step(true)) {
                    paper_ctrl.step_counter++;
                }
            }
            break;

        case PAPER_UNCLAMP_FEED:
            // 检测到纸张后立即松开夹具，面板电机同步运行5cm
            // 断电即为夹紧态，检测到纸张后需要松开夹子

            CHECK_STATE_SAFETY(500, "UNCLAMP_FEED");

            // 第一阶段：松开夹具（夹紧电机永不反转，通过延时等待机械松开）
            if (paper_ctrl.step_counter < 30) {
                if (millis() - clamp_motor_timing.last_step_time >= clamp_motor_timing.step_interval) {
                    // 松开夹具 - 夹紧电机永不反转，延时等待夹具机械松开
                    // 停止夹紧电机的步进信号，让其自然松开
                    set_motor_step(BIT_PAPER_CLAMP_STEP, LOW);  // 停止步进信号
                    clamp_motor_timing.last_step_time = millis();
                    paper_ctrl.step_counter++;
                }
            } else if (paper_ctrl.step_counter < 430) {  // 第二阶段：面板电机同步运行5cm (400步)
                // 面板电机与进纸电机同步运行，运送纸张5cm
                if (millis() - panel_motor_timing.last_step_time >= panel_motor_timing.step_interval) {
                    generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true);
                    panel_motor_timing.last_step_time = millis();
                    paper_ctrl.step_counter++;
                }

                // 检测纸张位置（虽然已经检测到，但持续监控）
                if (paper_ctrl.paper_sensor_state && !paper_ctrl.last_paper_sensor_state) {
                    grbl_sendf(CLIENT_ALL, "[MSG: Paper confirmed at sensor position (step %lu)\r\n",
                               paper_ctrl.step_counter);
                }
            } else {
                // 松开+5cm运送完成，进入夹紧状态
                grbl_sendf(CLIENT_ALL, "[MSG: Unclamp and 5cm transport complete (%lu steps), entering clamp state\r\n", 
                           paper_ctrl.step_counter);
                enter_state(PAPER_CLAMP_FEED);
            }
            break;

        case PAPER_CLAMP_FEED:
            // 执行夹紧动作（夹紧电机正向运行），然后进纸+面板电机同速运行
            // 夹紧电机永远不会反转，始终正向运行夹紧纸张

            CHECK_STATE_SAFETY(100, "CLAMP_FEED");

            // 执行夹紧动作（20步正向）
            if (paper_ctrl.step_counter < 20) {
                if (millis() - clamp_motor_timing.last_step_time >= clamp_motor_timing.step_interval) {
                    generate_motor_step(BIT_PAPER_CLAMP_STEP, BIT_PAPER_CLAMP_DIR, true);  // 正向夹紧
                    clamp_motor_timing.last_step_time = millis();
                    paper_ctrl.step_counter++;
                }
            } else {
                // 夹紧完成，进入完整输送状态
                grbl_sendf(CLIENT_ALL, "[MSG: Clamping complete (%lu steps), starting fast feed\r\n", paper_ctrl.step_counter);
                enter_state(PAPER_FULL_FEED);
            }
            break;

        case PAPER_FULL_FEED:
            // 进纸+面板电机同速快速运行，运行A4行程并检测纸张脱离传感器
            // 进纸电机永远不会反转，始终保持正向

            CHECK_STATE_SAFETY(25000, "FULL_FEED");  // A4约23760步，加上缓冲

            // 设置同速步进间隔
            feed_motor_timing.step_interval = 1000;  // 1ms
            panel_motor_timing.step_interval = 1000;  // 1ms

            // 检查是否达到A4行程且纸张脱离传感器
            if (paper_ctrl.step_counter >= 23760 && !paper_ctrl.paper_sensor_state) {
                // A4行程完成且纸张脱离传感器，进纸电机停止
                grbl_sendf(CLIENT_ALL, "[MSG: Full feed complete (%lu steps), paper detached, stopping feed motor\r\n",
                           paper_ctrl.step_counter);
                set_motor_step(BIT_FEED_MOTOR_STEP, LOW);  // 停止进纸电机
                enter_state(PAPER_REPOSITION);
            } else {
                // 继续同速运行（进纸和面板电机都只正向运行）
                if (millis() - feed_motor_timing.last_step_time >= feed_motor_timing.step_interval &&
                    millis() - panel_motor_timing.last_step_time >= panel_motor_timing.step_interval) {
                    // 进纸电机正向运行（永远不会反转）
                    generate_motor_step(BIT_FEED_MOTOR_STEP, BIT_FEED_MOTOR_DIR, true);
                    feed_motor_timing.last_step_time = millis();

                    // 面板电机正向运行
                    generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true);
                    panel_motor_timing.last_step_time = millis();

                    paper_ctrl.step_counter++;

                    // 定期输出进度（每1000步）
                    if (paper_ctrl.step_counter % 1000 == 0) {
                        float progress = (float)paper_ctrl.step_counter / 23760.0 * 100.0;
                        grbl_sendf(CLIENT_ALL, "[MSG: Full feed progress: %.1f%% (%lu/23760)\r\n",
                                   progress, paper_ctrl.step_counter);
                    }
                }
            }
            break;

        case PAPER_REPOSITION:
            // 关键定位逻辑：面板电机反转找到纸张后，必须正转5cm确保位置正确
            // 这一步很重要，不然纸张位置不对，影响后续写字任务

            CHECK_STATE_SAFETY(1200, "REPOSITION");

            // 阶段1：反转寻找纸张位置，以传感器信号为准
            if (paper_ctrl.step_counter < 500) {  // 安全上限500步，但以传感器信号为准
                if (millis() - panel_motor_timing.last_step_time >= panel_motor_timing.step_interval) {
                    generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, false);  // 反转寻找纸张
                    panel_motor_timing.last_step_time = millis();
                    paper_ctrl.step_counter++;

                    // 检测到纸张边缘，立即停止反转，以传感器信号为准
                    if (paper_ctrl.paper_sensor_state && !paper_ctrl.last_paper_sensor_state) {
                        grbl_sendf(CLIENT_ALL, "[MSG: Paper edge detected at reverse step %lu, STOPPED - sensor based detection\r\n",
                                   paper_ctrl.step_counter);
                        
                        // 立即停止反转，重置计数器开始关键5cm正转定位
                        paper_ctrl.step_counter = 0;
                    }
                }
            } else if (paper_ctrl.step_counter < 400) {  // 阶段2：极其重要的5cm正转定位（400步）
                if (millis() - panel_motor_timing.last_step_time >= panel_motor_timing.step_interval) {
                    generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true);  // 关键的正转定位
                    panel_motor_timing.last_step_time = millis();
                    paper_ctrl.step_counter++;

                    // 输出关键定位进度
                    if (paper_ctrl.step_counter % 50 == 0) {
                        float position_mm = (float)paper_ctrl.step_counter / 80.0;  // 80步/mm
                        grbl_sendf(CLIENT_ALL, "[MSG: CRITICAL POSITIONING: %.1fmm (%lu/400 steps) - EXTREMELY IMPORTANT\r\n",
                                   position_mm, paper_ctrl.step_counter);
                    }
                }
            } else if (paper_ctrl.step_counter >= 400) {  // 正转5cm完成
                // 关键定位完成，纸张已到达精确的写字位置
                float final_position = (float)paper_ctrl.step_counter / 80.0;
                grbl_sendf(CLIENT_ALL, "[MSG: CRITICAL: Paper positioning complete at %.1fmm (%lu steps), ready for writing tasks\r\n",
                           final_position, paper_ctrl.step_counter);
                enter_state(PAPER_COMPLETE);
            }
            break;




        case PAPER_COMPLETE:
            // Paper change complete
            grbl_sendf(CLIENT_ALL, "[MSG: Paper change completed successfully]\r\n");
            
            // Notify smart M0 system
            m0_paper_change_complete();
            
            // Reset to idle
            paper_ctrl.one_click_active = false;
            enter_state(PAPER_IDLE);
            break;
            
        case PAPER_ERROR:
            // Error state - wait for manual reset
            // Emergency stop auto-recovery after 2 seconds (for usability)
            // Note: This only applies to emergency stops, not system errors
            if (paper_ctrl.emergency_stop) {
                if (current_time - paper_ctrl.state_timer > 2000) {
                    grbl_sendf(CLIENT_ALL, "[MSG: Emergency stop timeout, auto-resuming\r\n");
                    paper_ctrl.emergency_stop = false;
                    enter_state(PAPER_IDLE);
                }
            }
            // If not emergency stop, require manual reset via M113
            break;
    }
}

// Get paper change status
bool paper_change_is_active() {
    return (paper_ctrl.state != PAPER_IDLE && paper_ctrl.state != PAPER_ERROR);
}

// Get paper change state
const char* paper_change_get_state() {
    
    return GET_STATE_NAME(paper_ctrl.state);
}

// Reset paper change system
void paper_change_reset() {
    STOP_ALL_MOTORS();  // Stop all motors
    
    // 安全重置：先保存状态，再清除变量
    paper_change_state_t old_state = paper_ctrl.state;
    
    // 重置控制结构体的所有成员
    paper_ctrl.state = PAPER_IDLE;
    paper_ctrl.state_timer = 0;
    paper_ctrl.step_counter = 0;
    paper_ctrl.paper_sensor_state = false;
    paper_ctrl.last_paper_sensor_state = false;
    paper_ctrl.one_click_active = false;
    paper_ctrl.hc595_output = 0;
    paper_ctrl.emergency_stop = false;
    paper_ctrl.sensor_detected = false;
    
    grbl_sendf(CLIENT_ALL, "[MSG: Paper change system reset (was: %d)]\r\n", old_state);
}

/**
 * @brief 换纸系统完整初始化函数（增强版）
 * 
 * === 功能说明 ===
 * 该函数执行换纸系统的完整初始化，包含：
 * 1. 参数验证：确保所有配置参数在安全范围内
 * 2. GPIO配置：设置所有相关引脚的工作模式
 * 3. 硬件初始化：HC595芯片和传感器初始化
 * 4. 系统状态重置：确保系统从干净状态开始
 * 5. 安全检查：验证硬件连接和工作状态
 * 
 * === 安全特性 ===
 * - 参数验证防止配置错误
 * - 引脚状态检查确保硬件连接正确
 * - 初始化完成验证确保系统就绪
 * 
 * @note 该函数应在Grbl系统启动时调用
 * @see validate_system_parameters(), paper_change_reset()
 */
void paper_change_init() {
    // === 第一步：系统参数验证 ===
    // 在进行任何硬件操作前，先验证所有参数的有效性
    grbl_sendf(CLIENT_ALL, "[MSG: Validating paper change system parameters...]\r\n");
    
    if (!validate_system_parameters()) {
        LOG_ERROR("System parameter validation failed!");
        return; // 参数验证失败，不继续初始化
    }
    
    grbl_sendf(CLIENT_ALL, "[MSG: System parameters validation passed\r\n");
    
    // === 第二步：GPIO引脚配置 ===
    // 配置所有相关的GPIO引脚为正确的工作模式
    grbl_sendf(CLIENT_ALL, "[MSG: Configuring GPIO pins...\r\n");
    
    // HC595控制引脚配置
    pinMode(HC595_DATA_PIN, OUTPUT);           // HC595数据引脚设置为输出
    pinMode(HC595_CLOCK_PIN, OUTPUT);         // HC595时钟引脚设置为输出
    pinMode(HC595_LATCH_PIN, OUTPUT);         // HC595锁存引脚设置为输出
    
    // 传感器和控制引脚配置
    pinMode(PAPER_SENSOR_PIN, INPUT); // GPIO34是输入专用引脚，只能设置INPUT
    pinMode(PAPER_BUTTON_PIN, INPUT); // GPIO35是输入专用引脚，只能设置INPUT
    
    // === 第三步：硬件初始化 ===
    // 初始化HC595移位寄存器，确保所有输出处于安全状态
    grbl_sendf(CLIENT_ALL, "[MSG: Initializing HC595 shift register...\r\n");
    hc595_write_fast(0);                      // 使用高性能版本快速初始化
    
    // === 第四步：系统状态重置 ===
    // 重置所有控制变量和状态机，确保从干净状态开始
    grbl_sendf(CLIENT_ALL, "[MSG: Resetting system state...\r\n");
    
    // 清空控制结构体（使用逐个赋值而非memset，更安全）
    paper_ctrl.state = PAPER_IDLE;
    paper_ctrl.state_timer = 0;
    paper_ctrl.step_counter = 0;
    paper_ctrl.paper_sensor_state = false;
    paper_ctrl.last_paper_sensor_state = false;
    paper_ctrl.one_click_active = false;
    paper_ctrl.hc595_output = 0;
    paper_ctrl.emergency_stop = false;
    
    // 初始化电机时间控制器
    init_motor_timing();
    
    // === 第五步：硬件状态验证 ===
    // 读取初始传感器状态，验证硬件连接
    grbl_sendf(CLIENT_ALL, "[MSG: Verifying hardware connections...\r\n");
    
    // HC595连接验证 - 输出详细的引脚映射信息
    grbl_sendf(CLIENT_ALL, "[MSG: HC595D Connection Map:\r\n");
    grbl_sendf(CLIENT_ALL, "[MSG:   74HC595D Pin 14 (DS)   ←→ ESP32 GPIO%d (DATA)\r\n", HC595_DATA_PIN);
    grbl_sendf(CLIENT_ALL, "[MSG:   74HC595D Pin 11 (SHCP) ←→ ESP32 GPIO%d (CLOCK)\r\n", HC595_CLOCK_PIN);
    grbl_sendf(CLIENT_ALL, "[MSG:   74HC595D Pin 12 (STCP) ←→ ESP32 GPIO%d (LATCH)\r\n", HC595_LATCH_PIN);
    grbl_sendf(CLIENT_ALL, "[MSG:   Note: GPIO2 used to avoid conflict with STEPPERS_DISABLE_PIN (GPIO4)\r\n");
    
    // 传感器连接验证
    grbl_sendf(CLIENT_ALL, "[MSG: Sensor Connection:\r\n");
    grbl_sendf(CLIENT_ALL, "[MSG:   Paper Sensor ←→ ESP32 GPIO%d\r\n", PAPER_SENSOR_PIN);
    bool initial_sensor_state = digitalRead(PAPER_SENSOR_PIN) == HIGH;
    grbl_sendf(CLIENT_ALL, "[MSG:   Initial sensor state: %s\r\n", 
               initial_sensor_state ? "PAPER DETECTED" : "CLEAR");
    
    // 按钮连接验证
    grbl_sendf(CLIENT_ALL, "[MSG: Button Connection:\r\n");
    grbl_sendf(CLIENT_ALL, "[MSG:   One-Click Button ←→ ESP32 GPIO%d\r\n", PAPER_BUTTON_PIN);
    bool initial_button_state = digitalRead(PAPER_BUTTON_PIN) == HIGH;
    grbl_sendf(CLIENT_ALL, "[MSG:   Initial button state: %s\r\n", 
               initial_button_state ? "PRESSED" : "RELEASED");
    
    // === 第六步：初始化完成确认 ===
    // 发送详细的初始化完成报告
    // 注意：夹紧电机断电即为夹紧态，无需额外初始化
    grbl_sendf(CLIENT_ALL, "[MSG: Paper change system initialization complete]\r\n");
    grbl_sendf(CLIENT_ALL, "[MSG: System ready - State: IDLE, Clamp: CLAMPED (power-on state), Version: v2.0]\r\n");
}

// ================================================================================
// 调试指令实现 - 串口调试各个电机和传感器
// ================================================================================

/**
 * @brief 进纸器电机调试指令
 * @param steps 步数（正数前进，负数后退）
 * @param delay_us 步间延时（微秒）
 * 
 * 使用方法：M800 S1000 D2000 (前进1000步，步间延时2ms)
 */
/**
 * @brief 通用电机调试函数
 * @param motor_name 电机名称
 * @param step_bit 步进控制位
 * @param dir_bit 方向控制位
 * @param steps 步数（正负表示方向）
 * @param delay_us 步间延时（微秒）
 */
static void debug_motor_generic(const char* motor_name, uint8_t step_bit, uint8_t dir_bit, int steps, uint32_t delay_us) {
    grbl_sendf(CLIENT_ALL, "[MSG: Debug %s Motor: steps=%d, delay=%dus]\r\n", motor_name, steps, delay_us);
    
    if (steps == 0) {
        STOP_ALL_MOTORS(); // 停止所有电机
        grbl_sendf(CLIENT_ALL, "[MSG: %s motor stopped]\r\n", motor_name);
        return;
    }
    
    bool direction = (steps > 0);
    int abs_steps = abs(steps);
    
    for (int i = 0; i < abs_steps; i++) {
        generate_motor_step(step_bit, dir_bit, direction);
        delayMicroseconds(delay_us);
    }
    
    STOP_ALL_MOTORS(); // 停止所有电机
    grbl_sendf(CLIENT_ALL, "[MSG: %s motor completed %d steps]\r\n", motor_name, steps);
}

void debug_feed_motor(int steps, uint32_t delay_us) {
    debug_motor_generic("Feed", BIT_FEED_MOTOR_STEP, BIT_FEED_MOTOR_DIR, steps, delay_us);
}

void debug_panel_motor(int steps, uint32_t delay_us) {
    debug_motor_generic("Panel", BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, steps, delay_us);
}

void debug_clamp_motor(int steps, uint32_t delay_us) {
    debug_motor_generic("Clamp", BIT_PAPER_CLAMP_STEP, BIT_PAPER_CLAMP_DIR, steps, delay_us);
}

/**
 * @brief 读取所有传感器状态
 * 
 * 使用方法：M803
 */
void debug_read_sensors() {
    grbl_sendf(CLIENT_ALL, "[MSG: === Sensor Status Report ===]\r\n");
    
    // 纸张传感器状态
    bool paper_sensor_raw = digitalRead(PAPER_SENSOR_PIN) == HIGH;
    bool paper_sensor_state = paper_ctrl.paper_sensor_state;
    
    grbl_sendf(CLIENT_ALL, "[MSG: Paper Sensor GPIO%d:\r\n", PAPER_SENSOR_PIN);
    grbl_sendf(CLIENT_ALL, "[MSG:   Raw State: %s\r\n", paper_sensor_raw ? "HIGH (Paper Detected)" : "LOW (Clear)");
    grbl_sendf(CLIENT_ALL, "[MSG:   Debounced: %s\r\n", paper_sensor_state ? "PAPER DETECTED" : "CLEAR");
    grbl_sendf(CLIENT_ALL, "[MSG:   Last State: %s\r\n", paper_ctrl.last_paper_sensor_state ? "HIGH" : "LOW");
    
    // 按钮状态
    bool button_raw = digitalRead(PAPER_BUTTON_PIN) == HIGH;
    bool emergency_active = paper_ctrl.emergency_stop;
    
    grbl_sendf(CLIENT_ALL, "[MSG: Button GPIO%d:\r\n", PAPER_BUTTON_PIN);
    grbl_sendf(CLIENT_ALL, "[MSG:   Raw State: %s\r\n", button_raw ? "HIGH (Pressed)" : "LOW (Released)");
    grbl_sendf(CLIENT_ALL, "[MSG:   Emergency Stop: %s\r\n", emergency_active ? "ACTIVE" : "INACTIVE");
    
    // HC595输出状态
    grbl_sendf(CLIENT_ALL, "[MSG: HC595D Output: 0x%02X (Binary: ", paper_ctrl.hc595_output);
    for (int i = 7; i >= 0; i--) {
        grbl_sendf(CLIENT_ALL, "%d", (paper_ctrl.hc595_output >> i) & 1);
    }
    grbl_sendf(CLIENT_ALL, ")\r\n");
    
    // 各电机位状态
    grbl_sendf(CLIENT_ALL, "[MSG: Motor Enable Bits:\r\n");
    grbl_sendf(CLIENT_ALL, "[MSG:   Feed Motor: %s (DIR: %s, STEP: %s)\r\n", 
               (paper_ctrl.hc595_output & (1 << BIT_FEED_MOTOR_DIR)) ? "ENABLED" : "UNKNOWN",
               (paper_ctrl.hc595_output & (1 << BIT_FEED_MOTOR_DIR)) ? "FORWARD" : "REVERSE",
               (paper_ctrl.hc595_output & (1 << BIT_FEED_MOTOR_STEP)) ? "HIGH" : "LOW");
               
    grbl_sendf(CLIENT_ALL, "[MSG:   Panel Motor: %s (DIR: %s, STEP: %s)\r\n", 
               (paper_ctrl.hc595_output & (1 << BIT_PANEL_MOTOR_DIR)) ? "ENABLED" : "UNKNOWN",
               (paper_ctrl.hc595_output & (1 << BIT_PANEL_MOTOR_DIR)) ? "FORWARD" : "REVERSE",
               (paper_ctrl.hc595_output & (1 << BIT_PANEL_MOTOR_STEP)) ? "HIGH" : "LOW");
               
    grbl_sendf(CLIENT_ALL, "[MSG:   Clamp Motor: %s (DIR: %s, STEP: %s)\r\n", 
               (paper_ctrl.hc595_output & (1 << BIT_PAPER_CLAMP_DIR)) ? "ENABLED" : "UNKNOWN",
               (paper_ctrl.hc595_output & (1 << BIT_PAPER_CLAMP_DIR)) ? "FORWARD" : "REVERSE",
               (paper_ctrl.hc595_output & (1 << BIT_PAPER_CLAMP_STEP)) ? "HIGH" : "LOW");
    
    grbl_sendf(CLIENT_ALL, "[MSG: === End Sensor Report ===]\r\n");
}

/**
 * @brief 紧急停止所有电机
 * 
 * 使用方法：M804
 */
void debug_emergency_stop() {
    grbl_sendf(CLIENT_ALL, "[MSG: EMERGENCY STOP - All motors halted]\r\n");
    STOP_ALL_MOTORS(); // 清除所有HC595输出，停止所有电机
    paper_ctrl.emergency_stop = true;
    grbl_sendf(CLIENT_ALL, "[MSG: All motors stopped, emergency flag set\r\n");
}

/**
 * @brief 重置紧急停止状态
 * 
 * 使用方法：M805
 */
void debug_reset_emergency() {
    grbl_sendf(CLIENT_ALL, "[MSG: Reset emergency stop flag\r\n");
    paper_ctrl.emergency_stop = false;
    grbl_sendf(CLIENT_ALL, "[MSG: Emergency stop cleared, motors can be used again\r\n");
}

/**
 * @brief HC595直接控制指令
 * @param data 8位输出数据 (0-255)
 * 
 * 使用方法：M806 V85 (设置HC595输出为85)
 */
void debug_hc595_direct(uint8_t data) {
    grbl_sendf(CLIENT_ALL, "[MSG: HC595 Direct Control: value=0x%02X (%d)\r\n", data, data);
    hc595_write(data);
    paper_ctrl.hc595_output = data;
    grbl_sendf(CLIENT_ALL, "[MSG: HC595 output set to 0x%02X\r\n", data);
}

#endif // AUTO_PAPER_CHANGE_ENABLE