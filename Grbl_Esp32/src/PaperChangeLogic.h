/*
  PaperChangeLogic.h - 换纸系统逻辑控制层
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#pragma once

#ifdef AUTO_PAPER_CHANGE_ENABLE

#include "Grbl.h"
#include "PaperChangeConfig.h"
#include "PaperChangeTypes.h"

// ================================================================================
// 状态机控制接口
// ================================================================================

/**
 * @brief 状态转换验证
 * @param from_state 源状态
 * @param to_state 目标状态
 * @return true=允许转换，false=非法转换
 */
bool is_valid_transition(paper_change_state_t from_state, paper_change_state_t to_state);

/**
 * @brief 安全状态转换
 * @param new_state 新状态
 * 执行安全的状态转换，包含验证和日志
 */
void enter_state(paper_change_state_t new_state);

/**
 * @brief 状态机更新函数
 * 在主循环中调用，处理状态转换和业务逻辑
 */
void paper_state_machine_update();

// ================================================================================
// 故障处理接口
// ================================================================================

/**
 * @brief 故障恢复策略决策
 * @param error_type 错误类型
 * @param retry_count 重试次数
 * @return 恢复策略
 */
recovery_strategy_t determine_recovery_strategy(paper_error_type_t error_type, uint8_t retry_count);

/**
 * @brief 执行故障恢复操作
 * @param strategy 恢复策略
 * @return true=恢复成功，false=恢复失败
 */
bool execute_recovery(recovery_strategy_t strategy);

/**
 * @brief 错误处理函数
 * @param error_type 错误类型
 * @param description 错误描述
 * @param current_state 当前状态
 */
void handle_error(paper_error_type_t error_type, const char* description, paper_change_state_t current_state);

/**
 * @brief 清除错误状态
 */
void clear_error_state();

// ================================================================================
// 业务逻辑处理接口
// ================================================================================

/**
 * @brief 处理预检状态逻辑
 */
void handle_pre_check_state();

/**
 * @brief 处理出纸状态逻辑
 */
void handle_ejecting_state();

/**
 * @brief 处理进纸状态逻辑
 */
void handle_feeding_state();

/**
 * @brief 处理松开进纸状态逻辑
 */
void handle_unclamp_feed_state();

/**
 * @brief 处理夹紧进纸状态逻辑
 */
void handle_clamp_feed_state();

/**
 * @brief 处理完整进给状态逻辑
 */
void handle_full_feed_state();

/**
 * @brief 处理重新定位状态逻辑
 */
void handle_reposition_state();

/**
 * @brief 处理完成状态逻辑
 */
void handle_complete_state();

/**
 * @brief 处理错误状态逻辑
 */
void handle_error_state();

// ================================================================================
// 按钮处理接口
// ================================================================================

/**
 * @brief 检查按钮状态
 * 处理短按和长按逻辑
 */
void check_button_press();

// ================================================================================
// 安全检查接口
// ================================================================================

/**
 * @brief 系统参数验证
 * @return true=所有参数有效，false=存在无效参数
 */
bool validate_system_parameters();

/**
 * @brief 运行时安全检查
 * @param step_counter 当前步数
 * @param max_steps 最大步数
 * @param operation_name 操作名称
 * @return true=安全，false=超限
 */
bool runtime_safety_check(uint32_t step_counter, uint32_t max_steps, const char* operation_name);

#endif // AUTO_PAPER_CHANGE_ENABLE