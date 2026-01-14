/*
  PaperChangeUtils.h - 换纸系统工具函数
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#pragma once

#ifdef AUTO_PAPER_CHANGE_ENABLE

#include "Grbl.h"
#include "PaperChangeConfig.h"
#include "PaperChangeTypes.h"

// ================================================================================
// 通用宏定义 - 简化常用操作
// ================================================================================

// 日志输出宏 - 修正多参数问题
#define LOG_MSG(msg) grbl_sendf(CLIENT_ALL, "[MSG: %s]\r\n", msg)
#define LOG_ERROR(msg) grbl_sendf(CLIENT_ALL, "[MSG: ERROR - %s]\r\n", msg)
#define LOG_MSG_F(format, ...) grbl_sendf(CLIENT_ALL, "[MSG: " format "]\r\n", ##__VA_ARGS__)
#define LOG_ERROR_F(format, ...) grbl_sendf(CLIENT_ALL, "[MSG: ERROR - " format "]\r\n", ##__VA_ARGS__)
#define LOG_PROGRESS(format, ...) grbl_sendf(CLIENT_ALL, "[MSG: " format "]\r\n", ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) do { \
    if (PAPER_DEBUG_ENABLED) { \
        grbl_sendf(CLIENT_ALL, "[DEBUG: " format "]\r\n", ##__VA_ARGS__); \
    } \
} while(0)

// 安全检查宏 - 修正变量作用域问题
#define CHECK_STATE_SAFETY(max_steps, state_name) \
    do { \
        paper_change_ctrl_t* ctrl = get_paper_control(); \
        if (!runtime_safety_check(ctrl ? ctrl->step_counter : 0, (max_steps), (state_name))) { \
            return; \
        } \
    } while(0)

// 电机控制宏 - 快速操作
#define STOP_ALL_MOTORS() stop_all_motors()
#define GET_STATE_NAME(state) get_state_name_string(state)

// 参数验证宏 - 边界检查
#define VALIDATE_STEPS_PER_MM(value)     ((value) > 0.0 && (value) <= 10000.0)
#define VALIDATE_MAX_RATE(value)          ((value) > 0.0 && (value) <= 50000.0)
#define VALIDATE_ACCELERATION(value)      ((value) > 0.0 && (value) <= 10000.0)
#define VALIDATE_STEPS_COUNT(value)       ((value) >= 0 && (value) <= 100000)
#define VALIDATE_TIMEOUT_MS(value)        ((value) > 0 && (value) <= 60000)
#define VALIDATE_GPIO_PIN(pin)            ((pin) >= 0 && (pin) < 40)

// ================================================================================
// 时间管理工具
// ================================================================================

/**
 * @brief 检查是否到了执行步进的时间
 * @param timing 电机时间控制器
 * @param current_time 当前时间
 * @return true=可以执行步进
 */
bool should_step_now(motor_timing_t* timing, uint32_t current_time);

/**
 * @brief 安排下一次步进时间
 * @param timing 电机时间控制器
 * @param current_time 当前时间
 */
void schedule_next_step(motor_timing_t* timing, uint32_t current_time);

/**
 * @brief 初始化电机时间控制器
 */
void init_motor_timing();

// ================================================================================
// 传感器处理工具
// ================================================================================

/**
 * @brief 通用传感器检测函数
 * @param check_edge 是否检查边沿
 * @param min_steps 最小步数
 * @return 传感器检测结果
 */
sensor_result_t check_paper_sensor(bool check_edge, uint32_t min_steps);

// ================================================================================
// 电机控制工具
// ================================================================================

/**
 * @brief 非阻塞式电机步进
 * @param config 电机配置
 * @param forward 前进方向
 * @return 是否执行了步进
 */
bool nonblocking_motor_step(const motor_config_t* config, bool forward);

/**
 * @brief 面板电机非阻塞步进
 * @param forward 前进方向
 * @return 是否执行了步进
 */
bool nonblocking_panel_step(bool forward);

/**
 * @brief 进纸电机非阻塞步进
 * @param forward 前进方向
 * @return 是否执行了步进
 */
bool nonblocking_feed_step(bool forward);

/**
 * @brief 夹紧电机非阻塞步进
 * @param forward 前进方向（true=正转松开，false=反转夹紧）
 * @return 是否执行了步进
 */
bool nonblocking_clamp_step(bool forward);

// ================================================================================
// 数据转换工具
// ================================================================================

/**
 * @brief 步数转换为毫米
 * @param steps 步数
 * @param steps_per_mm 每毫米步数
 * @return 毫米数
 */
float steps_to_mm(uint32_t steps, float steps_per_mm);

/**
 * @brief 毫米转换为步数
 * @param mm 毫米数
 * @param steps_per_mm 每毫米步数
 * @return 步数
 */
uint32_t mm_to_steps(float mm, float steps_per_mm);

/**
 * @brief 获取错误类型名称
 * @param error_type 错误类型
 * @return 错误类型名称字符串
 */
const char* get_error_type_name(paper_error_type_t error_type);

/**
 * @brief 获取恢复策略名称
 * @param strategy 恢复策略
 * @return 恢复策略名称字符串
 */
const char* get_recovery_strategy_name(recovery_strategy_t strategy);

/**
 * @brief 获取状态名称字符串
 * @param state 状态枚举
 * @return 状态名称字符串
 */
const char* get_state_name_string(paper_change_state_t state);

// ================================================================================
// 前置声明 - 避免循环依赖
// ================================================================================

// 从主模块获取控制结构的函数
paper_change_ctrl_t* get_paper_control();
error_info_t* get_error_info();
motor_timing_t* get_panel_motor_timing();
motor_timing_t* get_feed_motor_timing();
motor_timing_t* get_clamp_motor_timing();
void get_static_flags(bool* positioning_init, bool* eject_detected, bool* reverse_complete);
void set_static_flags(bool positioning_init, bool eject_detected, bool reverse_complete);

#endif // AUTO_PAPER_CHANGE_ENABLE