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
    
    // 步骤1：预检反转3mm，检测面板是否有纸
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
static void handle_edge_detection(paper_change_ctrl_t* ctrl, bool* eject_detected);
static void handle_full_ejection(paper_change_ctrl_t* ctrl, bool* eject_detected);
static void handle_no_paper_detected(paper_change_ctrl_t* ctrl, bool* eject_detected);

/**
 * @brief 处理出纸状态逻辑
 * 
 * 完全出纸：面板电机正转，送出A4(297mm) + 预留(40mm) = 337mm
 * 
 * @note PRE_CHECK已经检测到有纸，直接完全出纸，无需边缘检测
 */
void handle_ejecting_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    CHECK_STATE_SAFETY(PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS + 200, "EJECTING");
    
    // 完全出纸 - 面板电机快速正转
    // 距离：A4(297mm) + 预留(40mm) = 337mm
    // 步数：26960步（337mm × 80 steps/mm）
    if (ctrl->step_counter < (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS)) {
        handle_full_ejection(ctrl, nullptr);  // 不需要eject_detected标志
    } else {
        // 出纸完成
        LOG_MSG("Paper ejection complete - A4(297mm) + reserve(40mm) = total(337mm), %lu steps", 
                 (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS));
        STOP_ALL_MOTORS();
        enter_state(PAPER_FEEDING);
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
            
            // 保存标志到全局静态变量
            // 注意：eject_detected已经通过指针修改为true，现在保存到全局
            bool pos_init_val, eject_detected_val, reverse_complete_val;
            get_static_flags(&pos_init_val, &eject_detected_val, &reverse_complete_val);
            set_static_flags(pos_init_val, true, reverse_complete_val);  // 保存eject_detected=true（使用true而非从get获取的值）
            
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
 * 
 * @param ctrl 控制结构体指针
 * @param eject_detected 边缘检测标志指针（已废弃，保留参数以保持兼容性）
 */
static void handle_full_ejection(paper_change_ctrl_t* ctrl, bool* eject_detected) {
    // 设置快速正转速度（2kHz）
    motor_timing_t* panel_timing = get_panel_motor_timing();
    if (panel_timing) {
        panel_timing->step_interval = 500; // 2kHz快速出纸
    }
    
    if (nonblocking_panel_step(true)) {
        ctrl->step_counter++;
        
        if (ctrl->step_counter % 1000 == 0) {
            float progress = (float)ctrl->step_counter / (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS) * 100.0;
            LOG_PROGRESS("Ejection progress: %.1f%% (Step %lu/%lu)", 
                       progress, ctrl->step_counter, (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS));
        }
    }
}

/**
 * @brief 处理无纸张检测情况
 */
static void handle_no_paper_detected(paper_change_ctrl_t* ctrl, bool* eject_detected) {
    LOG_MSG("No paper detected in reverse, proceeding to feed");
    *eject_detected = false;
    
    // 保存标志到全局静态变量
    bool pos_init_val, eject_detected_val, reverse_complete_val;
    get_static_flags(&pos_init_val, &eject_detected_val, &reverse_complete_val);
    set_static_flags(pos_init_val, false, reverse_complete_val);  // 保存eject_detected=false
    
    // 停止所有电机，确保状态转换时电机已停止
    STOP_ALL_MOTORS();
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
 * @warning 进纸超时保护：至少2900步（约36.25mm）内未检测到纸张将报错
 * 考虑进纸器电机到传感器的距离3cm（2400步）+ 安全余量500步
 * 这防止了电机空转导致的硬件损坏
 * 
 * @example 实际场景：
 * 纸张盒中插入A4纸 → 进纸电机启动 → 传感器检测到纸张前边缘 → 进入松开状态
 */
void handle_feeding_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    HANDLE_NULL_POINTER(ctrl, "handle_feeding_state");
    
    // 安全检查：防止进纸过程卡死，考虑进纸器电机到传感器距离3cm（2400步）+ 超时步数（2900步）+ 安全余量（500步）
    CHECK_STATE_SAFETY(PAPER_FEED_TIMEOUT_STEPS + 500, "FEEDING");
    
    // 步骤1：等待纸张前边缘检测
    // 传感器跳变检测：HIGH→LOW电平跳变表示检测到纸张前边缘
    // paper_sensor_state=true 表示传感器检测到纸张（硬件低电平）
    if (ctrl->paper_sensor_state && !ctrl->last_paper_sensor_state) {
        LOG_MSG("Paper front edge detected by sensor (HIGH->LOW transition), entering unclamp state");
        STOP_ALL_MOTORS();              // 立即停止所有电机，精确定位
        enter_state(PAPER_UNCLAMP_FEED); // 进入松开状态，准备夹紧机构操作
    } else if (ctrl->step_counter > PAPER_FEED_TIMEOUT_STEPS) {
        // 超时保护：至少2900步（约36.25mm）内未检测到纸张
        // 考虑进纸器电机到传感器的距离3cm（2400步）+ 安全余量500步
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
 * 按照文档步骤5a和5b：
 * 步骤5a：夹紧电机正转1.5cm松开夹具（1200步）
 * 步骤5b：送纸器电机正转4cm送纸（3200步）
 * 注意：拾落电机松开后，面板电机无法带动纸张，必须使用送纸器电机
 */
void handle_unclamp_feed_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    HANDLE_NULL_POINTER(ctrl, "handle_unclamp_feed_state");
    
    motor_timing_t* clamp_timing = get_clamp_motor_timing();
    motor_timing_t* feed_timing = get_feed_motor_timing();
    if (!clamp_timing || !feed_timing) return;
    
    CHECK_STATE_SAFETY(PAPER_1_5CM_STEPS + PAPER_4CM_STEPS + 100, "UNCLAMP_FEED");
    
    if (ctrl->step_counter < PAPER_1_5CM_STEPS) {
        // 步骤5a：夹紧电机正转1.5cm松开夹具（拾落电机松开，为新纸张准备空间）
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
    } else if (ctrl->step_counter < PAPER_1_5CM_STEPS + PAPER_4CM_STEPS) {
        // 步骤5b：送纸器电机正转4cm送纸（拾落电机松开后，面板电机无法带动纸张，必须使用送纸器电机）
        if (nonblocking_feed_step(true)) {
            ctrl->step_counter++;
            
            // 每100步报告一次进度
            if ((ctrl->step_counter - PAPER_1_5CM_STEPS) % 100 == 0) {
                float progress_mm = steps_to_mm(ctrl->step_counter - PAPER_1_5CM_STEPS, DEFAULT_FEED_STEPS_PER_MM);
                LOG_PROGRESS("Feed motor transporting: %.1fmm", progress_mm);
            }
            
            if (ctrl->step_counter == PAPER_1_5CM_STEPS + PAPER_4CM_STEPS) {
                LOG_MSG("Feed motor transport complete (4cm forward)");
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
 * 直到纸张脱离传感器位置，然后进纸器电机停止，面板电机单独运行3cm
 * 
 * @note 完成条件：
 * 1. 步进计数器 >= A4纸张长度步数（PAPER_EJECT_STEPS）
 * 2. 传感器状态为无纸（!paper_sensor_state），表示纸张已脱离传感器位置
 * 3. 纸张脱离传感器后，进纸器电机停止，面板电机单独运行3cm
 */
void handle_full_feed_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    motor_timing_t* feed_timing = get_feed_motor_timing();
    motor_timing_t* panel_timing = get_panel_motor_timing();
    if (!ctrl || !feed_timing || !panel_timing) return;
    
    // 直接获取静态标志，用于跟踪面板电机单独运行阶段
    bool pos_init_val, eject_detected_val, reverse_complete_val;
    get_static_flags(&pos_init_val, &eject_detected_val, &reverse_complete_val);
    bool* panel_alone_complete = &reverse_complete_val;  // 复用reverse_complete标志表示面板单独运行完成
    
    CHECK_STATE_SAFETY(25000 + PAPER_3CM_STEPS + 100, "FULL_FEED");
    
    // 阶段1：同步运行阶段 - 两个电机同步运行，直到纸张脱离传感器
    if (!reverse_complete_val) {
        // 设置两个电机同步运行，相同步进间隔
        feed_timing->step_interval = 1000;
        panel_timing->step_interval = 1000;
        
        // 检查完成条件：
        // 1. 已进给A4长度且纸张已脱离传感器（正常情况）
        // 2. 或者纸张提前脱离传感器（提前完成，也需要至少进给80%的距离）
        uint32_t min_feed_steps = (uint32_t)(PAPER_EJECT_STEPS * 0.8f);  // 至少进给80%
        if ((ctrl->step_counter >= PAPER_EJECT_STEPS && !ctrl->paper_sensor_state) ||
            (!ctrl->paper_sensor_state && ctrl->step_counter >= min_feed_steps)) {
            LOG_MSG("Full feed sync phase complete, paper detached from sensor. Stopping feed motor, panel motor continues 3cm");
            // 停止进纸器电机
            feed_timing->last_step_time = 0;  // 禁用进纸器电机
            // 重置计数器，准备面板电机单独运行
            ctrl->step_counter = 0;
            *panel_alone_complete = true;
            set_static_flags(pos_init_val, eject_detected_val, true);  // 保存标志
        } else {
            // 只有当两个电机都到了应该步进的时间时，才同步执行步进
            // 安全处理millis()溢出（虽然步进间隔很短，溢出可能性极低，但为了安全仍处理）
            uint32_t current_time = millis();
            uint32_t feed_elapsed = (current_time >= feed_timing->last_step_time) ? 
                                    (current_time - feed_timing->last_step_time) :
                                    (UINT32_MAX - feed_timing->last_step_time + current_time + 1);
            uint32_t panel_elapsed = (current_time >= panel_timing->last_step_time) ? 
                                     (current_time - panel_timing->last_step_time) :
                                     (UINT32_MAX - panel_timing->last_step_time + current_time + 1);
            
            if (feed_elapsed >= feed_timing->step_interval && 
                panel_elapsed >= panel_timing->step_interval) {
                
                // 同步步进两个电机
                motor_step_control(BIT_FEED_MOTOR_STEP, BIT_FEED_MOTOR_DIR, true, feed_timing->step_interval);
                motor_step_control(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true, panel_timing->step_interval);
                
                // 更新时序和计数器
                feed_timing->last_step_time = current_time;
                panel_timing->last_step_time = current_time;
                ctrl->step_counter++;  // 每次两个电机同步步进时，计数器加1
                
                if (ctrl->step_counter % 1000 == 0) {
                    float progress = (float)ctrl->step_counter / PAPER_EJECT_STEPS * 100.0;
                    LOG_PROGRESS("Full feed sync progress: %.1f%% (Step %lu/%lu)", 
                               progress, ctrl->step_counter, PAPER_EJECT_STEPS);
                }
            }
        }
    }
    // 阶段2：面板电机单独运行阶段 - 面板电机单独运行3cm
    else if (reverse_complete_val && ctrl->step_counter < PAPER_3CM_STEPS) {
        // 面板电机单独运行，进纸器电机已停止
        panel_timing->step_interval = 1000;  // 保持1kHz速度
        
        if (nonblocking_panel_step(true)) {
            ctrl->step_counter++;
            
            if (ctrl->step_counter % 200 == 0) {
                float progress_mm = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
                LOG_PROGRESS("Panel motor alone: %.1fmm", progress_mm);
            }
            
            if (ctrl->step_counter >= PAPER_3CM_STEPS) {
                LOG_MSG("Panel motor 3cm movement complete, entering reposition state");
                *panel_alone_complete = false;
                set_static_flags(pos_init_val, eject_detected_val, false);  // 重置标志
                STOP_ALL_MOTORS();
                enter_state(PAPER_REPOSITION);
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
                
                // 立即保存标志到全局静态变量，避免状态丢失
                set_static_flags(pos_init_val, eject_detected_val, true);  // 保存reverse_complete=true
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
    // 边界情况处理 - reverse_complete=true但step_counter已达到或超过最大值
    // 这通常不应该发生（当达到PAPER_3CM_STEPS时会转换到COMPLETE状态），
    // 但如果发生，强制进入COMPLETE状态以避免状态卡住
    else {
        LOG_WARNING_F("REPOSITION state: positioning complete but counter exceeded (%lu/%lu), forcing completion", 
                     ctrl->step_counter, PAPER_3CM_STEPS);
        reverse_complete_val = false;
        *reverse_complete = false;
        set_static_flags(pos_init_val, eject_detected_val, false);  // 保存reverse_complete=false
        
        // 停止所有电机，确保状态转换时电机已停止
        STOP_ALL_MOTORS();
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
    // 安全处理millis()溢出（虽然2秒很短，溢出可能性极低，但为了安全仍处理）
    if (ctrl->emergency_stop) {
        uint32_t elapsed = (current_time >= ctrl->state_timer) ? 
                          (current_time - ctrl->state_timer) :
                          (UINT32_MAX - ctrl->state_timer + current_time + 1);
        if (elapsed > 2000) {
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
        // 安全处理millis()溢出（虽然2秒很短，溢出可能性极低，但为了代码一致性仍处理）
        uint32_t current_time = millis();
        uint32_t press_duration;
        if (current_time >= button_press_time) {
            press_duration = current_time - button_press_time;
        } else {
            // 处理millis()溢出情况（49天后）
            press_duration = (UINT32_MAX - button_press_time) + current_time + 1;
        }
        
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