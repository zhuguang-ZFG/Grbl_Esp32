/*
  PaperChangeHardware.cpp - 换纸系统硬件驱动实现
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE

// 为了使用map_f和constrain_f函数
#ifndef constrain_f
#define constrain_f(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#endif

#ifndef map_f
#define map_f(x, in_min, in_max, out_min, out_max) \
    ((x) - (in_min)) * ((out_max) - (out_min)) / ((in_max) - (in_min)) + (out_min)
#endif
#include "PaperChangeConfig.h"
#include "PaperChangeTypes.h"
#include "PaperChangeHardware.h"
#include "PaperChangeUtils.h"

// ================================================================================
// HC595批量更新器类 - 性能优化
// ================================================================================

class HC595BatchUpdater {
private:
    bool dirty = false;
    uint8_t pending_output = 0;
    
public:
    void set_bit(uint8_t pos, bool value) {
        if (value) {
            pending_output |= (1 << pos);
        } else {
            pending_output &= ~(1 << pos);
        }
        dirty = true;
    }
    
    void force_update() {
        dirty = true;
        pending_output = get_current_output();
    }
    
    void flush() {
        if (dirty) {
            hc595_write_fast(pending_output);
            dirty = false;
        }
    }
    
    uint8_t get_current_output() {
        paper_change_ctrl_t* ctrl = get_paper_control();
        return ctrl ? ctrl->hc595_output : 0;
    }
};

// 全局批量更新器实例
static HC595BatchUpdater hc595_updater;

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
 * 
 * 使用Arduino digitalWrite()向74HC595D移位寄存器写入8位数据
 * 这是标准的SPI兼容写入方式，可靠性好但性能较低
 * 
 * @param data 要写入的8位数据（bit0对应Q0，bit7对应Q7）
 * 
 * @note 时序要求：
 * 1. LATCH拉低，准备接收数据
 * 2. 8个时钟周期，从MSB到LSB移入数据
 * 3. LATCH拉高，将数据锁存到输出寄存器
 * 4. 稳定延时，确保数据可靠输出
 * 
 * @warning 时钟信号质量至关重要，确保>1μs的脉宽
 */
void hc595_write(uint8_t data) {
    // 步骤1：准备数据接收 - 拉低锁存信号
    digitalWrite(HC595_LATCH_PIN, LOW);
    
    // 步骤2：串行移入8位数据 - MSB优先（bit7到bit0）
    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(HC595_CLOCK_PIN, LOW);  // 时钟下降沿，准备数据
        
        // 设置数据位：根据当前位值设置DATA引脚
        // 0x80>>i 从MSB(10000000)到LSB(00000001)移动
        digitalWrite(HC595_DATA_PIN, (data & (0x80 >> i)) ? HIGH : LOW);
        
        delayMicroseconds(1);              // 数据建立时间
        digitalWrite(HC595_CLOCK_PIN, HIGH); // 时钟上升沿，移入数据
        delayMicroseconds(1);              // 数据保持时间
    }
    
    // 步骤3：锁存数据到输出寄存器
    digitalWrite(HC595_LATCH_PIN, HIGH);
    
    // 步骤4：等待输出稳定
    delayMicroseconds(HC595_UPDATE_DELAY_US);  // 通常10μs足够
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

/**
 * @brief HC595更新函数 - 使用批量更新器
 */
void hc595_update(void) {
    hc595_updater.flush();
}

/**
 * @brief 强制更新HC595输出
 */
void hc595_force_update(void) {
    hc595_updater.force_update();
    hc595_updater.flush();
}

// ================================================================================
// 传感器系统实现
// ================================================================================

/**
 * @brief 传感器系统初始化
 */
void sensor_system_init() {
    pinMode(PAPER_SENSOR_PIN, INPUT_PULLUP);  // GPIO34低电平有效，启用内部上拉
    pinMode(PAPER_BUTTON_PIN, INPUT_PULLUP);  // GPIO35按键，启用内部上拉
}

/**
 * @brief 读取纸张传感器原始状态
 */
bool read_paper_sensor_raw() {
    return digitalRead(PAPER_SENSOR_PIN) == LOW;  // 低电平有效
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
    return digitalRead(PAPER_BUTTON_PIN) == LOW;  // 低电平有效
}

// ================================================================================
// 电机控制实现
// ================================================================================

/**
 * @brief HC595位设置函数
 * 
 * 设置74HC595D移位寄存器的指定位，用于控制换纸系统的各个电机和指示灯
 * 这是硬件抽象层的核心函数，确保对HC595的操作是原子和安全的
 * 
 * @param bit_pos 位位置（0-7，对应Q0-Q7）
 * @param value 设置值（true=高电平，false=低电平）
 * 
 * @note 位映射：
 * bit 0 (Q0): 按钮LED控制
 * bit 1 (Q1): 换纸电机使能
 * bit 2 (Q2): 夹紧电机方向
 * bit 3 (Q3): 夹紧电机步进
 * bit 4 (Q4): 面板电机方向
 * bit 5 (Q5): 面板电机步进
 * bit 6 (Q6): 进纸电机方向
 * bit 7 (Q7): 进纸电机步进
 * 
 * @warning 修改后需要调用hc595_update()才能生效
 */
static inline void hc595_set_bit(uint8_t bit_pos, bool value) {
    paper_change_ctrl_t* ctrl = get_paper_control();
    HANDLE_NULL_POINTER(ctrl, "hc595_set_bit");
    
    // 检查位位置有效性
    HANDLE_INVALID_RANGE(bit_pos, 0, 7, "hc595_set_bit", "bit_pos");
    
    uint8_t new_output = ctrl->hc595_output;  // 获取当前输出状态
    
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
/**
 * @brief 电机步进控制函数
 * 
 * 这是换纸系统所有电机运动的底层控制函数
 * 负责生成正确的方向信号和步进脉冲时序
 * 
 * @param step_bit 步进控制位（对应HC595的STEP引脚）
 * @param dir_bit 方向控制位（对应HC595的DIR引脚）
 * @param forward 方向标志（true=正向，false=反向）
 * @param step_interval 步进间隔（微秒），控制电机速度
 * 
 * @note HR4988驱动器特性：
 * - DIR引脚：低电平=正转，高电平=反转
 * - STEP引脚：上升沿触发一步
 * - 方向改变需要稳定时间（≥10μs）
 * 
 * @example 调用示例：
 * motor_step_control(BIT_FEED_MOTOR_STEP, BIT_FEED_MOTOR_DIR, true, 2000);
 * // 进纸电机正转，2000μs间隔
 */
void motor_step_control(uint8_t step_bit, uint8_t dir_bit, bool forward, uint32_t step_interval) {
    // 步骤1：设置电机方向
    // 注意：HR4988的DIR逻辑是反向的（0=正，1=反）
    hc595_set_bit(dir_bit, !forward);  // !forward转换为硬件逻辑
    hc595_update();                   // 立即更新，确保方向信号稳定
    
    delayMicroseconds(10);  // 方向信号稳定时间，HR4988要求
    
    // 步骤2：生成步进脉冲
    // HR4988在STEP信号的上升沿触发一步
    hc595_set_bit(step_bit, HIGH);                         // 脉冲上升沿
    delayMicroseconds(DEFAULT_STEP_PULSE_MICROSECONDS); // 脉冲宽度（通常5μs）
    hc595_set_bit(step_bit, LOW);                          // 脉冲下降沿
    
    hc595_update();  // 确保脉冲输出到硬件
    
    // 步骤3：步进间隔延时
    // 控制电机速度，时间越长速度越慢
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
    #endif
    
    // 同时设置HC595 Q1位为低电平（使能换纸电机）
    hc595_set_bit(BIT_PAPER_MOTORS_ENABLE, LOW);
    
    LOG_MSG("Paper motors ENABLED (GPIO26=LOW, Q1=LOW)");
}

/**
 * @brief 禁用换纸电机
 */
void disable_paper_motors() {
    #ifdef PAPER_MOTORS_ENABLE
    digitalWrite(PAPER_MOTORS_ENABLE, HIGH);  // HIGH = 禁用
    #endif
    
    // 同时设置HC595 Q1位为高电平（禁用换纸电机）
    hc595_set_bit(BIT_PAPER_MOTORS_ENABLE, HIGH);
    
    LOG_MSG("Paper motors DISABLED (GPIO26=HIGH, Q1=HIGH)");
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

/**
 * @brief 设置按钮LED状态
 * 
 * 控制一键换纸按钮的LED指示灯，提供用户视觉反馈
 * LED状态帮助用户了解换纸系统的工作状态
 * 
 * @param on true=LED亮，false=LED灭
 * 
 * @note 电气特性：
 * - LED连接到74HC595D的Q0输出
 * - HC595输出低电平时LED亮，高电平时LED灭
 * - 使用!on是因为硬件为低电平有效
 * 
 * @example 使用场景：
 * - 换纸进行中：LED闪烁或常亮
 * - 换纸完成：LED熄灭
 * - 错误状态：LED快速闪烁
 */
void set_button_led(bool on) {
    hc595_set_bit(BIT_BUTTON_LED_CONTROL, !on);  // 硬件低电平有效，所以取反
    hc595_update();  // 立即更新HC595输出，使LED状态生效
    LOG_MSG("Button LED set to %s (HC595 Q0 = %s)", 
             on ? "ON" : "OFF", !on ? "LOW" : "HIGH");
}

/**
 * @brief 获取按钮LED状态
 * 
 * 读取当前按钮LED的实际状态，用于状态同步和调试
 * 
 * @return true=LED亮，false=LED灭
 * 
 * @note 返回的是逻辑状态（true=亮），而不是硬件电平
 */
bool get_button_led() {
    paper_change_ctrl_t* ctrl = get_paper_control();
    HANDLE_NULL_POINTER_RETURN(ctrl, "get_button_led", false);
    
    uint8_t current_output = ctrl->hc595_output;
    return !(current_output & (1 << BIT_BUTTON_LED_CONTROL));  // LOW=亮
}

// ================================================================================
// HR4988 VREF动态电流控制实现
// ================================================================================

bool init_hr4988_vref() {
#ifdef HR4988_VREF_PIN
    bool result = hr4988_simple_init();
    
    if (result) {
        // 设置换纸系统的默认工作模式
        hr4988_simple_set_mode(MOTOR_MODE_NORMAL);
        LOG_MSG("HR4988 VREF (Simple) initialized for paper change system");
    }
    
    return result;
#else
    LOG_WARNING("HR4988 VREF pin not defined, current control disabled");
    return false;
#endif
}

bool set_paper_motor_current(float current) {
#ifdef HR4988_VREF_PIN
    bool result = hr4988_simple_set_current(current);
    
    if (result) {
        LOG_MSG_F("Paper motor current set to %.2fA", current);
    } else {
        LOG_ERROR_F("Failed to set paper motor current to %.2fA", current);
    }
    
    return result;
#else
    LOG_WARNING("HR4988 VREF not available, cannot set current");
    return false;
#endif
}

bool set_paper_motor_mode(motor_current_mode_t mode) {
#ifdef HR4988_VREF_PIN
    bool result = hr4988_simple_set_mode(mode);
    
    if (result) {
        LOG_MSG_F("Paper motor mode set to %s", hr4988_simple_get_mode_name(mode));
    } else {
        LOG_ERROR_F("Failed to set paper motor mode to %d", mode);
    }
    
    return result;
#else
    LOG_WARNING("HR4988 VREF not available, cannot set mode");
    return false;
#endif
}

void set_current_for_paper_phase(paper_change_state_t phase) {
#ifdef HR4988_VREF_PIN
    // 使用简化版的VREF控制
    hr4988_simple_set_current_for_phase(phase);
#else
    LOG_DEBUG("HR4988 VREF not available, using default current");
#endif
}

void update_hr4988_vref() {
#ifdef HR4988_VREF_PIN
    // 简化版本暂时不需要定期更新
    // 可以添加节能检查等功能
#endif
}

void print_hr4988_vref_status() {
#ifdef HR4988_VREF_PIN
    hr4988_simple_print_status();
#else
    LOG_MSG("HR4988 VREF not available on this hardware");
#endif
}

#endif // AUTO_PAPER_CHANGE_ENABLE