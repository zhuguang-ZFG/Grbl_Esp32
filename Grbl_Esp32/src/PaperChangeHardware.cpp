/*
  PaperChangeHardware.cpp - 换纸系统硬件驱动实现
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE
#include "PaperChangeConfig.h"
#include "PaperChangeTypes.h"
#include "PaperChangeHardware.h"
#include "PaperChangeUtils.h"

// ================================================================================
// HC595移位寄存器实现
// ================================================================================

/**
 * @brief HC595移位寄存器初始化
 */
void hc595_init() {
    // 引脚冲突检测
    #ifdef X_STEP_PIN
    if (HC595_LATCH_PIN == X_STEP_PIN) {
        grbl_sendf(CLIENT_ALL, "[MSG: Error: HC595_LATCH_PIN (%d) conflicts with X_STEP_PIN (%d)]\n", HC595_LATCH_PIN, X_STEP_PIN);
        delay(1000);
        return;
    }
    #endif
    
    #ifdef STEPPERS_DISABLE_PIN
    if (HC595_LATCH_PIN == STEPPERS_DISABLE_PIN) {
        grbl_sendf(CLIENT_ALL, "[MSG: Error: HC595_LATCH_PIN (%d) conflicts with STEPPERS_DISABLE_PIN (%d)]\n", HC595_LATCH_PIN, STEPPERS_DISABLE_PIN);
        delay(1000);
        return;
    }
    #endif
    
    // 检查HC595引脚之间的冲突
    if (HC595_DATA_PIN == HC595_CLOCK_PIN || 
        HC595_DATA_PIN == HC595_LATCH_PIN || 
        HC595_CLOCK_PIN == HC595_LATCH_PIN) {
        grbl_sendf(CLIENT_ALL, "[MSG: Error: HC595 pins have conflicts: DATA=%d, CLOCK=%d, LATCH=%d]\n", 
                   HC595_DATA_PIN, HC595_CLOCK_PIN, HC595_LATCH_PIN);
        delay(1000);
        return;
    }
    
    pinMode(HC595_DATA_PIN, OUTPUT);
    pinMode(HC595_CLOCK_PIN, OUTPUT);
    pinMode(HC595_LATCH_PIN, OUTPUT);
    
    // 换纸电机独立使能控制初始化
    #ifdef PAPER_MOTORS_ENABLE
    pinMode(PAPER_MOTORS_ENABLE, OUTPUT);
    // 默认禁用换纸电机，等待系统完全初始化后启用
    digitalWrite(PAPER_MOTORS_ENABLE, HIGH);  // HIGH = 禁用
    #endif
    
    // 初始状态为0，确保所有电机停止
    hc595_write_fast(0);
    
    #ifdef PAPER_DEBUG_ENABLED
    if (PAPER_DEBUG_ENABLED) {
        grbl_sendf(CLIENT_ALL, "[MSG: HC595 initialized - DATA:%d, CLOCK:%d, LATCH:%d]\n", 
                   HC595_DATA_PIN, HC595_CLOCK_PIN, HC595_LATCH_PIN);
    }
    #endif
}

/**
 * @brief 标准HC595写入函数
 */
void hc595_write(uint8_t data) {
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
}

/**
 * @brief 高性能HC595写入函数
 */
void hc595_write_fast(uint8_t data) {
    GPIO.out_w1tc = (1ULL << HC595_LATCH_PIN);
    
    uint8_t mask = 0x80;
    for (uint8_t i = 0; i < 8; i++) {
        GPIO.out_w1tc = (1ULL << HC595_CLOCK_PIN);
        
        if (data & mask) {
            GPIO.out1.val = (1ULL << (HC595_DATA_PIN - 32));
        } else {
            GPIO.out1.val &= ~(1ULL << (HC595_DATA_PIN - 32));
        }
        
        GPIO.out_w1ts = (1ULL << HC595_CLOCK_PIN);
        mask >>= 1;
        delayMicroseconds(1);
    }
    
    GPIO.out_w1ts = (1ULL << HC595_LATCH_PIN);
    delayMicroseconds(HC595_UPDATE_DELAY_US);
}

// ================================================================================
// 传感器系统实现
// ================================================================================

/**
 * @brief 传感器系统初始化
 */
void sensor_system_init() {
    pinMode(PAPER_SENSOR_PIN, INPUT);  // GPIO34是输入专用引脚
    pinMode(PAPER_BUTTON_PIN, INPUT);  // GPIO35是输入专用引脚
}

/**
 * @brief 读取纸张传感器原始状态
 */
bool read_paper_sensor_raw() {
    return digitalRead(PAPER_SENSOR_PIN) == HIGH;
}

/**
 * @brief 读取带去抖动的纸张传感器状态
 */
bool read_paper_sensor_debounced() {
    static bool last_state = false;
    static uint32_t last_change_time = 0;
    static bool stable_state = false;
    
    bool current_state = read_paper_sensor_raw();
    uint32_t current_time = millis();
    
    if (current_state != last_state) {
        last_change_time = current_time;
        last_state = current_state;
    } else if (current_time - last_change_time > PAPER_SENSOR_DEBOUNCE_MS) {
        stable_state = current_state;
    }
    
    return stable_state;
}

/**
 * @brief 按钮状态读取
 */
bool read_button_state() {
    return digitalRead(PAPER_BUTTON_PIN) == HIGH;
}

// ================================================================================
// 电机控制实现
// ================================================================================

/**
 * @brief HC595位设置函数
 */
static inline void hc595_set_bit(uint8_t bit_pos, bool value) {
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (!ctrl) return;
    
    uint8_t new_output = ctrl->hc595_output;
    
    if (value) {
        new_output |= (1 << bit_pos);
    } else {
        new_output &= ~(1 << bit_pos);
    }
    
    if (new_output != ctrl->hc595_output) {
        hc595_write_fast(new_output);
        ctrl->hc595_output = new_output;
    }
}

/**
 * @brief 电机控制基础函数
 */
void motor_step_control(uint8_t step_bit, uint8_t dir_bit, bool forward, uint32_t step_interval) {
    // 设置方向
    hc595_set_bit(dir_bit, !forward);  // HR4988: 0=正向, 1=反向
    
    delayMicroseconds(10);  // 方向稳定时间
    
    // 生成步进脉冲
    hc595_set_bit(step_bit, HIGH);
    delayMicroseconds(DEFAULT_STEP_PULSE_MICROSECONDS);
    hc595_set_bit(step_bit, LOW);
    
    delayMicroseconds(step_interval);
}

/**
 * @brief 停止所有电机
 */
void stop_all_motors() {
    hc595_write_fast(0);
    
    paper_change_ctrl_t* ctrl = get_paper_control();
    if (ctrl) {
        ctrl->hc595_output = 0;
    }
}

// ================================================================================
// 硬件自检实现
// ================================================================================

/**
 * @brief 硬件自检函数
 */
bool hardware_self_test() {
    LOG_MSG("=== Hardware Self-Test ===");
    
    // HC595连接测试
    hc595_write(0x55);  // 测试模式
    delay(10);
    hc595_write(0xAA);  // 测试模式
    delay(10);
    hc595_write(0x00);  // 复位
    
    // 传感器测试
    bool sensor_state = read_paper_sensor_raw();
    LOG_PROGRESS("Paper sensor initial state: %s", 
                sensor_state ? "DETECTED" : "CLEAR");
    
    // 按钮测试
    bool button_state = read_button_state();
    LOG_PROGRESS("Button initial state: %s", 
                button_state ? "PRESSED" : "RELEASED");
    
    LOG_MSG("=== Hardware Self-Test Complete ===");
    return true;
}

// ================================================================================
// 换纸电机使能控制实现
// ================================================================================

/**
 * @brief 启用换纸电机
 */
void enable_paper_motors() {
    #ifdef PAPER_MOTORS_ENABLE
    digitalWrite(PAPER_MOTORS_ENABLE, LOW);  // LOW = 使能
    LOG_MSG("Paper motors ENABLED");
    #endif
}

/**
 * @brief 禁用换纸电机
 */
void disable_paper_motors() {
    #ifdef PAPER_MOTORS_ENABLE
    digitalWrite(PAPER_MOTORS_ENABLE, HIGH);  // HIGH = 禁用
    LOG_MSG("Paper motors DISABLED");
    #endif
}

/**
 * @brief 获取换纸电机使能状态
 * @return true=已启用, false=已禁用
 */
bool is_paper_motors_enabled() {
    #ifdef PAPER_MOTORS_ENABLE
    return digitalRead(PAPER_MOTORS_ENABLE) == LOW;  // LOW=使能
    #else
    return true;  // 如果没有定义使能引脚，认为始终启用
    #endif
}

#endif // AUTO_PAPER_CHANGE_ENABLE