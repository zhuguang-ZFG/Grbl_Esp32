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
 * @brief 状态转换验证
 */
bool is_valid_transition(paper_change_state_t from_state, paper_change_state_t to_state) {
    if (from_state < PAPER_IDLE || from_state > PAPER_ERROR ||
        to_state < PAPER_IDLE || to_state > PAPER_ERROR) {
        return false;
    }
    
    return state_transition_matrix[from_state][to_state];
}

/**
 * @brief 安全状态转换
 */
void enter_state(paper_change_state_t new_state) {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    if (new_state < PAPER_IDLE || new_state > PAPER_ERROR) {
        LOG_ERROR("Invalid state value");
        return;
    }
    
    paper_change_state_t old_state = ctrl->state;
    if (!is_valid_transition(old_state, new_state)) {
        LOG_ERROR_F("Invalid state transition %s->%s", 
                   GET_STATE_NAME(old_state), GET_STATE_NAME(new_state));
        if (new_state != PAPER_ERROR) {
            enter_state(PAPER_ERROR);
        }
        return;
    }
    
    // 错误状态特殊处理
    if (old_state == PAPER_ERROR && new_state != PAPER_ERROR) {
        LOG_MSG_F("Recovering from ERROR state to %s", GET_STATE_NAME(new_state));
        ctrl->emergency_stop = false;
        STOP_ALL_MOTORS();
    }
    
    // 重置静态标志
    bool pos_init_val, eject_detected_val, reverse_complete_val;
    get_static_flags(&pos_init_val, &eject_detected_val, &reverse_complete_val);
    bool* pos_init = &pos_init_val;
    bool* eject_detected = &eject_detected_val;
    bool* reverse_complete = &reverse_complete_val;
    
    if (new_state == PAPER_REPOSITION) {
        *pos_init = false;
    }
    
    if (new_state != PAPER_EJECTING) {
        *eject_detected = false;
    }
    
    if (new_state != PAPER_REPOSITION) {
        *reverse_complete = false;
    }
    
    set_static_flags(*pos_init, *eject_detected, *reverse_complete);
    
    // 更新状态
    ctrl->state = new_state;
    ctrl->state_timer = millis();
    ctrl->step_counter = 0;
    
    // 初始化电机时序
    init_motor_timing();
    
    LOG_MSG_F("State %s->%s", GET_STATE_NAME(old_state), GET_STATE_NAME(new_state));
}

/**
 * @brief 状态机更新函数
 */
void paper_state_machine_update() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    switch (ctrl->state) {
        case PAPER_IDLE:
            // 空闲状态，无操作
            break;
            
        case PAPER_PRE_CHECK:
            handle_pre_check_state();
            break;
            
        case PAPER_EJECTING:
            handle_ejecting_state();
            break;
            
        case PAPER_FEEDING:
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
 */
void handle_pre_check_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    CHECK_STATE_SAFETY(PAPER_PRE_CHECK_STEPS + 50, "PRE_CHECK");
    
    if (ctrl->step_counter < PAPER_PRE_CHECK_STEPS) {
        if (nonblocking_panel_step(false)) {
            ctrl->step_counter++;
        }
    } else {
        if (ctrl->paper_sensor_state) {
            LOG_MSG("Paper detected on panel, starting ejection");
            enter_state(PAPER_EJECTING);
        } else {
            LOG_MSG("No paper on panel, starting feeding");
            enter_state(PAPER_FEEDING);
        }
    }
}

/**
 * @brief 处理出纸状态逻辑
 * 按照文档实现：面板电机反转检测纸张位置，然后快速正转A4+3-5cm送出纸张
 */
void handle_ejecting_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    bool* pos_init, *eject_detected, *reverse_complete;
    get_static_flags(pos_init, eject_detected, reverse_complete);
    if (!ctrl || !eject_detected) return;
    
    CHECK_STATE_SAFETY(PAPER_MAX_REVERSE_STEPS + PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS + 200, "EJECTING");
    
    // 步骤1：检测纸张位置 - 面板电机反转，直到传感器感应到纸张后停止
    if (!*eject_detected && ctrl->step_counter < PAPER_MAX_REVERSE_STEPS) {
        if (nonblocking_panel_step(false)) {
            ctrl->step_counter++;
            
            if (ctrl->paper_sensor_state && !ctrl->last_paper_sensor_state) {
                LOG_MSG("Paper edge detected in reverse, starting forward ejection");
                ctrl->step_counter = 0;
                *eject_detected = true;
                
                // 设置快速正转速度
                motor_timing_t* panel_timing = get_panel_motor_timing();
                if (panel_timing) {
                    panel_timing->step_interval = 500; // 快速正转（2kHz）
                }
            }
            
            if (ctrl->step_counter % 200 == 0) {
                float reverse_mm = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
                LOG_PROGRESS("Panel motor reverse searching: %.1fmm", reverse_mm);
            }
        }
    } 
    // 步骤2：完全出纸 - 面板电机快速正转，运行距离：A4纸张长度 + 3-5cm
    else if (*eject_detected && ctrl->step_counter < (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS)) {
        if (nonblocking_panel_step(true)) {
            ctrl->step_counter++;
            
            if (ctrl->step_counter % 1000 == 0) {
                float progress = (float)ctrl->step_counter / (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS) * 100.0;
                LOG_PROGRESS("Ejection progress: %.1f%%", progress);
            }
            
            if (ctrl->step_counter >= (PAPER_EJECT_STEPS + PAPER_3TO5CM_STEPS)) {
                LOG_MSG("Paper ejection complete (A4 + 3-5cm forward)");
                *eject_detected = false;
                enter_state(PAPER_FEEDING);
            }
        }
    } 
    // 步骤3：如果反转没有检测到纸张，直接进入进纸状态
    else if (!*eject_detected && ctrl->step_counter >= PAPER_MAX_REVERSE_STEPS) {
        LOG_MSG("No paper detected in reverse, proceeding to feed");
        *eject_detected = false;
        enter_state(PAPER_FEEDING);
    }
}

/**
 * @brief 处理进纸状态逻辑
 * 按照文档步骤1：启动进纸 - 进纸电机开始运动，等待纸张感应器检测到纸张
 */
void handle_feeding_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    CHECK_STATE_SAFETY(1000, "FEEDING");
    
    // 步骤1：启动进纸 - 等待纸张感应器检测到纸张
    if (ctrl->paper_sensor_state && !ctrl->last_paper_sensor_state) {
        LOG_MSG("Paper detected by sensor, entering unclamp state");
        STOP_ALL_MOTORS();
        enter_state(PAPER_UNCLAMP_FEED);
    } else if (ctrl->step_counter > 500) {
        handle_error(ERROR_SENSOR_TIMEOUT, "Feed operation timeout - no paper detected", PAPER_FEEDING);
    } else {
        // 进纸电机正转，等待检测到纸张
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
    motor_timing_t* clamp_timing = get_clamp_motor_timing();
    motor_timing_t* panel_timing = get_panel_motor_timing();
    if (!ctrl || !clamp_timing || !panel_timing) return;
    
    CHECK_STATE_SAFETY(PAPER_1_5CM_STEPS + PAPER_3_5CM_STEPS + 100, "UNCLAMP_FEED");
    
    if (ctrl->step_counter < PAPER_1_5CM_STEPS) {
        // 步骤2a：夹紧电机正转1.5cm松开夹子（初始为夹紧态）
        if (nonblocking_clamp_step(true)) {
            ctrl->step_counter++;
            
            if (ctrl->step_counter % 50 == 0) {
                float progress_mm = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
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
                float progress_mm = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
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
 */
void handle_full_feed_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    motor_timing_t* feed_timing = get_feed_motor_timing();
    motor_timing_t* panel_timing = get_panel_motor_timing();
    if (!ctrl || !feed_timing || !panel_timing) return;
    
    CHECK_STATE_SAFETY(25000, "FULL_FEED");
    
    feed_timing->step_interval = 1000;
    panel_timing->step_interval = 1000;
    
    if (ctrl->step_counter >= PAPER_EJECT_STEPS && !ctrl->paper_sensor_state) {
        LOG_MSG("Full feed complete, paper detached");
        motor_step_control(BIT_FEED_MOTOR_STEP, BIT_FEED_MOTOR_DIR, false, 0);
        enter_state(PAPER_REPOSITION);
    } else {
        if (millis() - feed_timing->last_step_time >= feed_timing->step_interval &&
            millis() - panel_timing->last_step_time >= panel_timing->step_interval) {
            
            motor_step_control(BIT_FEED_MOTOR_STEP, BIT_FEED_MOTOR_DIR, true, feed_timing->step_interval);
            motor_step_control(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true, panel_timing->step_interval);
            
            feed_timing->last_step_time = millis();
            panel_timing->last_step_time = millis();
            ctrl->step_counter++;
            
            if (ctrl->step_counter % 1000 == 0) {
                float progress = (float)ctrl->step_counter / PAPER_EJECT_STEPS * 100.0;
                LOG_PROGRESS("Full feed progress: %.1f%%", progress);
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
    bool* pos_init, *eject_detected, *reverse_complete;
    get_static_flags(pos_init, eject_detected, reverse_complete);
    if (!ctrl || !panel_timing || !reverse_complete) return;
    
    CHECK_STATE_SAFETY(PAPER_MAX_REVERSE_STEPS + PAPER_3CM_STEPS + 100, "REPOSITION");
    
    if (!*reverse_complete && ctrl->step_counter < PAPER_MAX_REVERSE_STEPS) {
        // 步骤4a：面板电机反转，直到传感器再次感应到纸张
        if (nonblocking_panel_step(false)) {
            ctrl->step_counter++;
            
            if (ctrl->paper_sensor_state && !ctrl->last_paper_sensor_state) {
                LOG_MSG("Paper edge detected in reverse, starting 3cm forward positioning");
                ctrl->step_counter = 0;
                *reverse_complete = true;
            }
            
            if (ctrl->step_counter % 200 == 0) {
                float reverse_mm = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
                LOG_PROGRESS("Panel motor reversing: %.1fmm", reverse_mm);
            }
        }
    } else if (*reverse_complete && ctrl->step_counter < PAPER_3CM_STEPS) {
        // 步骤4b：面板电机正转3cm左右（关键步骤，确保纸张位置正确）
        if (nonblocking_panel_step(true)) {
            ctrl->step_counter++;
            
            if (ctrl->step_counter % 50 == 0) {
                float position_mm = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
                LOG_PROGRESS("Critical positioning: %.1fmm", position_mm);
            }
            
            if (ctrl->step_counter >= PAPER_3CM_STEPS) {
                *reverse_complete = false;
                float final_position = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
                LOG_MSG_F("Paper positioning complete at %.1fmm (critical 3cm forward)", final_position);
                enter_state(PAPER_COMPLETE);
            }
        }
    } else if (!*reverse_complete && ctrl->step_counter >= PAPER_MAX_REVERSE_STEPS) {
        LOG_MSG("No paper detected in reposition, completing without positioning");
        *reverse_complete = false;
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
    
    if (ctrl->emergency_stop) {
        if (current_time - ctrl->state_timer > 2000) {
            LOG_MSG("Emergency stop timeout, auto-resuming");
            ctrl->emergency_stop = false;
            enter_state(PAPER_IDLE);
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

#endif // AUTO_PAPER_CHANGE_ENABLE