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
    PAPER_PRE_CHECK,         // 预检状态 - 面板电机倒转检测是否有纸
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
// 74HC595D移位寄存器数据写入函数
// 参数: data - 要输出的8位数据 (每一位对应一个控制信号)
static void hc595_write(uint8_t data) {
    // 写入数据到74HC595D移位寄存器
    digitalWrite(HC595_LATCH_PIN, LOW);    // 拉低锁存引脚，准备移位数据
    
    // 循环8次，逐位移入数据 (从最高位MSB到最低位LSB)
    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(HC595_CLOCK_PIN, LOW);    // 时钟信号拉低，准备数据位
        // 设置数据引脚: 检查对应位是否为1
        // (data & (0x80 >> i)): 从MSB开始检查每一位
        digitalWrite(HC595_DATA_PIN, (data & (0x80 >> i)) ? HIGH : LOW);
        delayMicroseconds(1);                  // 数据建立时间，确保数据稳定
        digitalWrite(HC595_CLOCK_PIN, HIGH);   // 时钟上升沿，数据移入寄存器
        delayMicroseconds(1);                  // 时钟保持时间，完成移位
    }
    
    digitalWrite(HC595_LATCH_PIN, HIGH);   // 锁存上升沿，将移位寄存器数据并行输出
    delayMicroseconds(HC595_UPDATE_DELAY_US); // 等待硬件稳定时间
    
    paper_ctrl.hc595_output = data;        // 保存当前输出状态，用于状态跟踪
}

// 电机步进使能控制函数
// 参数: motor_bit - 电机对应的位位置 (0-7), enable - true使能步进，false停止步进
static void set_motor_step(uint8_t motor_bit, bool enable) {
    uint8_t new_output = paper_ctrl.hc595_output;  // 获取当前HC595输出状态
    
    if (enable) {
        // 使能电机步进: 将对应位设置为1 (高电平)
        new_output |= (1 << motor_bit);  // 按位或操作，设置指定位为1
    } else {
        // 禁用电机步进: 将对应位设置为0 (低电平)  
        new_output &= ~(1 << motor_bit); // 按位与操作，清除指定位为0
    }
    
    // 只有输出状态发生变化时才更新硬件，避免不必要的操作
    if (new_output != paper_ctrl.hc595_output) {
        hc595_write(new_output);          // 写入新的状态到HC595
    }
}

// 电机方向控制函数  
// 参数: motor_bit - 电机对应的位位置 (0-7), forward - true正向，false反向
static void set_motor_dir(uint8_t motor_bit, bool forward) {
    uint8_t new_output = paper_ctrl.hc595_output;  // 获取当前HC595输出状态
    
    if (forward) {
        // 正向旋转: 将方向位设置为0 (根据硬件设计，低电平为正向)
        new_output &= ~(1 << motor_bit);  // 按位清零，设置正向
    } else {
        // 反向旋转: 将方向位设置为1 (根据硬件设计，高电平为反向)
        new_output |= (1 << motor_bit);   // 按位置一，设置反向
    }
    
    // 只有输出状态发生变化时才更新硬件
    if (new_output != paper_ctrl.hc595_output) {
        hc595_write(new_output);          // 写入新的方向状态到HC595
    }
}

// === Motor Step Generation ===
// 电机步进脉冲生成函数 - 生成一个完整的步进脉冲
// 参数: step_bit - 步进控制位, dir_bit - 方向控制位, forward - 转向
static void generate_motor_step(uint8_t step_bit, uint8_t dir_bit, bool forward) {
    set_motor_dir(dir_bit, forward);                // 1. 首先设置电机旋转方向
    delayMicroseconds(10);                          // 2. 方向设置稳定时间 (10μs)
    set_motor_step(step_bit, HIGH);                  // 3. 步进信号拉高 (开始步进)
    delayMicroseconds(DEFAULT_STEP_PULSE_MICROSECONDS); // 4. 步进脉冲宽度 (默认5μs)
    set_motor_step(step_bit, LOW);                   // 5. 步进信号拉低 (完成一步)
    delayMicroseconds(10);                          // 6. 步进间隔时间 (10μs)
}

// === Paper Sensor Reading ===
// 纸张传感器读取函数
// 返回: true - 检测到纸张, false - 无纸张
static bool read_paper_sensor() {
    return digitalRead(PAPER_SENSOR_PIN) == HIGH;  // 读取GPIO34，假设高电平表示检测到纸张
}

// === State Machine Functions ===
static void enter_state(paper_change_state_t new_state) {
    paper_ctrl.state = new_state;
    paper_ctrl.state_timer = millis();
    paper_ctrl.step_counter = 0;
    
    // Log state transition
    const char* state_names[] = {
        "IDLE", "PRE_CHECK", "EJECTING", "FEEDING", "DETECTING", 
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
    
    grbl_sendf(CLIENT_ALL, "[MSG: Starting automatic paper change with pre-check]\r\n");
    enter_state(PAPER_PRE_CHECK);  // 先进入预检状态，倒转检测是否有纸
}

// Manual one-click paper change
void paper_change_one_click() {
    if (paper_ctrl.state != PAPER_IDLE) {
        grbl_sendf(CLIENT_ALL, "[MSG: Paper change busy, cannot start manual]\r\n");
        return;
    }
    
    grbl_sendf(CLIENT_ALL, "[MSG: Starting manual paper change with pre-check]\r\n");
    paper_ctrl.one_click_active = true;
    enter_state(PAPER_PRE_CHECK);  // 手动换纸也先预检
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
    
    // Read button state from GPIO input pin
    bool button_pressed = (digitalRead(PAPER_BUTTON_PIN) == HIGH);
    
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
// 换纸状态机更新函数 - 在主循环中被调用，处理状态转换和电机控制
void paper_change_update() {
    // Check for emergency stop - 检查紧急停止标志
    if (paper_ctrl.emergency_stop) {
        return;  // 紧急停止状态下，不执行任何换纸操作
    }
    
    // Check button press - 检查按钮状态变化
    check_button_press();
    
    // Update sensor state - 更新传感器状态
    paper_ctrl.last_paper_sensor_state = paper_ctrl.paper_sensor_state; // 保存上一次状态
    paper_ctrl.paper_sensor_state = read_paper_sensor();             // 读取当前传感器状态
    
    uint32_t current_time = millis();  // 获取当前系统时间，用于超时判断
    
    switch (paper_ctrl.state) {         // 根据当前状态执行相应操作
        case PAPER_IDLE:
            // Check for paper sensor changes in idle (for monitoring)
            break;
            
        case PAPER_PRE_CHECK:
            // Pre-check state - 面板电机倒转检测是否有纸
            if (paper_ctrl.step_counter < PAPER_PRE_CHECK_STEPS) {  // 倒转检测步数(约0.625mm)
                generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, false); // 倒转
                paper_ctrl.step_counter++;
                delayMicroseconds(PAPER_PRE_CHECK_INTERVAL_US); // 倒转间隔
            } else {
                // 检测倒转后的传感器状态
                if (paper_ctrl.paper_sensor_state) {
                    // 传感器仍检测到纸 → 面板上有纸，执行出纸
                    grbl_sendf(CLIENT_ALL, "[MSG: Paper detected on panel, starting ejection]\r\n");
                    enter_state(PAPER_EJECTING);
                } else {
                    // 传感器未检测到纸 → 面板无纸，直接进纸
                    grbl_sendf(CLIENT_ALL, "[MSG: No paper on panel, starting feeding]\r\n");
                    enter_state(PAPER_FEEDING);
                }
            }
            break;
            
        case PAPER_EJECTING:
            // Eject finished paper using panel motor - 完全送出A4纸张(297mm)
            if (paper_ctrl.step_counter < PAPER_EJECT_STEPS) {  // 检查是否达到A4长度所需步数
                generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true); // 生成面板电机步进脉冲
                paper_ctrl.step_counter++;        // 步进计数器递增
                delayMicroseconds(PAPER_EJECT_INTERVAL_US); // 步进间隔1ms，提高出纸速度
            } else {
                enter_state(PAPER_FEEDING);       // A4长度完成，转入进纸状态
            }
            break;
            
        case PAPER_FEEDING:
            // Feed new paper and detect sensor - 送入新纸并检测传感器
            set_motor_dir(BIT_FEED_MOTOR_DIR, true);  // 设置进纸电机方向为正向
            set_motor_step(BIT_FEED_MOTOR_STEP, HIGH); // 开始步进脉冲
            delayMicroseconds(DEFAULT_STEP_PULSE_MICROSECONDS); // 脉冲宽度时间
            set_motor_step(BIT_FEED_MOTOR_STEP, LOW);  // 结束步进脉冲，完成一步
            delayMicroseconds(2000);                  // 步进间隔2ms，控制进纸速度
            
            paper_ctrl.step_counter++;  // 进纸步数计数器递增
            
            // Check for paper detection - 检查纸张检测 (从无到有的上升沿)
            if (paper_ctrl.paper_sensor_state && !paper_ctrl.last_paper_sensor_state) {
                grbl_sendf(CLIENT_ALL, "[MSG: Paper detected, engaging clamp]\r\n"); // 发送检测到纸张的消息
                set_motor_step(BIT_FEED_MOTOR_STEP, LOW);  // 停止进纸电机
                enter_state(PAPER_CLAMPING);               // 转入夹纸状态
            } else if (paper_ctrl.step_counter > 500) {
                // Timeout - no paper detected - 超时保护：500步后仍无检测到纸张
                grbl_sendf(CLIENT_ALL, "[MSG: Feed timeout, no paper detected]\r\n"); // 发送超时消息
                set_motor_step(BIT_FEED_MOTOR_STEP, LOW);  // 停止进纸电机
                enter_state(PAPER_ERROR);                  // 转入错误状态
            }
            break;
            
        case PAPER_DETECTING:
            // This state might not be needed since we detect in FEEDING state
            enter_state(PAPER_CLAMPING);
            break;
            
        case PAPER_CLAMPING:
            // Engage clamp motor - both clamp and panel motors active - 启动夹纸电机，夹纸和面板电机同时工作
            generate_motor_step(BIT_PAPER_CLAMP_STEP, BIT_PAPER_CLAMP_DIR, true);  // 压纸电机步进
            generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true);  // 面板电机同步步进
            
            paper_ctrl.step_counter++;  // 夹纸步数计数器递增
            
            if (paper_ctrl.step_counter >= 20) {  // 检查是否达到20步的目标夹纸步数
                grbl_sendf(CLIENT_ALL, "[MSG: Clamp engaged, monitoring sensor]\r\n"); // 发送夹纸完成消息
                enter_state(PAPER_RELEASING);      // 转入释放检测状态
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
            // Panel motor fine positioning: forward 32 steps, back 2 steps - 精确定位A4纸张位置
            if (current_time >= paper_ctrl.state_timer) {
                if (paper_ctrl.step_counter < 32) {
                    // Forward 32 steps - 前进32步(0.4mm)，确保纸张完全到位
                    generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, true);
                    paper_ctrl.step_counter++;
                    delayMicroseconds(2000);  // 优化：2ms延迟，提高响应速度
                } else if (paper_ctrl.step_counter < 34) {
                    // Back 2 steps - 后退2步(0.025mm)，微调纸张精确位置
                    generate_motor_step(BIT_PANEL_MOTOR_STEP, BIT_PANEL_MOTOR_DIR, false);
                    paper_ctrl.step_counter++;
                    delayMicroseconds(2000);  // 优化：2ms延迟
                } else {
                    // Positioning complete - 精确定位完成
                    hc595_write(0);  // Stop all motors - 停止所有电机
                    enter_state(PAPER_COMPLETE);  // 进入完成状态
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
        "IDLE", "PRE_CHECK", "EJECTING", "FEEDING", "DETECTING", 
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
// 换纸系统初始化函数 - 配置所有GPIO和硬件接口
void paper_change_init() {
    // Configure GPIO pins - 配置所有相关的GPIO引脚
    pinMode(HC595_DATA_PIN, OUTPUT);           // HC595数据引脚设置为输出
    pinMode(HC595_CLOCK_PIN, OUTPUT);         // HC595时钟引脚设置为输出
    pinMode(HC595_LATCH_PIN, OUTPUT);         // HC595锁存引脚设置为输出
    pinMode(PAPER_SENSOR_PIN, INPUT_PULLDOWN); // 纸张传感器引脚设置为输入+下拉电阻
    pinMode(PAPER_BUTTON_PIN, INPUT_PULLDOWN); // 按钮引脚设置为输入+下拉电阻，默认低电平
    
    // Initialize HC595 output to all off - 初始化HC595所有输出为关闭状态
    hc595_write(0);                          // 写入0，关闭所有电机输出
    
    // 发送初始化完成消息到所有客户端
    grbl_sendf(CLIENT_ALL, "[MSG: Paper change system initialized]\r\n");
}

#endif // AUTO_PAPER_CHANGE_ENABLE