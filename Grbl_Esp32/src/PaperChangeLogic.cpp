/*
  PaperChangeLogic.cpp - 换纸系统逻辑控制实现
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE
#include "PaperChangeConfig.h"
#include "PaperChangeTypes.h"
#include "PaperChangeLogic.h"
#include "PaperChangeHardware.h"
#include "PaperChangeUtils.h"
#include "PaperChange.h"
#include "SmartM0.h"

// ================================================================================
// 状态机控制实现
// ================================================================================

/**
 * @brief 状态转换验证函数
 * 
 * 检查状态机转换是否合法，防止非法状态跳转
 * 这对于系统稳定性和错误处理至关重要
 * 
 * @param from_state 源状态
 * @param to_state 目标状态
 * @return true=合法转换，false=非法转换
 * 
 * @example 典型合法转换流程：
 * IDLE → PRE_CHECK → EJECTING → FEEDING → UNCLAMP_FEED → CLAMP_FEED → FULL_FEED → REPOSITION → COMPLETE
 */
bool is_valid_transition(paper_change_state_t from_state, paper_change_state_t to_state) {
    // 参数有效性检查 - 防止数组越界
    if (from_state < PAPER_IDLE || from_state > PAPER_ERROR ||
        to_state < PAPER_IDLE || to_state > PAPER_ERROR) {
        return false;
    }
    
    // 查表验证转换合法性 - 使用预定义的状态转换矩阵
    return state_transition_matrix[from_state][to_state];
}

/**
 * @brief 安全状态转换函数
 * 
 * 执行状态机状态转换，包含完整的验证和日志记录
 * 这是整个换纸系统的核心控制函数
 * 
 * @param new_state 要转换到的新状态
 * 
 * @note 转换过程：
 * 1. 验证新状态有效性
 * 2. 检查转换合法性  
 * 3. 执行状态清理和初始化
 * 4. 记录转换日志
 * 5. 更新状态变量
 */
// ================================================================================
// 状态转换辅助函数 - 提高代码可读性
// ================================================================================

/**
 * @brief 验证状态转换的有效性
 * @param new_state 新状态
 * @return true=有效, false=无效
 */
static bool validate_state_transition(paper_change_state_t new_state) {
    // 状态参数有效性检查
    if (new_state < PAPER_IDLE || new_state > PAPER_ERROR) {
        LOG_ERROR_F("Invalid state value: %d", new_state);
        return false;
    }
    
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) {
        LOG_ERROR("Control structure not available");
        return false;
    }
    
    paper_change_state_t old_state = ctrl->state;
    
    // 状态转换合法性验证
    if (!is_valid_transition(old_state, new_state)) {
        LOG_ERROR_F("Invalid state transition %s->%s", 
                   GET_STATE_NAME(old_state), GET_STATE_NAME(new_state));
        return false;
    }
    
    return true;
}

/**
 * @brief 处理错误状态恢复
 * @param old_state 原状态
 * @param new_state 新状态
 * @return true=已处理, false=无需处理
 */
static bool handle_error_state_recovery(paper_change_state_t old_state, paper_change_state_t new_state) {
    if (old_state == PAPER_ERROR && new_state != PAPER_ERROR) {
        paper_change_ctrl_t* ctrl = get_paper_control();
        if (ctrl) {
            LOG_MSG_F("Recovering from ERROR state to %s", GET_STATE_NAME(new_state));
            ctrl->emergency_stop = false;
            STOP_ALL_MOTORS();
        }
        return true;
    }
    return false;
}

/**
 * @brief 重置静态标志
 * @param new_state 新状态
 */
static void reset_static_flags(paper_change_state_t new_state) {
    // 直接获取和设置静态标志，避免使用局部变量指针
    bool pos_init_val, eject_detected_val, reverse_complete_val;
    get_static_flags(&pos_init_val, &eject_detected_val, &reverse_complete_val);
    
    // 根据新状态重置相应的标志
    if (new_state == PAPER_REPOSITION) {
        pos_init_val = false;
    }
    
    if (new_state != PAPER_EJECTING) {
        eject_detected_val = false;
    }
    
    if (new_state != PAPER_REPOSITION) {
        reverse_complete_val = false;
    }
    
    set_static_flags(pos_init_val, eject_detected_val, reverse_complete_val);
}

/**
 * @brief 更新控制结构
 * @param new_state 新状态
 */
static void update_control_structure(paper_change_state_t new_state) {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (ctrl) {
        ctrl->state = new_state;
        ctrl->state_timer = millis();
        ctrl->step_counter = 0;
    }
}

/**
 * @brief 完成状态转换
 * @param old_state 原状态
 * @param new_state 新状态
 */
static void complete_state_transition(paper_change_state_t old_state, paper_change_state_t new_state) {
    // 初始化电机时序
    init_motor_timing();
    
    // 记录状态转换
    LOG_MSG_F("State %s->%s", GET_STATE_NAME(old_state), GET_STATE_NAME(new_state));
}

// ================================================================================
// 主状态转换函数 - 简化版本
// ================================================================================

void enter_state(paper_change_state_t new_state) {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) {
        LOG_ERROR("Control structure not available");
        return;
    }
    
    paper_change_state_t old_state = ctrl->state;
    
    // 验证状态转换
    if (!validate_state_transition(new_state)) {
        // 自动进入错误状态，保护系统安全
        if (new_state != PAPER_ERROR) {
            enter_state(PAPER_ERROR);
        }
        return;
    }
    
    // 处理错误状态恢复
    handle_error_state_recovery(old_state, new_state);
    
    // 重置静态标志
    reset_static_flags(new_state);
    
    // 更新控制结构
    update_control_structure(new_state);
    
    // 完成状态转换
    complete_state_transition(old_state, new_state);
}

/**
 * @brief 状态机更新函数
 * 
 * 这是换纸系统的核心调度器，根据当前状态调用相应的处理函数
 * 采用状态机设计模式，确保系统状态转换的可预测和可控性
 * 
 * @note 设计原则：
 * 1. 每个状态有明确的入口和出口条件
 * 2. 状态转换必须经过验证，防止非法跳转
 * 3. 每个状态都有超时保护，防止死锁
 * 4. 所有电机控制都是非阻塞的，保证系统响应性
 * 
 * @warning 状态机是单线程的，任何状态处理都不能阻塞太久
 * 
 * @example 状态流转：
 * IDLE → PRE_CHECK → EJECTING → FEEDING → UNCLAMP_FEED → CLAMP_FEED → FULL_FEED → REPOSITION → COMPLETE
 */
void handle_state_machine_update() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) {
        LOG_ERROR("Control structure not available for state machine update");
        return;
    }
    
    // 根据当前状态调用相应的处理函数
    switch (ctrl->state) {
        case PAPER_IDLE:
            // 空闲状态：系统待命，等待触发条件
            // 触发条件：M0命令、用户按钮等
            break;
            
        case PAPER_PRE_CHECK:
            // 预检状态：检查面板上是否有纸张
            // 功能：反转2.5mm，检查传感器状态
            handle_pre_check_state();
            break;
            
        case PAPER_EJECTING:
            // 出纸状态：将现有纸张完全送出
            // 功能：检测边缘 → 完全送出A4+4cm
            handle_ejecting_state();
            break;
            
        case PAPER_FEEDING:
            // 进纸状态：送入新纸张
            // 功能：进纸直到检测到纸张前边缘
            handle_feeding_state();
            break;
            
        case PAPER_UNCLAMP_FEED:
            handle_unclamp_feed_state();
            break;
            
        case PAPER_CLAMP_FEED:
            handle_clamp_feed_state();
            break;
            
        case PAPER_FULL_FEED:
            handle_full_feed_state();
            break;
            
        case PAPER_REPOSITION:
            handle_reposition_state();
            break;
            
        case PAPER_COMPLETE:
            handle_complete_state();
            break;
            
        case PAPER_ERROR:
            handle_error_state();
            break;
    }
}

// ================================================================================
// 业务逻辑处理实现
// ================================================================================

/**
 * @brief 处理预检状态逻辑
 * 
 * 预检阶段的目的是检查面板上是否已有纸张
 * 这是出纸流程的第一个步骤，确保后续操作的准确性
 * 
 * @note 预检流程：
 * 1. 面板电机反转2.5mm，移除可能的纸张遮挡
 * 2. 检查传感器状态
 * 3. 根据检测结果决定进入出纸或进纸状态
 * 
 * @warning 此步骤确保纸张传感器可靠工作，避免误检测
 */
void handle_pre_check_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    HANDLE_NULL_POINTER(ctrl, "handle_pre_check_state");
    
    // 设置适合预检阶段的电流
    set_current_for_paper_phase(PAPER_PRE_CHECK);
    
    // 安全检查：防止预检过程卡死
    CHECK_STATE_SAFETY(PAPER_PRE_CHECK_STEPS + 50, "PRE_CHECK");
    
    // 步骤1：预检反转2.5mm，清除可能的干扰
    if (ctrl->step_counter < PAPER_PRE_CHECK_STEPS) {
        if (nonblocking_panel_step(false)) {
            ctrl->step_counter++;
        }
    } else {
        // 步骤2：根据传感器状态决定后续流程
        if (ctrl->paper_sensor_state) {
            // 传感器检测到纸张 -> 面板上有纸，需要先送出
            LOG_MSG("Paper detected on panel, starting ejection sequence");
            enter_state(PAPER_EJECTING);
        } else {
            // 传感器未检测到纸张 -> 面板为空，可以直接进纸
            LOG_MSG("No paper on panel, starting feeding sequence");
            enter_state(PAPER_FEEDING);
        }
    }
}

// 出纸状态子函数声明
static void handle_eject_pre_check(paper_change_ctrl_t* ctrl, bool* eject_detected);
static void handle_edge_detection(paper_change_ctrl_t* ctrl, bool* eject_detected);
static void handle_full_ejection(paper_change_ctrl_t* ctrl, bool* eject_detected);
static void handle_no_paper_detected(paper_change_ctrl_t* ctrl, bool* eject_detected);

/**
 * @brief 处理出纸状态逻辑
 * 
 * 出纸流程分为三个阶段：
 * 1. 预检反转(3cm) → 2. 边沿检测 → 3. 完全出纸(A4+4cm)
 */
void handle_ejecting_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    // 直接获取静态标志，避免使用局部变量指针
    bool pos_init_val, eject_detected_val, reverse_complete_val;
    get_static_flags(&pos_init_val, &eject_detected_val, &reverse_complete_val);
    bool* eject_detected = &eject_detected_val;  // 需要修改，所以保留指针
    
    CHECK_STATE_SAFETY(PAPER_EJECT_CHECK_STEPS + PAPER_MAX_REVERSE_STEPS + 
                      PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS + 200, "EJECTING");
    
    // 步骤0：出纸预检 - 面板电机反转3cm检测是否有纸
    // 预检完成后会重置step_counter=0，然后进入步骤1
    if (!eject_detected_val && ctrl->step_counter < PAPER_EJECT_CHECK_STEPS) {
        handle_eject_pre_check(ctrl, eject_detected);
    }
    // 步骤1：检测纸张后边缘位置（预检完成后，从step_counter=0开始继续反转）
    // 注意：预检完成后step_counter被重置为0，边缘检测阶段从0开始计数
    // 最大反转步数限制：PAPER_MAX_REVERSE_STEPS（不包括预检步数）
    else if (!eject_detected_val && ctrl->step_counter < PAPER_MAX_REVERSE_STEPS) {
        handle_edge_detection(ctrl, eject_detected);
    }
    // 步骤2：完全出纸 - 面板电机快速正转
    else if (eject_detected_val && ctrl->step_counter < (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS)) {
        handle_full_ejection(ctrl, eject_detected);
    }
    // 步骤3：如果反转没有检测到纸张（超过最大反转步数）
    else if (!eject_detected_val && ctrl->step_counter >= PAPER_MAX_REVERSE_STEPS) {
        handle_no_paper_detected(ctrl, eject_detected);
    }
}

/**
 * @brief 出纸预检 - 反转3cm检测传感器状态
 */
static void handle_eject_pre_check(paper_change_ctrl_t* ctrl, bool* eject_detected) {
    HANDLE_NULL_POINTER(ctrl, "handle_eject_pre_check");
    HANDLE_NULL_POINTER(eject_detected, "handle_eject_pre_check");
    
    if (nonblocking_panel_step(false)) {
        ctrl->step_counter++;
        
        if (ctrl->step_counter % 100 == 0) {
            float check_mm = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
            LOG_PROGRESS("Ejection pre-check reverse: %.1fmm", check_mm);
        }
        
        if (ctrl->step_counter >= PAPER_EJECT_CHECK_STEPS) {
            LOG_MSG("Ejection pre-check complete (3cm reverse), starting full detection");
            ctrl->step_counter = 0;
        }
    }
}

/**
 * @brief 边沿检测 - 寻找纸张后边缘
 * 
 * 传感器位置：在面板电机和进纸器电机之间
 * 出纸反转时，面板电机反转（向后移动纸张），纸张后边缘经过传感器
 * 传感器从无纸(false)变为有纸(true)，表示检测到纸张后边缘
 * 硬件上对应：HIGH(无纸)→LOW(有纸)的电平跳变（传感器低电平有效）
 */
static void handle_edge_detection(paper_change_ctrl_t* ctrl, bool* eject_detected) {
    if (nonblocking_panel_step(false)) {
        ctrl->step_counter++;
        
        // 检测纸张后边缘：从无纸(false)到有纸(true)的跳变
        // 硬件上对应HIGH→LOW电平跳变（传感器低电平有效）
        if (ctrl->paper_sensor_state && !ctrl->last_paper_sensor_state) {
            LOG_MSG("Paper rear edge detected in reverse (clear->detected), switching to forward ejection");
            ctrl->step_counter = 0;
            *eject_detected = true;
            
            // 设置快速正转速度
            motor_timing_t* panel_timing = get_panel_motor_timing();
            if (panel_timing) {
                panel_timing->step_interval = 500; // 2kHz快速出纸
            }
        }
        
        if (ctrl->step_counter % 200 == 0) {
            float reverse_mm = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
            LOG_PROGRESS("Panel motor reverse searching: %.1fmm", reverse_mm);
        }
    }
}

/**
 * @brief 完全出纸 - 快速正转A4+4cm距离
 */
static void handle_full_ejection(paper_change_ctrl_t* ctrl, bool* eject_detected) {
    if (nonblocking_panel_step(true)) {
        ctrl->step_counter++;
        
        if (ctrl->step_counter % 1000 == 0) {
            float progress = (float)ctrl->step_counter / (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS) * 100.0;
            LOG_PROGRESS("Ejection progress: %.1f%% (Step %d/%d)", 
                       progress, ctrl->step_counter, (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS));
        }
        
        if (ctrl->step_counter >= (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS)) {
            LOG_MSG("Paper ejection complete - A4(297mm) + reserve(40mm) = total(337mm), %d steps", 
                     (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS));
            *eject_detected = false;
            enter_state(PAPER_FEEDING);
        }
    }
}

/**
 * @brief 处理无纸张检测情况
 */
static void handle_no_paper_detected(paper_change_ctrl_t* ctrl, bool* eject_detected) {
    LOG_MSG("No paper detected in reverse, proceeding to feed");
    *eject_detected = false;
    enter_state(PAPER_FEEDING);
}

/**
 * @brief 处理进纸状态逻辑
 * 
 * 进纸是换纸流程的第一个主动动作，负责将新纸张送入系统
 * 这是整个换纸过程的基础，需要确保可靠检测
 * 
 * @note 进纸流程：
 * 1. 启动进纸电机（永不反转，只正向进纸）
 * 2. 监控纸张传感器，等待检测到纸张前边缘
 * 3. 检测到纸张后立即停止，准备松开夹紧机构
 * 
 * @warning 进纸超时保护：500步内未检测到纸张将报错
 * 这防止了电机空转导致的硬件损坏
 * 
 * @example 实际场景：
 * 纸张盒中插入A4纸 → 进纸电机启动 → 传感器检测到纸张前边缘 → 进入松开状态
 */
void handle_feeding_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    HANDLE_NULL_POINTER(ctrl, "handle_feeding_state");
    
    // 安全检查：防止进纸过程卡死，最多1000步
    CHECK_STATE_SAFETY(1000, "FEEDING");
    
    // 步骤1：等待纸张前边缘检测
    // 传感器跳变检测：HIGH→LOW电平跳变表示检测到纸张前边缘
    // paper_sensor_state=true 表示传感器检测到纸张（硬件低电平）
    if (ctrl->paper_sensor_state && !ctrl->last_paper_sensor_state) {
        LOG_MSG("Paper front edge detected by sensor (HIGH->LOW transition), entering unclamp state");
        STOP_ALL_MOTORS();              // 立即停止所有电机，精确定位
        enter_state(PAPER_UNCLAMP_FEED); // 进入松开状态，准备夹紧机构操作
    } else if (ctrl->step_counter > 500) {
        // 超时保护：500步（约6.25mm）内未检测到纸张
        // 这通常表示纸张盒为空或传感器故障
        handle_error(ERROR_SENSOR_TIMEOUT, "Feed operation timeout - no paper detected", PAPER_FEEDING);
    } else {
        // 持续进纸：进纸电机正转，等待检测到纸张
        // 注意：进纸电机只正转，从不反转，这是机械设计决定的
        if (nonblocking_feed_step(true)) {
            ctrl->step_counter++;
        }
    }
}

/**
 * @brief 处理松开进纸状态逻辑
 * 按照文档步骤2：检测到纸张后 - 夹紧电机正转1.5cm松开夹子，面板电机同步运行，纸张经过传感器后继续运行3.5cm
 */
void handle_unclamp_feed_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    HANDLE_NULL_POINTER(ctrl, "handle_unclamp_feed_state");
    
    motor_timing_t* clamp_timing = get_clamp_motor_timing();
    motor_timing_t* panel_timing = get_panel_motor_timing();
    if (!clamp_timing || !panel_timing) return;
    
    CHECK_STATE_SAFETY(PAPER_1_5CM_STEPS + PAPER_3_5CM_STEPS + 100, "UNCLAMP_FEED");
    
    if (ctrl->step_counter < PAPER_1_5CM_STEPS) {
        // 步骤2a：夹紧电机正转1.5cm松开夹子（初始为夹紧态）
        if (nonblocking_clamp_step(true)) {
            ctrl->step_counter++;
            
            if (ctrl->step_counter % 50 == 0) {
                float progress_mm = steps_to_mm(ctrl->step_counter, DEFAULT_CLAMP_STEPS_PER_MM);
                LOG_PROGRESS("Clamp motor releasing: %.1fmm", progress_mm);
            }
            
            if (ctrl->step_counter == PAPER_1_5CM_STEPS) {
                LOG_MSG("Clamp motor released (1.5cm forward)");
            }
        }
    } else if (ctrl->step_counter < PAPER_1_5CM_STEPS + PAPER_3_5CM_STEPS) {
        // 步骤2b：面板电机同步运行，纸张经过传感器后继续运行3.5cm
        if (nonblocking_panel_step(true)) {
            ctrl->step_counter++;
            
            // 每100步报告一次进度
            if ((ctrl->step_counter - PAPER_1_5CM_STEPS) % 100 == 0) {
                float progress_mm = steps_to_mm(ctrl->step_counter - PAPER_1_5CM_STEPS, DEFAULT_PANEL_STEPS_PER_MM);
                LOG_PROGRESS("Panel motor transporting: %.1fmm", progress_mm);
            }
            
            if (ctrl->step_counter == PAPER_1_5CM_STEPS + PAPER_3_5CM_STEPS) {
                LOG_MSG("Panel motor transport complete (3.5cm forward)");
            }
        }
    } else {
        LOG_MSG("Unclamp and 3.5cm transport complete, entering clamp feed");
        enter_state(PAPER_CLAMP_FEED);
    }
}

/**
 * @brief 处理夹紧进纸状态逻辑
 * 按照文档步骤3：夹紧行程 - 夹紧电机反转到初始位置夹紧纸张
 */
void handle_clamp_feed_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    CHECK_STATE_SAFETY(PAPER_1_5CM_STEPS + 50, "CLAMP_FEED");
    
    if (ctrl->step_counter < PAPER_1_5CM_STEPS) {
        // 夹紧电机反转1.5cm到初始位置夹紧纸张
        if (nonblocking_clamp_step(false)) {
            ctrl->step_counter++;
            
            if (ctrl->step_counter % 50 == 0) {
                float progress_mm = steps_to_mm(ctrl->step_counter, DEFAULT_CLAMP_STEPS_PER_MM);
                LOG_PROGRESS("Clamp motor clamping: %.1fmm", progress_mm);
            }
            
            if (ctrl->step_counter == PAPER_1_5CM_STEPS) {
                LOG_MSG("Clamping complete (1.5cm reverse to initial position)");
            }
        }
    } else {
        LOG_MSG("Paper clamped, entering full feed state");
        enter_state(PAPER_FULL_FEED);
    }
}

/**
 * @brief 处理完整进给状态逻辑
 * 
 * 在此状态中，进纸电机和面板电机同步运行，将纸张完全送入系统
 * 直到纸张脱离传感器位置，表示纸张已经完全进入系统
 * 
 * @note 完成条件：
 * 1. 步进计数器 >= A4纸张长度步数（PAPER_EJECT_STEPS）
 * 2. 传感器状态为无纸（!paper_sensor_state），表示纸张已脱离传感器位置
 */
void handle_full_feed_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    motor_timing_t* feed_timing = get_feed_motor_timing();
    motor_timing_t* panel_timing = get_panel_motor_timing();
    if (!ctrl || !feed_timing || !panel_timing) return;
    
    CHECK_STATE_SAFETY(25000, "FULL_FEED");
    
    // 设置两个电机同步运行，相同步进间隔
    feed_timing->step_interval = 1000;
    panel_timing->step_interval = 1000;
    
    // 检查完成条件：
    // 1. 已进给A4长度且纸张已脱离传感器（正常情况）
    // 2. 或者纸张提前脱离传感器（提前完成，也需要至少进给80%的距离）
    uint32_t min_feed_steps = (uint32_t)(PAPER_EJECT_STEPS * 0.8f);  // 至少进给80%
    if ((ctrl->step_counter >= PAPER_EJECT_STEPS && !ctrl->paper_sensor_state) ||
        (!ctrl->paper_sensor_state && ctrl->step_counter >= min_feed_steps)) {
        LOG_MSG("Full feed complete, paper detached from sensor");
        STOP_ALL_MOTORS();
        enter_state(PAPER_REPOSITION);
    } else {
        // 只有当两个电机都到了应该步进的时间时，才同步执行步进
        if (millis() - feed_timing->last_step_time >= feed_timing->step_interval &&
            millis() - panel_timing->last_step_time >= panel_timing->step_interval) {
            
            // 同步步进两个电机
            motor_step_control(BIT_FEED_MOTOR_STEP, BIT_FEED_MOTOR_DIR, true, feed_timing->step_interval);
            motor_step_control(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true, panel_timing->step_interval);
            
            // 更新时序和计数器
            feed_timing->last_step_time = millis();
            panel_timing->last_step_time = millis();
            ctrl->step_counter++;  // 每次两个电机同步步进时，计数器加1
            
            if (ctrl->step_counter % 1000 == 0) {
                float progress = (float)ctrl->step_counter / PAPER_EJECT_STEPS * 100.0;
                LOG_PROGRESS("Full feed progress: %.1f%% (Step %lu/%lu)", 
                           progress, ctrl->step_counter, PAPER_EJECT_STEPS);
            }
        }
    }
}

/**
 * @brief 处理重新定位状态逻辑
 * 按照文档步骤4：位置校准 - 进纸电机停止，面板电机反转直到再次感应到纸张，然后正转3cm
 */
void handle_reposition_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    motor_timing_t* panel_timing = get_panel_motor_timing();
    if (!ctrl || !panel_timing) return;
    
    // 直接获取静态标志，避免使用局部变量指针
    bool pos_init_val, eject_detected_val, reverse_complete_val;
    get_static_flags(&pos_init_val, &eject_detected_val, &reverse_complete_val);
    bool* reverse_complete = &reverse_complete_val;  // 需要修改，所以保留指针
    
    CHECK_STATE_SAFETY(PAPER_MAX_REVERSE_STEPS + PAPER_3CM_STEPS + 100, "REPOSITION");
    
    if (!reverse_complete_val && ctrl->step_counter < PAPER_MAX_REVERSE_STEPS) {
        // 步骤4a：面板电机反转，直到传感器再次感应到纸张
        // 反转时，传感器从无纸(false)变为有纸(true)，表示检测到纸张边缘
        if (nonblocking_panel_step(false)) {
            ctrl->step_counter++;
            
            // 检测纸张边缘：从无纸(false)到有纸(true)的跳变（反转时检测前边缘）
            // 硬件上对应HIGH→LOW电平跳变（传感器低电平有效）
            if (ctrl->paper_sensor_state && !ctrl->last_paper_sensor_state) {
                LOG_MSG("Paper edge detected in reverse (clear->detected), starting 3cm forward positioning");
                ctrl->step_counter = 0;
                reverse_complete_val = true;
                *reverse_complete = true;  // 更新标志
            }
            
            if (ctrl->step_counter % 200 == 0) {
                float reverse_mm = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
                LOG_PROGRESS("Panel motor reversing: %.1fmm", reverse_mm);
            }
        }
    } else if (reverse_complete_val && ctrl->step_counter < PAPER_3CM_STEPS) {
        // 步骤4b：面板电机正转3cm左右（关键步骤，确保纸张位置正确）
        if (nonblocking_panel_step(true)) {
            ctrl->step_counter++;
            
            if (ctrl->step_counter % 50 == 0) {
                float position_mm = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
                LOG_PROGRESS("Critical positioning: %.1fmm", position_mm);
            }
            
            if (ctrl->step_counter >= PAPER_3CM_STEPS) {
                reverse_complete_val = false;
                *reverse_complete = false;  // 更新标志
                set_static_flags(pos_init_val, eject_detected_val, reverse_complete_val);  // 保存到全局
                float final_position = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
                LOG_MSG_F("Paper positioning complete at %.1fmm (critical 3cm forward)", final_position);
                enter_state(PAPER_COMPLETE);
            }
        }
    } else if (!reverse_complete_val && ctrl->step_counter >= PAPER_MAX_REVERSE_STEPS) {
        LOG_MSG("No paper detected in reposition, completing without positioning");
        reverse_complete_val = false;
        *reverse_complete = false;  // 更新标志
        set_static_flags(pos_init_val, eject_detected_val, reverse_complete_val);  // 保存到全局
        enter_state(PAPER_COMPLETE);
    }
}

/**
 * @brief 处理完成状态逻辑
 */
void handle_complete_state() {
    LOG_MSG("Paper change completed successfully");
    m0_paper_change_complete();
    
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (ctrl) {
        ctrl->one_click_active = false;
    }
    
    enter_state(PAPER_IDLE);
}

/**
 * @brief 处理错误状态逻辑
 */
void handle_error_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    uint32_t current_time = millis();
    
    // 处理紧急停止：2秒后自动恢复
    if (ctrl->emergency_stop) {
        if (current_time - ctrl->state_timer > 2000) {
            LOG_MSG("Emergency stop timeout, auto-resuming");
            ctrl->emergency_stop = false;
            enter_state(PAPER_IDLE);
        }
    }
    // 处理其他错误：检查错误信息，决定是否自动恢复
    else {
        error_info_t* error_info = get_error_info();
        if (error_info && error_info->auto_recovery_enabled) {
            // 如果错误信息存在且允许自动恢复，可以在这里实现恢复逻辑
            // 目前保持错误状态，等待用户手动处理或外部调用恢复
        }
    }
}

// ================================================================================
// 按钮处理实现
// ================================================================================

/**
 * @brief 检查按钮状态
 */
void check_button_press() {
    static bool last_button_state = false;
    static uint32_t button_press_time = 0;
    
    bool button_pressed = read_button_state();
    
    if (button_pressed && !last_button_state) {
        button_press_time = millis();
    } else if (!button_pressed && last_button_state) {
        uint32_t press_duration = millis() - button_press_time;
        
        if (press_duration < PAPER_BUTTON_LONG_PRESS_MS) {
            paper_change_one_click();
        } else {
            smart_m0_emergency_stop();
        }
    }
    
    last_button_state = button_pressed;
}

/**
 * @brief 纸张换纸状态机更新函数 - 公共接口
 */
void paper_state_machine_update() {
    handle_state_machine_update();
}

#endif // AUTO_PAPER_CHANGE_ENABLE