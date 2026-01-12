/*
  PaperChange.cpp - Complete paper change system implementation
  Part of Grbl_ESP32

  Implements full paper change workflow:
  1. Writing complete -> Panel motor ejects finished paper
  2. Feed motor starts new paper
  3. Sensor detection -> Clamp motor engages
  4. Paper leaves sensor -> All motors stop
  5. Panel motor: forward 32 steps, back 2 steps
  6. Resume writing
  
  Also implements one-click paper change functionality
  
  Copyright (c) 2024 Grbl_ESP32
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE
#include "PaperChange.h"
#include "SmartM0.h"
#include "Report.h"

// === Paper Change State Machine ===
typedef enum {
    PAPER_IDLE,              // 空闲状态
    PAPER_EJECTING,          // 出纸状态 - 面板电机送出写完的纸
    PAPER_FEEDING,           // 进纸状态 - 进纸器送入新纸
    PAPER_DETECTING,         // 检测状态 - 等待传感器触发
    PAPER_CLAMPING,          // 夹纸状态 - 拾落电机夹纸
    PAPER_RELEASING,        // 释放状态 - 纸张离开传感器，释放夹具
    PAPER_POSITIONING,       // 定位状态 - 面板电机微调
    PAPER_COMPLETE,          // 完成状态 - 换纸完成
    PAPER_ERROR             // 错误状态
} paper_change_state_t;

// === Paper Change Control Structure ===
typedef struct {
    paper_change_state_t state = PAPER_IDLE;
    uint32_t state_timer = 0;
    uint32_t step_counter = 0;
    bool paper_sensor_state = false;
    bool last_paper_sensor_state = false;
    bool one_click_active = false;
    uint8_t hc595_output = 0;  // Current HC595 output state
    bool emergency_stop = false;
} paper_change_ctrl_t;

static paper_change_ctrl_t paper_ctrl;

// === Motor Control Functions ===
static void hc595_write(uint8_t data) {
    // Write data to 74HC595D shift register
    digitalWrite(HC595_LATCH_PIN, LOW);
    
    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(HC595_CLOCK_PIN, LOW);
        digitalWrite(HC595_DATA_PIN, (data & (0x80 >> i)) ? HIGH : LOW);
        delayMicroseconds(1);
        digitalWrite(HC595_CLOCK_PIN, HIGH);
        delayMicroseconds(1);
    }
    
    digitalWrite(HC595_LATCH_PIN, HIGH);
    delayMicroseconds(HC595_UPDATE_DELAY_US);
    
    paper_ctrl.hc595_output = data;
}

static void set_motor_step(uint8_t motor_bit, bool enable) {
    uint8_t new_output = paper_ctrl.hc595_output;
    
    if (enable) {
        new_output |= (1 << motor_bit);
    } else {
        new_output &= ~(1 << motor_bit);
    }
    
    if (new_output != paper_ctrl.hc595_output) {
        hc595_write(new_output);
    }
}

static void set_motor_dir(uint8_t motor_bit, bool forward) {
    uint8_t new_output = paper_ctrl.hc595_output;
    
    if (forward) {
        new_output &= ~(1 << motor_bit);  // Forward direction
    } else {
        new_output |= (1 << motor_bit);   // Reverse direction
    }
    
    if (new_output != paper_ctrl.hc595_output) {
        hc595_write(new_output);
    }
}

// === Motor Step Generation ===
static void generate_motor_step(uint8_t step_bit, uint8_t dir_bit, bool forward) {
    set_motor_dir(dir_bit, forward);
    delayMicroseconds(10);
    set_motor_step(step_bit, HIGH);
    delayMicroseconds(DEFAULT_STEP_PULSE_MICROSECONDS);
    set_motor_step(step_bit, LOW);
    delayMicroseconds(10);
}

// === Paper Sensor Reading ===
static bool read_paper_sensor() {
    return digitalRead(PAPER_SENSOR_PIN) == HIGH;  // Assuming HIGH when paper detected
}

// === State Machine Functions ===
static void enter_state(paper_change_state_t new_state) {
    paper_ctrl.state = new_state;
    paper_ctrl.state_timer = millis();
    paper_ctrl.step_counter = 0;
    
    // Log state transition
    const char* state_names[] = {
        "IDLE", "EJECTING", "FEEDING", "DETECTING", 
        "CLAMPING", "RELEASING", "POSITIONING", "COMPLETE", "ERROR"
    };
    
    if (new_state < sizeof(state_names)/sizeof(state_names[0])) {
        grbl_sendf(CLIENT_ALL, "[MSG: Paper change state: %s]\r\n", state_names[new_state]);
    }
}

// === Paper Change Implementation ===

// Start automatic paper change sequence
void paper_change_start() {
    if (paper_ctrl.state != PAPER_IDLE) {
        grbl_sendf(CLIENT_ALL, "[MSG: Paper change already in progress]\r\n");
        return;
    }
    
    grbl_sendf(CLIENT_ALL, "[MSG: Starting automatic paper change]\r\n");
    enter_state(PAPER_EJECTING);
}

// Manual one-click paper change
void paper_change_one_click() {
    if (paper_ctrl.state != PAPER_IDLE) {
        grbl_sendf(CLIENT_ALL, "[MSG: Paper change busy, cannot start manual]\r\n");
        return;
    }
    
    grbl_sendf(CLIENT_ALL, "[MSG: Starting manual paper change]\r\n");
    paper_ctrl.one_click_active = true;
    enter_state(PAPER_EJECTING);
}

// Emergency stop paper change
void paper_change_stop() {
    grbl_sendf(CLIENT_ALL, "[MSG: Emergency stop paper change]\r\n");
    paper_ctrl.emergency_stop = true;
    
    // Stop all motors
    hc595_write(0);
    
    enter_state(PAPER_ERROR);
}

// Check one-click button press
static void check_button_press() {
    static bool last_button_state = false;
    static uint32_t button_press_time = 0;
    
    // Read button state from HC595 Q0 (bit 0)
    bool button_pressed = (paper_ctrl.hc595_output & (1 << BIT_PAPER_BUTTON)) != 0;
    
    if (button_pressed && !last_button_state) {
        // Button pressed - start timer
        button_press_time = millis();
    } else if (!button_pressed && last_button_state) {
        // Button released - check press duration
        uint32_t press_duration = millis() - button_press_time;
        
        if (press_duration < PAPER_BUTTON_LONG_PRESS_MS) {
            // Short press - start manual paper change
            paper_change_one_click();
        } else {
            // Long press - emergency stop
            smart_m0_emergency_stop();
            paper_change_stop();
        }
    }
    
    last_button_state = button_pressed;
}

// Update paper change state machine
void paper_change_update() {
    // Check for emergency stop
    if (paper_ctrl.emergency_stop) {
        return;
    }
    
    // Check button press
    check_button_press();
    
    // Update sensor state
    paper_ctrl.last_paper_sensor_state = paper_ctrl.paper_sensor_state;
    paper_ctrl.paper_sensor_state = read_paper_sensor();
    
    uint32_t current_time = millis();
    
    switch (paper_ctrl.state) {
        case PAPER_IDLE:
            // Check for paper sensor changes in idle (for monitoring)
            break;
            
        case PAPER_EJECTING:
            // Eject finished paper using panel motor
            if (paper_ctrl.step_counter < 100) {  // Eject for 100 steps
                generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true);
                paper_ctrl.step_counter++;
                delayMicroseconds(2000);  // 2ms delay between steps
            } else {
                enter_state(PAPER_FEEDING);
            }
            break;
            
        case PAPER_FEEDING:
            // Feed new paper and detect sensor
            set_motor_dir(BIT_FEED_MOTOR_DIR, true);  // Forward direction
            set_motor_step(BIT_FEED_MOTOR_STEP, HIGH);
            delayMicroseconds(DEFAULT_STEP_PULSE_MICROSECONDS);
            set_motor_step(BIT_FEED_MOTOR_STEP, LOW);
            delayMicroseconds(2000);  // 2ms between steps
            
            paper_ctrl.step_counter++;
            
            // Check for paper detection
            if (paper_ctrl.paper_sensor_state && !paper_ctrl.last_paper_sensor_state) {
                grbl_sendf(CLIENT_ALL, "[MSG: Paper detected, engaging clamp]\r\n");
                // Stop feed motor
                set_motor_step(BIT_FEED_MOTOR_STEP, LOW);
                enter_state(PAPER_CLAMPING);
            } else if (paper_ctrl.step_counter > 500) {
                // Timeout - no paper detected
                grbl_sendf(CLIENT_ALL, "[MSG: Feed timeout, no paper detected]\r\n");
                set_motor_step(BIT_FEED_MOTOR_STEP, LOW);
                enter_state(PAPER_ERROR);
            }
            break;
            
        case PAPER_DETECTING:
            // This state might not be needed since we detect in FEEDING state
            enter_state(PAPER_CLAMPING);
            break;
            
        case PAPER_CLAMPING:
            // Engage clamp motor - both clamp and panel motors active
            generate_motor_step(BIT_PAPER_CLAMP_STEP, BIT_PAPER_CLAMP_DIR, true);
            generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true);
            
            paper_ctrl.step_counter++;
            
            if (paper_ctrl.step_counter >= 20) {  // Clamp for 20 steps
                grbl_sendf(CLIENT_ALL, "[MSG: Clamp engaged, monitoring sensor]\r\n");
                enter_state(PAPER_RELEASING);
            }
            break;
            
        case PAPER_RELEASING:
            // Wait for paper to leave sensor, then stop all motors
            if (!paper_ctrl.paper_sensor_state && paper_ctrl.last_paper_sensor_state) {
                grbl_sendf(CLIENT_ALL, "[MSG: Paper left sensor, stopping all motors]\r\n");
                
                // Stop all motors
                hc595_write(0);
                
                // Start positioning after 1 second delay
                paper_ctrl.state_timer = current_time + 1000;
                enter_state(PAPER_POSITIONING);
            } else if (current_time - paper_ctrl.state_timer > 10000) {
                // 10 second timeout
                grbl_sendf(CLIENT_ALL, "[MSG: Release timeout]\r\n");
                enter_state(PAPER_ERROR);
            }
            break;
            
        case PAPER_POSITIONING:
            // Panel motor fine positioning: forward 32 steps, back 2 steps
            if (current_time >= paper_ctrl.state_timer) {
                if (paper_ctrl.step_counter < 32) {
                    // Forward 32 steps
                    generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true);
                    paper_ctrl.step_counter++;
                    delayMicroseconds(3000);  // 3ms delay
                } else if (paper_ctrl.step_counter < 34) {
                    // Back 2 steps
                    generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, false);
                    paper_ctrl.step_counter++;
                    delayMicroseconds(3000);  // 3ms delay
                } else {
                    // Positioning complete
                    hc595_write(0);  // Stop all motors
                    enter_state(PAPER_COMPLETE);
                }
            }
            break;
            
        case PAPER_COMPLETE:
            // Paper change complete
            grbl_sendf(CLIENT_ALL, "[MSG: Paper change completed successfully]\r\n");
            
            // Notify smart M0 system
            m0_paper_change_complete();
            
            // Reset to idle
            paper_ctrl.one_click_active = false;
            enter_state(PAPER_IDLE);
            break;
            
        case PAPER_ERROR:
            // Error state - wait for manual reset
            if (paper_ctrl.emergency_stop) {
                // Reset emergency stop after 2 seconds
                if (current_time - paper_ctrl.state_timer > 2000) {
                    paper_ctrl.emergency_stop = false;
                    enter_state(PAPER_IDLE);
                }
            }
            break;
    }
}

// Get paper change status
bool paper_change_is_active() {
    return (paper_ctrl.state != PAPER_IDLE && paper_ctrl.state != PAPER_ERROR);
}

// Get paper change state
const char* paper_change_get_state() {
    static const char* state_names[] = {
        "IDLE", "EJECTING", "FEEDING", "DETECTING", 
        "CLAMPING", "RELEASING", "POSITIONING", "COMPLETE", "ERROR"
    };
    
    if (paper_ctrl.state < sizeof(state_names)/sizeof(state_names[0])) {
        return state_names[paper_ctrl.state];
    }
    return "UNKNOWN";
}

// Reset paper change system
void paper_change_reset() {
    hc595_write(0);  // Stop all motors
    memset(&paper_ctrl, 0, sizeof(paper_change_ctrl_t));
    enter_state(PAPER_IDLE);
    grbl_sendf(CLIENT_ALL, "[MSG: Paper change system reset]\r\n");
}

// Initialize paper change system
void paper_change_init() {
    // Configure GPIO pins
    pinMode(HC595_DATA_PIN, OUTPUT);
    pinMode(HC595_CLOCK_PIN, OUTPUT);
    pinMode(HC595_LATCH_PIN, OUTPUT);
    pinMode(PAPER_SENSOR_PIN, INPUT_PULLDOWN);
    
    // Initialize HC595 output to all off
    hc595_write(0);
    
    grbl_sendf(CLIENT_ALL, "[MSG: Paper change system initialized]\r\n");
}

#endif // AUTO_PAPER_CHANGE_ENABLE