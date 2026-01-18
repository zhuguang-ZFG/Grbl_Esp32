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
#define HC595_LATCH_PIN         5     // HC595锁存引脚（避免与X_STEP_PIN冲突）
#endif

// 换纸电机独立使能控制（与XYZ轴分离）
#ifndef PAPER_MOTORS_ENABLE
#define PAPER_MOTORS_ENABLE      26    // 换纸电机使能控制（拾落/面板/送纸器）
#endif

// ================================================================================
// 重要：警告信息，防止其他文件重定义这些参数
// ================================================================================
#ifdef PAPER_EJECT_STEPS
#pragma message("警告：PAPER_EJECT_STEPS在其他文件中重定义，请移除重复定义")
#endif

#ifdef PAPER_EJECT_INTERVAL_US
#pragma message("警告：PAPER_EJECT_INTERVAL_US在其他文件中重定义，请移除重复定义")
#endif

#ifdef PAPER_PRE_CHECK_STEPS
#pragma message("警告：PAPER_PRE_CHECK_STEPS在其他文件中重定义，请移除重复定义")
#endif

#ifdef PAPER_PRE_CHECK_INTERVAL_US
#pragma message("警告：PAPER_PRE_CHECK_INTERVAL_US在其他文件中重定义，请移除重复定义")
#endif

#ifdef DEFAULT_CLAMP_STEPS_PER_MM
#pragma message("警告：DEFAULT_CLAMP_STEPS_PER_MM在其他文件中重定义，请移除重复定义")
#endif

// ================================================================================
// HC595位映射定义 - 电机控制位分配
// ================================================================================
#define BIT_BUTTON_LED_CONTROL   0     // 按钮指示灯控制位（Q0）
#define BIT_PAPER_MOTORS_ENABLE  1     // 换纸电机使能控制位（Q1）
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
#ifndef DEFAULT_FEED_STEPS_PER_MM
#define DEFAULT_FEED_STEPS_PER_MM   80.0  // 进纸电机步数/毫米
#endif

#ifndef DEFAULT_PANEL_STEPS_PER_MM
#define DEFAULT_PANEL_STEPS_PER_MM  80.0  // 面板电机步数/毫米
#endif

#ifndef DEFAULT_CLAMP_STEPS_PER_MM
#define DEFAULT_CLAMP_STEPS_PER_MM  80.0  // 夹紧电机步数/毫米
#endif

// 速度参数（避免重定义）
#ifndef DEFAULT_FEED_MAX_RATE
#define DEFAULT_FEED_MAX_RATE      2000.0  // 进纸电机最大速度
#endif

#ifndef DEFAULT_PANEL_MAX_RATE
#define DEFAULT_PANEL_MAX_RATE     3000.0  // 面板电机最大速度
#endif

#ifndef DEFAULT_CLAMP_MAX_RATE
#define DEFAULT_CLAMP_MAX_RATE      2000.0  // 夹紧电机最大速度
#endif

// 运动距离参数（精确控制的关键参数）- 根据文档更新
#define PAPER_A4_LENGTH_MM          297.0  // A4纸张长度（毫米）
#define PAPER_4CM_LENGTH_MM          40.0  // 4厘米距离（毫米）- 步骤5b送纸器电机送纸距离（用户修正：从3.5cm改为4cm）
#define PAPER_3CM_LENGTH_MM          30.0  // 3厘米距离（毫米）- 传感器到夹紧电机距离+位置校准距离
#define PAPER_1_5CM_LENGTH_MM        15.0  // 1.5厘米距离（毫米）- 夹紧电机正转松开距离
#define PAPER_3TO5CM_LENGTH_MM       40.0  // 3-5厘米距离（毫米）- 出纸预留距离（取中间值4cm）
#define PAPER_PRE_CHECK_MM           3.0   // 预检距离（毫米）- 用户修正：从2.5mm改为3mm

// 步数计算（避免重定义）
#ifndef PAPER_EJECT_STEPS
#define PAPER_EJECT_STEPS            (uint32_t)(PAPER_A4_LENGTH_MM * DEFAULT_PANEL_STEPS_PER_MM)
#endif

#ifndef PAPER_4CM_STEPS
#define PAPER_4CM_STEPS              (uint32_t)(PAPER_4CM_LENGTH_MM * DEFAULT_FEED_STEPS_PER_MM)  // 使用送纸器电机步数计算（步骤5b使用送纸器电机，用户修正：从3.5cm改为4cm）
#endif
// 保持向后兼容（如果其他地方还在使用PAPER_3_5CM_STEPS）
#ifndef PAPER_3_5CM_STEPS
#define PAPER_3_5CM_STEPS            PAPER_4CM_STEPS  // 已改为4cm，保持兼容性
#endif

#ifndef PAPER_3CM_STEPS
#define PAPER_3CM_STEPS              (uint32_t)(PAPER_3CM_LENGTH_MM * DEFAULT_PANEL_STEPS_PER_MM)
#endif

#ifndef PAPER_1_5CM_STEPS
#define PAPER_1_5CM_STEPS            (uint32_t)(PAPER_1_5CM_LENGTH_MM * DEFAULT_CLAMP_STEPS_PER_MM)
#endif

#ifndef PAPER_3TO5CM_STEPS
#define PAPER_3TO5CM_STEPS           (uint32_t)(PAPER_3TO5CM_LENGTH_MM * DEFAULT_PANEL_STEPS_PER_MM)
#endif

#ifndef PAPER_PRE_CHECK_STEPS
#define PAPER_PRE_CHECK_STEPS        (uint32_t)(PAPER_PRE_CHECK_MM * DEFAULT_PANEL_STEPS_PER_MM)
#endif

#ifndef PAPER_EJECT_CHECK_STEPS
#define PAPER_EJECT_CHECK_STEPS       (uint32_t)(PAPER_3CM_LENGTH_MM * DEFAULT_PANEL_STEPS_PER_MM)  // 出纸预检反转3cm步数
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
#define PAPER_CLAMP_INTERVAL_US     2000   // 夹紧电机步进间隔（微秒）
#define PAPER_PANEL_INTERVAL_US     1000   // 面板电机步进间隔（微秒）

// 超时保护参数（安全机制）
#define PAPER_FEED_TIMEOUT_MS       5000   // 进纸超时（毫秒）
#define PAPER_STATE_TIMEOUT_MS      30000  // 单状态最大时间（毫秒）
#define PAPER_FEED_TIMEOUT_STEPS    (PAPER_3CM_STEPS + 500)  // 进纸超时步数（2900步，考虑进纸器电机到传感器距离3cm=2400步+安全余量500步）

// ================================================================================
// 控制参数配置 - 用户体验相关
// ================================================================================
#define PAPER_BUTTON_LONG_PRESS_MS   2000  // 长按判定时间（毫秒）
#define PAPER_BUTTON_DEBOUNCE_MS     50    // 按键去抖动时间（毫秒）
#define PAPER_SENSOR_DEBOUNCE_MS     50    // 传感器去抖动时间（毫秒）

// ================================================================================
// 调试参数配置 - 开发和故障排除
// ================================================================================
#define PAPER_DEBUG_ENABLED         1     // 是否启用调试输出
#define PAPER_PROGRESS_REPORT_INTERVAL 1000  // 进度报告间隔（步数）

// ================================================================================
// 安全限制参数 - 防止硬件损坏
// ================================================================================
#define PAPER_MAX_REVERSE_STEPS     4000   // 反转最大步数限制（50mm，确保有足够的反转距离检测纸张）
#define PAPER_MAX_TOTAL_STEPS       50000  // 单过程最大总步数
#define PAPER_MAX_RETRY_COUNT       3      // 最大重试次数

// ================================================================================
// 阈值参数配置 - 判定条件
// ================================================================================
#define PAPER_MIN_FEED_RATIO        0.8f   // FULL_FEED状态最小进给比例（80%）- 允许提前完成

#endif // AUTO_PAPER_CHANGE_ENABLE