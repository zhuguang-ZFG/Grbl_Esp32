/*
  PaperChangeHardware.h - 换纸系统硬件驱动层
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#pragma once

#ifdef AUTO_PAPER_CHANGE_ENABLE

#include "Grbl.h"
#include "PaperChangeConfig.h"
#include "PaperChangeTypes.h"
#include "HR4988VREF_SIMPLE.h"

// ================================================================================
// 硬件抽象层接口 - 便于移植和维护
// ================================================================================

/**
 * @brief HC595移位寄存器初始化
 * 配置所有相关GPIO引脚为输出模式
 */
void hc595_init();

/**
 * @brief HC595移位寄存器数据写入
 * @param data 要写入的8位数据
 */
void hc595_write(uint8_t data);

/**
 * @brief HC595高性能数据写入
 * @param data 要写入的8位数据
 * 使用寄存器直接操作，性能更高
 */
void hc595_write_fast(uint8_t data);

/**
 * @brief 传感器系统初始化
 * 配置传感器引脚和相关参数
 */
void sensor_system_init();

/**
 * @brief 读取纸张传感器原始状态
 * @return true=检测到纸张，false=无纸张
 */
bool read_paper_sensor_raw();

/**
 * @brief 读取带去抖动的纸张传感器状态
 * @return true=稳定检测到纸张，false=稳定无纸张
 */
bool read_paper_sensor_debounced();

/**
 * @brief 按钮状态读取
 * @return true=按钮按下，false=按钮释放
 */
bool read_button_state();

/**
 * @brief 电机控制基础函数
 * @param step_bit 步进控制位
 * @param dir_bit 方向控制位
 * @param forward 前进方向
 * @param step_interval 步进间隔
 */
void motor_step_control(uint8_t step_bit, uint8_t dir_bit, bool forward, uint32_t step_interval);

/**
 * @brief 停止所有电机
 * 清除HC595所有输出
 */
void stop_all_motors();

/**
 * @brief 硬件自检函数
 * @return true=硬件正常，false=硬件故障
 */
bool hardware_self_test();

// ================================================================================
// 换纸电机使能控制接口
// ================================================================================

/**
 * @brief 启用换纸电机
 * 设置PAPER_MOTORS_ENABLE为低电平（使能有效）
 */
void enable_paper_motors();

/**
 * @brief 禁用换纸电机
 * 设置PAPER_MOTORS_ENABLE为高电平（禁用有效）
 */
void disable_paper_motors();

/**
 * @brief 获取换纸电机使能状态
 * @return true=已启用, false=已禁用
 */
bool is_paper_motors_enabled();

/**
 * @brief 设置按钮LED状态
 * @param on true=LED亮，false=LED灭
 */
void set_button_led(bool on);

/**
 * @brief 获取按钮LED状态
 * @return true=LED亮，false=LED灭
 */
bool get_button_led();

// ================================================================================
// HR4988 VREF动态电流控制接口
// ================================================================================

/**
 * @brief 初始化HR4988 VREF控制（简化版）
 * @return true=成功, false=失败
 */
bool init_hr4988_vref();

/**
 * @brief 设置换纸系统电机工作电流
 * @param current 电流 (A)
 * @return true=成功, false=失败
 */
bool set_paper_motor_current(float current);

/**
 * @brief 设置换纸系统工作模式（已废弃：VREF固定输出0.64V，不支持模式切换）
 * @param mode 工作模式（已废弃）
 * @return false（不支持）
 */
bool set_paper_motor_mode(int mode);  // 使用int避免类型依赖

/**
 * @brief 为特定换纸阶段设置合适的电流
 * @param phase 换纸阶段
 */
void set_current_for_paper_phase(paper_change_state_t phase);

/**
 * @brief 更新HR4988 VREF控制状态
 * 需要在主循环中定期调用
 */
void update_hr4988_vref();

/**
 * @brief 输出HR4988 VREF状态信息
 */
void print_hr4988_vref_status();

#endif // AUTO_PAPER_CHANGE_ENABLE