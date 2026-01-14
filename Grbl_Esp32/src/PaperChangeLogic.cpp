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
    bool* pos_init, *eject_detected, *reverse_complete;
    get_static_flags(pos_init, eject_detected, reverse_complete);
    
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
 */
void handle_ejecting_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    bool* eject_detected;
    get_static_flags(nullptr, eject_detected, nullptr);
    if (!ctrl || !eject_detected) return;
    
    CHECK_STATE_SAFETY(PAPER_EJECT_STEPS + 2000, "EJECTING");
    
    if (!*eject_detected && ctrl->step_counter < PAPER_MAX_REVERSE_STEPS) {
        if (nonblocking_panel_step(false)) {
            ctrl->step_counter++;
            
            if (ctrl->paper_sensor_state && !ctrl->last_paper_sensor_state) {
                LOG_MSG("Paper edge detected, starting forward ejection");
                ctrl->step_counter = 0;
                *eject_detected = true;
            }
        }
    } else if (*eject_detected && ctrl->step_counter < (PAPER_EJECT_STEPS + PAPER_5CM_STEPS)) {
        if (nonblocking_panel_step(true)) {
            ctrl->step_counter++;
            
            if (ctrl->step_counter % 1000 == 0) {
                float progress = (float)ctrl->step_counter / (PAPER_EJECT_STEPS + PAPER_5CM_STEPS) * 100.0;
                LOG_PROGRESS("Ejection progress: %.1f%%", progress);
            }
            
            if (ctrl->step_counter >= (PAPER_EJECT_STEPS + PAPER_5CM_STEPS)) {
                LOG_MSG("Paper ejection complete");
                enter_state(PAPER_FEEDING);
            }
        }
    } else if (!*eject_detected && ctrl->step_counter >= PAPER_MAX_REVERSE_STEPS) {
        LOG_MSG("No paper detected in reverse, proceeding to feed");
        *eject_detected = false;
        enter_state(PAPER_FEEDING);
    }
}

/**
 * @brief 处理进纸状态逻辑
 */
void handle_feeding_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    CHECK_STATE_SAFETY(1000, "FEEDING");
    
    if (ctrl->paper_sensor_state && !ctrl->last_paper_sensor_state) {
        LOG_MSG("Paper detected, entering unclamp state");
        STOP_ALL_MOTORS();
        enter_state(PAPER_UNCLAMP_FEED);
    } else if (ctrl->step_counter > 500) {
        handle_error(ERROR_SENSOR_TIMEOUT, "Feed operation timeout", PAPER_FEEDING);
    } else {
        if (nonblocking_feed_step(true)) {
            ctrl->step_counter++;
        }
    }
}

/**
 * @brief 处理松开进纸状态逻辑
 */
void handle_unclamp_feed_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    motor_timing_t* clamp_timing = get_clamp_motor_timing();
    motor_timing_t* panel_timing = get_panel_motor_timing();
    if (!ctrl || !clamp_timing || !panel_timing) return;
    
    CHECK_STATE_SAFETY(500, "UNCLAMP_FEED");
    
    if (ctrl->step_counter < 30) {
        // 松开夹具
        if (millis() - clamp_timing->last_step_time >= clamp_timing->step_interval) {
            stop_all_motors();
            clamp_timing->last_step_time = millis();
            ctrl->step_counter++;
        }
    } else if (ctrl->step_counter < PAPER_5CM_STEPS + 30) {
        // 面板电机运行5cm
        if (nonblocking_panel_step(true)) {
            ctrl->step_counter++;
        }
    } else {
        LOG_MSG("Unclamp and 5cm transport complete");
        enter_state(PAPER_CLAMP_FEED);
    }
}

/**
 * @brief 处理夹紧进纸状态逻辑
 */
void handle_clamp_feed_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    motor_timing_t* clamp_timing = get_clamp_motor_timing();
    if (!ctrl || !clamp_timing) return;
    
    CHECK_STATE_SAFETY(100, "CLAMP_FEED");
    
    if (ctrl->step_counter < 20) {
        if (millis() - clamp_timing->last_step_time >= clamp_timing->step_interval) {
            motor_step_control(BIT_PAPER_CLAMP_STEP, BIT_PAPER_CLAMP_DIR, true, clamp_timing->step_interval);
            clamp_timing->last_step_time = millis();
            ctrl->step_counter++;
        }
    } else {
        LOG_MSG("Clamping complete");
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
 */
void handle_reposition_state() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    motor_timing_t* panel_timing = get_panel_motor_timing();
    bool* reverse_complete;
    get_static_flags(nullptr, nullptr, reverse_complete);
    if (!ctrl || !panel_timing || !reverse_complete) return;
    
    CHECK_STATE_SAFETY(2000, "REPOSITION");
    
    if (!*reverse_complete && ctrl->step_counter < PAPER_MAX_REVERSE_STEPS) {
        if (millis() - panel_timing->last_step_time >= panel_timing->step_interval) {
            motor_step_control(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, false, panel_timing->step_interval);
            panel_timing->last_step_time = millis();
            ctrl->step_counter++;
            
            if (ctrl->paper_sensor_state && !ctrl->last_paper_sensor_state) {
                LOG_MSG("Paper edge detected in reverse, positioning");
                ctrl->step_counter = 0;
                *reverse_complete = true;
            }
        }
    } else if (*reverse_complete && ctrl->step_counter < PAPER_CRITICAL_POSITION_STEPS) {
        if (nonblocking_panel_step(true)) {
            ctrl->step_counter++;
            
            if (ctrl->step_counter % 50 == 0) {
                float position_mm = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
                LOG_PROGRESS("Critical positioning: %.1fmm", position_mm);
            }
            
            if (ctrl->step_counter >= PAPER_CRITICAL_POSITION_STEPS) {
                *reverse_complete = false;
                float final_position = steps_to_mm(ctrl->step_counter, DEFAULT_PANEL_STEPS_PER_MM);
                LOG_MSG_F("Paper positioning complete at %.1fmm", final_position);
                enter_state(PAPER_COMPLETE);
            }
        }
    } else if (!*reverse_complete && ctrl->step_counter >= PAPER_MAX_REVERSE_STEPS) {
        LOG_MSG("No paper detected in reposition, completing");
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