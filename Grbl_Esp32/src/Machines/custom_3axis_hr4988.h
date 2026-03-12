#pragma once
// clang-format off

/*
    custom_3axis_hr4988.h
    Part of Grbl_ESP32

    Pin assignments for custom 3-axis CNC with HR4988 drivers
    ESP32-D0WDQ6 with 40MHz crystal

    Hardware Configuration (matching user wiring):
    - X Axis:  GPIO2  (STEP), GPIO15 (DIR)
    - Y Axis:  GPIO13 (STEP), GPIO12 (DIR) - dual motor ganged (both motors share STEP/DIR)
    - Z Axis:  MTMS (GPIO14/STEP), GPIO27 (DIR) - Pen up/down control
    - Enable:  GPIO4 (shared for all 4 HR4988 drivers, nENABLE active low)
    - Driver REF: Connected to ESP32 GPIO25 (DAC) — firmware must output voltage for feeder current

    74HC595D Shift Register (paper handling system):
    - DATA (SI):   GPIO32 -> 74HC595 Pin 14
    - SRCLK:       GPIO3  -> 74HC595 Pin 11
    - RCLK:        GPIO5  -> 74HC595 Pin 12
    - SRCLR:       VCC    -> 74HC595 Pin 10 (always enabled)
    - OE:          GND    -> 74HC595 Pin 13 (outputs always enabled)

    74HC595D outputs (Q0..Q7):
    - Q0 (bit0):   Paper change button LED  (HIGH=off, LOW=on)
    - Q1 (bit1):   Paper-change HR4988 enable (LOW=enabled, HIGH=disabled)
    - Q2 (bit2):   Clamp lift motor DIR (HR4988 pin 19)
    - Q3 (bit3):   Clamp lift motor STEP (HR4988 pin 16)
    - Q4 (bit4):   Panel motor DIR (HR4988 pin 19)
    - Q5 (bit5):   Panel motor STEP (HR4988 pin 16)
    - Q6 (bit6):   Feeder motor DIR (HR4988 pin 19)
    - Q7 (bit7):   Feeder motor STEP (HR4988 pin 16)

    Sensors:
    - Paper sensor:         GPIO34 (HIGH=paper present, LOW=no paper)
    - Paper-change button:  GPIO35 (LOW=pressed, with pulldown)

    Note: No limit switches or position sensors are used.
    Enable pin: Output LOW to enable steppers, HIGH to disable

    Pen Control:
    - Z=0mm: Pen down (writing position)
    - Z=20mm: Pen up (travel position)
*/

#define MACHINE_NAME "Custom 3-Axis HR4988"
#define GRBL_PAPER_SYSTEM 1  /* æ¢çº¸ç³»ç» M701/M711/M712/M713 å?GCode.cpp ä¸­ç´åå®ç?*/

// Use custom machine code (Custom/paper_system.cpp)
#define CUSTOM_CODE_FILENAME "Custom/paper_system.cpp"
// Enable user-defined M codes (handled in paper_system.cpp)
#define USE_USER_M_CODES

// Enable software debounce since no hardware R/C filters
#define ENABLE_SOFTWARE_DEBOUNCE

// === I2S-based GPIO expander (74HC595D) for paper-handling system ===
// Use ESP32 I2S peripheral to drive 74HC595D, expanding 8 output-only pins (Q0..Q7).
// These pins are only used for the paper change / feed system, not for main XYZ steppers.

// Enable I2S output-only expander
#define USE_I2S_OUT

// Map I2S signals to your wiring
// DATA (SI)  -> GPIO21
// BCK (SRCLK)-> GPIO16
// WS  (RCLK) -> GPIO17
#define I2S_OUT_DATA            GPIO_NUM_21
#define I2S_OUT_BCK             GPIO_NUM_16
#define I2S_OUT_WS              GPIO_NUM_17

// Initial state of 74HC595 outputs after reset/latch:
// - Q0 LED:    HIGH = off
// - Q1 Enable: HIGH = paper-change disabled (safe)
// Others default LOW.
#define I2S_OUT_INIT_VAL        (bit(0) | bit(1))

// Logical names for 74HC595 expanded outputs (Q0..Q7)
#define PAPER_LED_PIN           I2SO(0)  // Q0: paper-change button LED (HIGH=off, LOW=on)
#define PAPER_PANEL_ENABLE_PIN   I2SO(1)
#define PAPER_DRIVER_ENABLE_PIN  GPIO_NUM_26
#define PAPER_DRIVER_REF_PIN    GPIO_NUM_25   // DAC 直连 HR4988 REF，无分压；0–255 → 0–3.3V
#define PAPER_DRIVER_REF_DAC    60          // 直连时建议 80–150（≈1.0–1.9V），过大易过流发热
#define PAPER_ENABLE_PIN         PAPER_PANEL_ENABLE_PIN  // Q1: paper system HR4988 enable (LOW=enable, HIGH=disable)
#define CLAMP_MOTOR_DIR_PIN     I2SO(2)  // Q2: clamp (press roller) up/down motor DIR
#define CLAMP_MOTOR_STEP_PIN    I2SO(3)  // Q3: clamp up/down motor STEP
#define PANEL_MOTOR_DIR_PIN     I2SO(4)  // Q4: panel motor DIR
#define PANEL_MOTOR_STEP_PIN    I2SO(5)  // Q5: panel motor STEP
#define FEEDER_MOTOR_DIR_PIN    I2SO(6)  // Q6: feeder motor DIR
#define FEEDER_MOTOR_STEP_PIN   I2SO(7)  // Q7: feeder motor STEP

// === Paper auto-change flow parameters（按机型可调整） ===
// 步数需根据实际机构行程微调

// 面板电机：弹出旧纸（A4 长度 + 余量）
#define PANEL_EJECT_STEPS       8000u
// 面板电机：快速送纸阶段的最大步数上限（防止传感器异常时跑飞）
#define PANEL_FAST_STEPS_MAX    16000u
// 面板电机：反向找感应点的最大步数
#define PANEL_BACK_STEPS_MAX    4000u
// 面板电机：最终微调到位的步数
#define PANEL_FINAL_STEPS       500u

// 进纸器电机：寻找新纸到感应器的最大步数
#define FEEDER_FIND_STEPS_MAX   8000u
// 进纸器电机：纸到感应器后继续送入的步数（约 5cm）
#define FEEDER_EXTRA_STEPS      2000u

// 拾落电机：压纸 / 抬纸的步数（一次完整动作）
#define CLAMP_TOGGLE_STEPS      600u

// 方向极性（如方向相反，可将 true/false 互换）
#define PANEL_DIR_EJECT         false  // 面板电机“弹出旧纸”方向（反向）
#define PANEL_DIR_FEED          true   // 面板电机“送入新纸”方向
#define FEEDER_DIR_FORWARD      false  // 进纸器“送纸进入机器”方向（反向）
#define CLAMP_DIR_RELEASE       false  // 拾落电机“松开纸张”方向（已反向）
#define CLAMP_DIR_CLAMP         true   // 拾落电机“压紧纸张”方向

// Sensor and input pins for paper system
#define PAPER_SENSOR_PIN        GPIO_NUM_34  // HIGH=paper present, LOW=no paper
#define PAPER_CHANGE_BTN_PIN    GPIO_NUM_35  // LOW=pressed (with external pulldown)

// Map paper sensor to a macro button input so its state appears in status reports:
// When PAPER_SENSOR_PIN is HIGH (æçº¸), status line will include "Pn:0"
// When LOW (æ çº¸), "0" willä¸åºç?
#define MACRO_BUTTON_0_PIN      PAPER_SENSOR_PIN

// Map user digital outputs (M62..M65 Px) to paper-handling signals
// M62/M64 Px = ON (HIGH), M63/M65 Px = OFF (LOW)
// P0: Paper-change HR4988 enable (LOW=enable at driver, HIGH=disable)
// P1: Clamp motor DIR
// P2: Panel motor DIR
// P3: Feeder motor DIR
#define USER_DIGITAL_PIN_0      PAPER_ENABLE_PIN
#define USER_DIGITAL_PIN_1      CLAMP_MOTOR_DIR_PIN
#define USER_DIGITAL_PIN_2      PANEL_MOTOR_DIR_PIN
#define USER_DIGITAL_PIN_3      FEEDER_MOTOR_DIR_PIN

// Map user digital outputs (M62..M65 Px) to paper-handling signals
// M62/M64 Px = ON (HIGH), M63/M65 Px = OFF (LOW)
// P0: Paper-change HR4988 enable (LOW=enable, HIGH=disable) - note: logic is active LOW at driver side
// P1: Clamp motor DIR
// P2: Panel motor DIR
// P3: Feeder motor DIR
#define USER_DIGITAL_PIN_0      PAPER_ENABLE_PIN
#define USER_DIGITAL_PIN_1      CLAMP_MOTOR_DIR_PIN
#define USER_DIGITAL_PIN_2      PANEL_MOTOR_DIR_PIN
#define USER_DIGITAL_PIN_3      FEEDER_MOTOR_DIR_PIN

// NOTE: This configuration uses HARDWARE ganged motors (both Y motors share
// the same STEP/DIR signals). Therefore, Y2_STEP_PIN and Y2_DIRECTION_PIN
// are NOT defined. If you want software ganged motors with independent control,
// you would need to use different GPIO pins for the second motor.

// === Stepper Motor Definitions ===

// X Axis
#define X_STEP_PIN              GPIO_NUM_2
#define X_DIRECTION_PIN         GPIO_NUM_15

// Y Axis (Hardware ganged - both motors share STEP/DIR signals)
// Both Y motors are driven in parallel from these signals
#define Y_STEP_PIN              GPIO_NUM_13
#define Y_DIRECTION_PIN         GPIO_NUM_12

// Z Axis
// STEP on MTMS (GPIO14) JTAG pin, DIR on GPIO27 (pen up/down control)
// NOTE: Using MTMS may interfere with debugging functionality
#define Z_STEP_PIN              GPIO_NUM_14
#define Z_DIRECTION_PIN         GPIO_NUM_27

// Shared stepper enable pin for all HR4988 drivers
// Connected to GPIO4 (ESP32-WROOM-32 pin P26)
// Active LOW (nENABLE): Output LOW to enable, HIGH to disable (HR4988 standard)
#define STEPPERS_DISABLE_PIN    GPIO_NUM_4

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

#define DEFAULT_X_MAX_TRAVEL         200.0   // mm - adjust to your machine
#define DEFAULT_Y_MAX_TRAVEL         200.0   // mm - adjust to your machine
#define DEFAULT_Z_MAX_TRAVEL         20.0    // mm - pen lift height (0-20mm)
