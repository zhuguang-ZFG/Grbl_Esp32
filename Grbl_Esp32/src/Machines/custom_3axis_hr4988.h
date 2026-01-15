#pragma once
// clang-format off

/*
    custom_3axis_hr4988.h
    Part of Grbl_ESP32

    Pin assignments for custom 3-axis CNC with HR4988 drivers
    ESP32-WROOM-32E with 40MHz crystal
    Integrated 74HC595D-based automatic paper change system

    === A4 Size Writing Plotter ===
    - X Axis:  GPIO2 (STEP), GPIO15 (DIR) - 210mm travel (A4 width)
    - Y1Y2 Axis: GPIO13 (STEP), GPIO12 (DIR) - 297mm travel (A4 height), dual motor ganged
    - Z Axis:  GPIO14 (STEP), GPIO27 (DIR) - Pen up/down control, 20mm travel
    - Enable:   GPIO4 (shared for all 4 HR4988 drivers, nENABLE active low)

    === Paper Change System (74HC595D Control) ===
- 74HC595D Data Pin: GPIO32 (SI input) - 74HC595D Pin 14
- 74HC595D Clock Pin: GPIO3 (SRCLK input) - 74HC595D Pin 11
- 74HC595D Latch Pin: GPIO5 (RCLK input) - 74HC595D Pin 12
- 74HC595D Clear Pin: VCC (SRCLR input) - 74HC595D Pin 10 (高电平，不复位)
- 74HC595D OE Pin: GND (OE input) - 74HC595D Pin 13 (低电平，使能输出)
    - Paper Sensor: GPIO34 (paper presence detection)
    - One-Click Button: GPIO35 (input only pin, with pull-down resistor)
    - Button LED Control: HC595 Q0 - 按钮指示灯控制
    - Paper Motors Enable: GPIO26 + HC595 Q1 (互斥使能控制)
    - Feed Motor: HC595 pins Q6/Q7 (DIR/STEP)
    - Panel Motor: HC595 pins Q4/Q5 (DIR/STEP) 
    - Clamp Motor: HC595 pins Q2/Q3 (DIR/STEP)

    Note: No limit switches are used in this configuration.
    Enable pin: Output LOW to enable steppers, HIGH to disable
    
    Pen Control:
    - Z=0mm: Pen down (writing position)
    - Z=20mm: Pen up (travel position)
    
    Paper Change Control:
    - M0 command triggers automatic paper change sequence
    - Sensors provide feedback for paper detection and positioning
    - 74HC595D provides expanded GPIO for multiple motor control
*/

#define MACHINE_NAME "Custom 3-Axis HR4988"

// Enable software debounce since no hardware R/C filters
#define ENABLE_SOFTWARE_DEBOUNCE

// NOTE: This configuration uses HARDWARE ganged motors (both Y motors share
// the same STEP/DIR signals). Therefore, Y2_STEP_PIN and Y2_DIRECTION_PIN
// are NOT defined. If you want software ganged motors with independent control,
// you would need to use different GPIO pins for the second motor.

// === Stepper Motor Definitions ===

// X Axis
#define X_STEP_PIN              GPIO_NUM_2
#define X_DIRECTION_PIN         GPIO_NUM_15

// Y Axis (Hardware ganged - both motors share STEP/DIR signals)
// Connected to GPIO13 (STEP) and GPIO12 (DIR)
#define Y_STEP_PIN              GPIO_NUM_13
#define Y_DIRECTION_PIN         GPIO_NUM_12

// Z Axis
// Connected to GPIO14 (STEP) and GPIO27 (DIR) - Pen up/down control
#define Z_STEP_PIN              GPIO_NUM_14
#define Z_DIRECTION_PIN         GPIO_NUM_27

// Shared stepper enable pin for all HR4988 drivers
// Active LOW (nENABLE): Output LOW to enable, HIGH to disable (HR4988 standard)
// 注意：GPIO4 (P26脚)是正确的STEPPERS_DISABLE_PIN，但HC595_LATCH_PIN需要使用其他引脚
#define STEPPERS_DISABLE_PIN    GPIO_NUM_4     // 恢复：ESP32 P26脚，正确的使能引脚

// === Paper Change System with 74HC595D ===

// 74HC595D Shift Register Control
// 用户提供的实际硬件连接：
// 74HC595D Pin 10 (SRCLR) ←→ VCC (高电平，不复位)
// 74HC595D Pin 11 (SRCLK) ←→ ESP32 GPIO3
// 74HC595D Pin 12 (RCLK) ←→ ESP32 GPIO5
// 74HC595D Pin 13 (OE) ←→ GND (低电平，使能输出)
// 74HC595D Pin 14 (SI) ←→ ESP32 GPIO32
#define HC595_DATA_PIN            GPIO_NUM_32    // Serial data input (SI pin) - 74HC595D Pin 14
#define HC595_CLOCK_PIN           GPIO_NUM_3     // Shift register clock (SRCLK) - 74HC595D Pin 11
#define HC595_LATCH_PIN           GPIO_NUM_5     // Register clock (RCLK) - 74HC595D Pin 12

// Paper Sensor Input (Low active)
#define PAPER_SENSOR_PIN          GPIO_NUM_34    // Paper presence sensor (input only pin, LOW=paper detected)

// One-Click Button Input (GPIO, not HC595)
#define PAPER_BUTTON_PIN          GPIO_NUM_35    // One-click paper change button (input only pin, LOW=pressed, with pull-up)

// HR4988 Driver Reference Voltage Control
// GPIO25 connected to VREF pin of HR4988 drivers for current setting
#define HR4988_VREF_PIN           GPIO_NUM_25    // HR4988 VREF current control pin

// Paper Motors Enable Control (dual control)
// GPIO26: XYZ轴使能控制（互斥逻辑）
//   - GPIO26=LOW: XYZ轴使能，换纸电机禁用（不能动）
//   - GPIO26=HIGH: XYZ轴禁用，换纸电机可以工作
// HC595 Q1: 换纸电机使能控制（互斥逻辑）  
//   - Q1=LOW: 换纸电机使能，XYZ轴禁用（不能动）
//   - Q1=HIGH: 换纸电机禁用，XYZ轴可以工作
// 注意：PAPER_MOTORS_ENABLE 已移动到74HC595D Q1控制

// 74HC595D Output Mapping (based on pin numbers)
// Q0 (pin 15): BUTTON_LED_CONTROL      - 一键换纸按钮指示灯控制(LOW=亮, HIGH=灭)
// Q1 (pin 1):  PAPER_MOTORS_ENABLE      - 换纸电机HR4988额外使能控制(配合GPIO26)  
// Q2 (pin 2):  PAPER_CLAMP_DIR_PIN      - 压纸抬落电机方向控制
// Q3 (pin 3):  PAPER_CLAMP_STEP_PIN     - 压纸抬落电机步进控制
// Q4 (pin 4):  PANEL_MOTOR_DIR_PIN      - 出纸面板电机方向控制  
// Q5 (pin 5):  PANEL_MOTOR_STEP_PIN     - 出纸面板电机步进控制
// Q6 (pin 6):  FEED_MOTOR_DIR_PIN       - 进纸器电机方向控制
// Q7 (pin 7):  FEED_MOTOR_STEP_PIN      - 进纸器电机步进控制

// Bit positions in 74HC595D shift register (0-7)
#define BIT_BUTTON_LED_CONTROL       0    // Q0 - 按钮指示灯控制
#define BIT_PAPER_MOTORS_ENABLE    1    // Q1 - 换纸电机使能
#define BIT_PAPER_CLAMP_DIR         2    // Q2 - 压纸抬落电机方向
#define BIT_PAPER_CLAMP_STEP        3    // Q3 - 压纸抬落电机步进
#define BIT_PANEL_MOTOR_DIR        4    // Q4 - 面板电机方向
#define BIT_PANEL_MOTOR_STEP        5    // Q5 - 面板电机步进
#define BIT_FEED_MOTOR_DIR          6    // Q6 - 进纸器电机方向
#define BIT_FEED_MOTOR_STEP         7    // Q7 - 进纸器电机步进

// Note: No limit switches are used in this configuration
// Uncomment and configure if you add limit switches later
// #define X_LIMIT_PIN             GPIO_NUM_XX
// #define Y_LIMIT_PIN             GPIO_NUM_XX
// #define Z_LIMIT_PIN             GPIO_NUM_XX

// === Default Settings ===

#define DEFAULT_STEP_PULSE_MICROSECONDS     5  // Increased for better driver reliability
#define DEFAULT_STEPPER_IDLE_LOCK_TIME      250

#define DEFAULT_STEPPING_INVERT_MASK        0  // uint8_t
#define DEFAULT_DIRECTION_INVERT_MASK        0  // uint8_t
#define DEFAULT_INVERT_ST_ENABLE             0  // boolean (no invert - nENABLE active low)

#define DEFAULT_STATUS_REPORT_MASK           1

#define DEFAULT_JUNCTION_DEVIATION   0.01    // mm
#define DEFAULT_ARC_TOLERANCE        0.002   // mm
#define DEFAULT_REPORT_INCHES         0       // false

#define DEFAULT_SOFT_LIMIT_ENABLE     0       // false
#define DEFAULT_HARD_LIMIT_ENABLE     0       // false

#define DEFAULT_HOMING_ENABLE        0       // No homing without sensors

// Spindle configuration (adjust as needed)
#define DEFAULT_SPINDLE_RPM_MAX      1000.0  // rpm
#define DEFAULT_SPINDLE_RPM_MIN      0.0     // rpm

#define DEFAULT_LASER_MODE           0       // false

// Motor parameters (adjusted for plotter/pen writing machine)
#define DEFAULT_X_STEPS_PER_MM       200.0
#define DEFAULT_Y_STEPS_PER_MM       200.0
#define DEFAULT_Z_STEPS_PER_MM       400.0   // Higher resolution for pen control

#define DEFAULT_X_MAX_RATE           5000.0  // mm/min
#define DEFAULT_Y_MAX_RATE           5000.0  // mm/min
#define DEFAULT_Z_MAX_RATE           1500.0  // Slower for pen up/down control

#define DEFAULT_X_ACCELERATION       500.0   // mm/sec^2
#define DEFAULT_Y_ACCELERATION       500.0   // mm/sec^2
#define DEFAULT_Z_ACCELERATION       800.0   // Faster acceleration for pen lift

#define DEFAULT_X_MAX_TRAVEL         210.0   // mm - A4 width (short side)
#define DEFAULT_Y_MAX_TRAVEL         297.0   // mm - A4 height (long side)
#define DEFAULT_Z_MAX_TRAVEL         20.0    // mm - pen lift height (0-20mm)

// === Paper Change System Parameters ===

// Feed Motor (进纸器电机)
#define DEFAULT_FEED_STEPS_PER_MM    80.0     // steps/mm - 与文档一致
#define DEFAULT_FEED_MAX_RATE        2000.0   // mm/min - 与文档一致
#define DEFAULT_FEED_ACCELERATION    400.0    // mm/sec^2
#define DEFAULT_FEED_MAX_TRAVEL      350.0    // mm - maximum paper feed length (A4+margin)

// Panel Motor (出纸面板电机)  
#define DEFAULT_PANEL_STEPS_PER_MM   80.0     // steps/mm
#define DEFAULT_PANEL_MAX_RATE       3000.0   // mm/min - 提高速度
#define DEFAULT_PANEL_ACCELERATION   400.0    // mm/sec^2 - 提高加速度
#define DEFAULT_PANEL_MAX_TRAVEL     300.0    // mm - panel travel distance (full A4 length)

// 注意：Paper Change相关参数已移动到 PaperChangeConfig.h
// 请勿在此文件中重定义换纸系统参数，避免编译冲突

// Paper Change Timing Parameters
#define PAPER_FEED_TIMEOUT_MS        5000     // ms - maximum time to wait for paper feeding
#define PAPER_CLAMP_DELAY_MS         100      // ms - delay after clamp operation
#define PAPER_UNCLAMP_DELAY_MS       150      // ms - delay after unclamp operation
#define HC595_UPDATE_DELAY_US        10       // microseconds - delay between HC595 operations

// One-Click Paper Change Button Parameters
#define PAPER_BUTTON_DEBOUNCE_MS     50       // ms - button debounce time
#define PAPER_BUTTON_LONG_PRESS_MS   2000     // ms - long press detection for emergency stop
#define PAPER_BUTTON_POLL_INTERVAL   100      // ms - button check interval in main loop

// Enable Paper Change System
#define AUTO_PAPER_CHANGE_ENABLE     1        // Enable automatic paper change functionality

// Smart M0 Detection Parameters
#define SMART_M0_DETECTION_ENABLE     1        // Enable smart M0 content detection
#define M0_CONTENT_CHECK_TIMEOUT_MS   2000     // ms - timeout for checking content after M0
#define M0_COMMAND_QUEUE_SIZE         16       // Number of commands to check ahead for content
#define PAPER_CHANGE_AUTO_OK_REPLY    1        // Automatically send OK after paper change completes
