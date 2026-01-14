/*
  PaperChangeTypes.h - 换纸系统数据类型定义
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#pragma once

#ifdef AUTO_PAPER_CHANGE_ENABLE

#include "Grbl.h"

// ================================================================================
// 状态机定义 - 系统核心状态
// ================================================================================
typedef enum {
    PAPER_IDLE,              // 空闲状态 - 系统待机
    PAPER_PRE_CHECK,         // 预检状态 - 检测面板是否有纸
    PAPER_EJECTING,          // 出纸状态 - 送出已写完的纸张
    PAPER_FEEDING,           // 进纸状态 - 送入新纸张
    PAPER_UNCLAMP_FEED,      // 松开进纸状态 - 松开夹具并送纸5cm
    PAPER_CLAMP_FEED,        // 夹紧进纸状态 - 夹紧纸张
    PAPER_FULL_FEED,         // 完整进给状态 - A4行程进给
    PAPER_REPOSITION,        // 重新定位状态 - 精确定位到写字位置
    PAPER_COMPLETE,          // 完成状态 - 换纸完成
    PAPER_ERROR             // 错误状态 - 系统异常
} paper_change_state_t;

// ================================================================================
// 故障处理定义 - 错误类型和恢复策略
// ================================================================================
typedef enum {
    ERROR_NONE = 0,              // 无错误
    ERROR_SENSOR_TIMEOUT,       // 传感器超时
    ERROR_MOTOR_STALL,          // 电机堵转
    ERROR_STATE_TIMEOUT,        // 状态超时
    ERROR_PARAMETER_INVALID,    // 参数无效
    ERROR_HARDWARE_FAULT,       // 硬件故障
    ERROR_COMMUNICATION,        // 通信错误
    ERROR_EMERGENCY_STOP,       // 紧急停止
    ERROR_UNKNOWN               // 未知错误
} paper_error_type_t;

typedef enum {
    RECOVERY_NONE = 0,          // 无恢复操作
    RECOVERY_RETRY,             // 重试操作
    RECOVERY_RESET_MOTORS,      // 重置电机
    RECOVERY_FULL_RESTART,      // 完全重启
    RECOVERY_SAFE_SHUTDOWN,     // 安全关机
    RECOVERY_MANUAL_INTERVENTION // 需要人工干预
} recovery_strategy_t;

// ================================================================================
// 传感器数据结构
// ================================================================================
typedef struct {
    bool detected;           // 是否检测到纸张
    bool edge_detected;      // 是否检测到边沿变化
    uint32_t step_count;     // 当前步数
} sensor_result_t;

// ================================================================================
// 故障信息结构
// ================================================================================
typedef struct {
    paper_error_type_t error_type;          // 错误类型
    uint32_t error_time;                    // 错误发生时间
    paper_change_state_t error_state;       // 错误发生时的状态
    uint32_t step_count_at_error;           // 错误时的步数计数
    char error_description[64];             // 错误描述
    uint8_t retry_count;                    // 重试次数
    bool auto_recovery_enabled;             // 是否启用自动恢复
} error_info_t;

// ================================================================================
// 电机控制结构
// ================================================================================
typedef struct {
    uint32_t last_step_time;              // 上次步进时间戳
    uint32_t step_interval;               // 步进间隔时间(μs)
    bool step_pending;                    // 是否有待执行的步进
} motor_timing_t;

typedef struct {
    uint8_t step_bit;        // 步进控制位
    uint8_t dir_bit;         // 方向控制位
    uint32_t step_interval;  // 步进间隔
    const char* name;         // 电机名称
    motor_timing_t* timing;   // 时间控制器
} motor_config_t;

// ================================================================================
// 系统控制结构 - 全局状态管理
// ================================================================================
typedef struct {
    // === 核心状态信息 ===
    paper_change_state_t state;              // 当前状态机状态
    uint32_t state_timer;                    // 状态进入时间戳（毫秒）
    uint32_t step_counter;                   // 当前状态下的步数计数器
    bool paper_sensor_state;                 // 当前纸张传感器状态
    bool last_paper_sensor_state;            // 上一次传感器状态
    
    // === 控制标志 ===
    bool one_click_active;                   // 一键换纸激活标志
    uint8_t hc595_output;                   // 当前HC595芯片的8位输出状态
    bool emergency_stop;                    // 紧急停止标志
    bool sensor_detected;                   // 传感器检测标志
} paper_change_ctrl_t;

// ================================================================================
// 状态转换矩阵 - 确保状态机逻辑正确
// ================================================================================
extern const bool state_transition_matrix[PAPER_ERROR + 1][PAPER_ERROR + 1];

// ================================================================================
// 状态名称映射 - 用于调试和日志
// ================================================================================
extern const char* STATE_NAMES[];

#endif // AUTO_PAPER_CHANGE_ENABLE