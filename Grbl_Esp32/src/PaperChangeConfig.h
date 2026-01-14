/*
  PaperChangeConfig.h - 换纸系统配置参数
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#pragma once

#ifdef AUTO_PAPER_CHANGE_ENABLE

#include "Grbl.h"

// ================================================================================
// 换纸系统配置 - 避免与机器配置冲突
// ================================================================================

// 检查是否已定义，避免重定义冲突
#ifndef PAPER_SENSOR_PIN
#define PAPER_SENSOR_PIN       34    // 纸张传感器引脚（使用现有定义）
#endif

#ifndef PAPER_BUTTON_PIN  
#define PAPER_BUTTON_PIN        35    // 一键换纸按钮引脚（使用现有定义）
#endif

#ifndef HC595_DATA_PIN
#define HC595_DATA_PIN          32    // HC595数据引脚（使用现有定义）
#endif

#ifndef HC595_CLOCK_PIN
#define HC595_CLOCK_PIN         3     // HC595时钟引脚（使用现有定义）
#endif

#ifndef HC595_LATCH_PIN
#define HC595_LATCH_PIN         2     // HC595锁存引脚（使用现有定义）
#endif

// ================================================================================
// HC595位映射定义 - 电机控制位分配
// ================================================================================
#define BIT_PAPER_CLAMP_DIR     2     // 夹纸电机方向控制位
#define BIT_PAPER_CLAMP_STEP    3     // 夹纸电机步进控制位
#define BIT_PANEL_MOTOR_DIR      4     // 面板电机方向控制位
#define BIT_PANEL_MOTOR_STEP     5     // 面板电机步进控制位
#define BIT_FEED_MOTOR_DIR       6     // 进纸电机方向控制位
#define BIT_FEED_MOTOR_STEP      7     // 进纸电机步进控制位

// ================================================================================
// 机械参数配置 - 易于调整的运动参数
// ================================================================================
// 步进电机参数（避免重定义）
#ifndef DEFAULT_STEPS_PER_MM
#define DEFAULT_STEPS_PER_MM    80.0  // 默认步进分辨率
#endif

#ifndef DEFAULT_FEED_STEPS_PER_MM
#define DEFAULT_FEED_STEPS_PER_MM   80.0  // 进纸电机步数/毫米
#endif

#ifndef DEFAULT_PANEL_STEPS_PER_MM
#define DEFAULT_PANEL_STEPS_PER_MM  80.0  // 面板电机步数/毫米
#endif

// 速度参数（避免重定义）
#ifndef DEFAULT_FEED_MAX_RATE
#define DEFAULT_FEED_MAX_RATE      2000.0  // 进纸电机最大速度
#endif

#ifndef DEFAULT_PANEL_MAX_RATE
#define DEFAULT_PANEL_MAX_RATE     3000.0  // 面板电机最大速度
#endif

// 运动距离参数（精确控制的关键参数）
#define PAPER_A4_LENGTH_MM      297.0  // A4纸张长度（毫米）
#define PAPER_5CM_LENGTH_MM      50.0   // 5厘米距离（毫米）
#define PAPER_PRE_CHECK_MM       2.5    // 预检距离（毫米）

// 步数计算（避免重定义）
#ifndef PAPER_EJECT_STEPS
#define PAPER_EJECT_STEPS        (uint32_t)(PAPER_A4_LENGTH_MM * DEFAULT_PANEL_STEPS_PER_MM)
#endif

#ifndef PAPER_5CM_STEPS
#define PAPER_5CM_STEPS          (uint32_t)(PAPER_5CM_LENGTH_MM * DEFAULT_PANEL_STEPS_PER_MM)
#endif

#ifndef PAPER_PRE_CHECK_STEPS
#define PAPER_PRE_CHECK_STEPS    (uint32_t)(PAPER_PRE_CHECK_MM * DEFAULT_PANEL_STEPS_PER_MM)
#endif

#ifndef PAPER_CRITICAL_POSITION_STEPS
#define PAPER_CRITICAL_POSITION_STEPS  400  // 关键5cm定位步数
#endif

// ================================================================================
// 时序参数配置 - 控制精度和稳定性
// ================================================================================
// 步进脉冲参数（根据电机特性调整）
#define DEFAULT_STEP_PULSE_MICROSECONDS  5     // 步进脉冲宽度（微秒）
#define HC595_UPDATE_DELAY_US              10    // HC595硬件稳定时间（微秒）

// 运动间隔参数（影响速度和平稳性）
#define PAPER_EJECT_INTERVAL_US     1000   // 出纸步进间隔（微秒）
#define PAPER_PRE_CHECK_INTERVAL_US  1000   // 预检步进间隔（微秒）
#define PAPER_FEED_INTERVAL_US      2000   // 进纸步进间隔（微秒）

// 超时保护参数（安全机制）
#define PAPER_FEED_TIMEOUT_MS       5000   // 进纸超时（毫秒）
#define PAPER_STATE_TIMEOUT_MS      30000  // 单状态最大时间（毫秒）

// ================================================================================
// 控制参数配置 - 用户体验相关
// ================================================================================
#define PAPER_BUTTON_LONG_PRESS_MS   2000  // 长按判定时间（毫秒）
#define PAPER_SENSOR_DEBOUNCE_MS    50    // 传感器去抖动时间（毫秒）

// ================================================================================
// 调试参数配置 - 开发和故障排除
// ================================================================================
#define PAPER_DEBUG_ENABLED         1     // 是否启用调试输出
#define PAPER_PROGRESS_REPORT_INTERVAL 1000  // 进度报告间隔（步数）

// ================================================================================
// 安全限制参数 - 防止硬件损坏
// ================================================================================
#define PAPER_MAX_REVERSE_STEPS     1000   // 反转最大步数限制
#define PAPER_MAX_TOTAL_STEPS       50000  // 单过程最大总步数
#define PAPER_MAX_RETRY_COUNT       3      // 最大重试次数

#endif // AUTO_PAPER_CHANGE_ENABLE