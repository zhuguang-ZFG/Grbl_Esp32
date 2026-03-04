/*
  PaperChange.h - 自动换纸系统对外接口声明
  Part of Grbl_ESP32

  本头文件只暴露“写字机主程序”需要调用的函数，不包含具体实现细节。
  可以简单理解为：
    - 在主程序初始化时调用 paper_change_init()
    - 在主循环中周期性调用 paper_change_update()
    - 通过 M 指令或按键调用 paper_change_start() / paper_change_one_click()
    - 通过 paper_change_is_active() 与 paper_change_get_state() 查询当前换纸进度

  Copyright (c) 2024 Grbl_ESP32
*/

#pragma once

#ifdef AUTO_PAPER_CHANGE_ENABLE

#include "Grbl.h"

// === 换纸系统主流程函数 ===

// 初始化换纸系统（在系统启动时调用一次）
void paper_change_init();

// 启动一次完整的自动换纸流程（从当前纸张退出到新纸张定位完成）
void paper_change_start();

// 一键换纸入口（通常由按键或 M 指令触发，内部与 paper_change_start 类似）
void paper_change_one_click();

// 紧急停止当前换纸操作（立即停电机并进入错误状态）
void paper_change_stop();

// 在主循环中周期性调用，用于驱动换纸状态机前进
void paper_change_update();

// 查询换纸系统当前是否处于工作状态（非 IDLE 且非 ERROR）
bool paper_change_is_active();

// 获取当前换纸状态机的名称字符串（用于日志与调试）
const char* paper_change_get_state();

// 将换纸系统重置为初始空闲状态（清步数、清错误、关闭换纸电机）
void paper_change_reset();

// === 调试函数（手动测试，配合 M80x 指令使用） ===
// 调试进纸电机 (M800)：让进纸电机按给定步数/间隔单独运动
void debug_feed_motor(int steps, uint32_t delay_us);

// 调试面板电机 (M801)：只移动面板电机，验证面板机构运动方向与行程
void debug_panel_motor(int steps, uint32_t delay_us);

// 调试压纸/夹紧电机 (M802)：单独测试压纸机构
void debug_clamp_motor(int steps, uint32_t delay_us);

// 读取所有与换纸相关的传感器状态 (M803)：纸张传感器、一键换纸按钮等
void debug_read_sensors();

// 触发一次紧急停止 (M804)：等价于手动调用 paper_change_stop()
void debug_emergency_stop();

// 复位紧急停止标志 (M805)：清除 ERROR 状态，使系统可以再次换纸
void debug_reset_emergency();

// 直接写入 HC595 数据 (M806)：低层调试 74HC595 输出位，用于硬件连线排查
void debug_hc595_direct(uint8_t data);

#endif // AUTO_PAPER_CHANGE_ENABLE