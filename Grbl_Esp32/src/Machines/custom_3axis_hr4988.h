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
    - Paper sensor:         GPIO34 (HIGH=no paper, LOW=paper present)
    - Paper-change button:  GPIO35 (LOW=pressed, with pulldown)

    Note: No limit switches or position sensors are used.
    Enable pin: Output LOW to enable steppers, HIGH to disable

    Pen Control:
    - Z=0mm: Pen down (writing position)
    - Z=20mm: Pen up (travel position)

    Jog: Z 轴点动（抬落笔）单独限速，避免 $J= 使用较大 F 时 Z 过快。
*/
#ifndef JOG_Z_MAX_FEED_MM_PER_MIN
#define JOG_Z_MAX_FEED_MM_PER_MIN  1000   // Z 轴点动最大进给 mm/min，含 Z 的点动会被限制到此值
#endif

#define MACHINE_NAME "Custom 3-Axis HR4988"
#define GRBL_PAPER_SYSTEM 1  /* æ¢çº¸ç³»ç» M701/M711/M712/M713 å?GCode.cpp ä¸­ç´åå®ç?*/

// Use custom machine code (Custom/paper_system.cpp)
#define CUSTOM_CODE_FILENAME "Custom/paper_system.cpp"
// Enable user-defined M codes (handled in paper_system.cpp)
#define USE_USER_M_CODES

// === Control pin invert mask ===
// 默认在 Config.h 中定义为 B00001111（仅反相 SafetyDoor/Reset/FeedHold/CycleStart）。
// 本机型的“一键换纸”按键接在 GPIO35，实际接法为 LOW=按下、HIGH=松开（外部下拉），
// 所以需要把 Macro0 也设置为“低电平有效”，避免松开时被视为按下。
#ifdef INVERT_CONTROL_PIN_MASK
#    undef INVERT_CONTROL_PIN_MASK
#endif
// 位顺序: Macro3 | Macro2 | Macro1 | Macro0 | CycleStart | FeedHold | Reset | SafetyDoor
// 这里在原来的低 4 位基础上，额外打开 Macro0（bit4），得到 B00011111。
#define INVERT_CONTROL_PIN_MASK B00011111

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
#define PAPER_DRIVER_REF_DAC    100          // 直连时建议 80–150（≈1.0–1.9V），过大易过流发热；60≈0.75V
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
#define PANEL_EJECT_STEPS       12000u
// 面板电机：快速送纸阶段的最大步数上限（防止传感器异常时跑飞）
#define PANEL_FAST_STEPS_MAX    16000u
// 面板电机：反向找感应点的最大步数（需覆盖第6步快进的最远位置）
// 快进约 8.5k 步时脱离传感器，这里给到 9000 步保证能退回到感应点
#define PANEL_BACK_STEPS_MAX    9000u
// 面板电机：最终微调到位的步数
#define PANEL_FINAL_STEPS       1110u

// 进纸器电机：寻找新纸到感应器的最大步数（超时时间，可根据实际距离调大）
#define FEEDER_FIND_STEPS_MAX   12000u
// 进纸器电机：纸到感应器后继续送入的步数（约 8cm）
#define FEEDER_EXTRA_STEPS      9000u

// 拾落电机：压纸 / 抬纸的步数（一次完整动作），经实测约 220 步
#define CLAMP_TOGGLE_STEPS      470u

// 步进脉冲时序（μs）：在 PaperSystem.cpp 的 paper_step_pulses 中使用
// 面板/进纸器：起步阶段用较慢脉宽减小冲击，之后切换为正常速度
#define PAPER_RAMP_STEPS         40u    // 起步缓运行步数
#define PAPER_RAMP_HI_US         400u   // 起步阶段高电平 μs
#define PAPER_RAMP_LO_US         400u   // 起步阶段低电平 μs
#define PAPER_NORMAL_HI_US       150u   // 面板/进纸器正常高速 μs
#define PAPER_NORMAL_LO_US       150u
#define PAPER_CLAMP_HI_US        2000u  // 拾落电机脉宽 μs
#define PAPER_CLAMP_LO_US        2000u

// 面板电机方向：三个独立宏，只改需要反的那一个即可
// - PANEL_DIR_FEED:    第6步快速进纸、第8步最终对位（送新纸方向）
// - PANEL_DIR_EJECT:   第1步弹出旧纸
// - PANEL_DIR_REVERSE: 第7步回找传感器（与 FEED 同向）
#define PANEL_DIR_FEED          false  // 送新纸 / 快速进纸 / 最终对位（你反馈进纸反了，只改此项）
#define PANEL_DIR_EJECT         false  // 弹出旧纸（单独调，不动）
#define PANEL_DIR_REVERSE       true   // 回找传感器（你反馈此方向反了，单独取反）
#define FEEDER_DIR_FORWARD      false  // 进纸器“送纸进入机器”方向（反向）
#define CLAMP_DIR_RELEASE       true   // 拾落电机“松开纸张”方向（再次反向）
#define CLAMP_DIR_CLAMP         false  // 拾落电机“压紧纸张”方向

// Sensor and input pins for paper system
// 实测接法：挡住传感器时为 LOW，松开为 HIGH；代码里已按“LOW=paper present, HIGH=no paper”处理
#define PAPER_SENSOR_PIN        GPIO_NUM_34
#define PAPER_CHANGE_BTN_PIN    GPIO_NUM_35  // LOW=pressed (with external pulldown)

// 一键换纸物理按键 → Macro0，后端在 Custom/paper_system.cpp 里做了额外软件去抖。
// 注意：按键为“低电平按下”，已在上方 INVERT_CONTROL_PIN_MASK 中把 Macro0 置为低电平有效。
#define MACRO_BUTTON_0_PIN      PAPER_CHANGE_BTN_PIN

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

// 方向建立时间（µs）：DIR 变化后到第一个 STEP 前的延时，减轻 Z 轴卡顿感（仍卡顿可试 20µs 或串口 $ 调大）
#ifndef STEP_PULSE_DELAY
#define STEP_PULSE_DELAY                    15  // $Stepper/Direction/Delay 默认 15µs
#endif
#define DEFAULT_STEP_PULSE_MICROSECONDS     10  // Increased for better driver reliability and to prevent Z-axis stuttering
#define DEFAULT_STEPPER_IDLE_LOCK_TIME      250

#define DEFAULT_STEPPING_INVERT_MASK        0  // uint8_t
#define DEFAULT_DIRECTION_INVERT_MASK        0  // Z 不反相；若抬笔/落笔仍反，改为 bit(Z_AXIS) 并重新烧录或 $3=4 $S
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
#define DEFAULT_Z_MAX_RATE           1000.0  // Reduced speed to prevent Z-axis stuttering

#define DEFAULT_X_ACCELERATION       500.0   // mm/sec^2
#define DEFAULT_Y_ACCELERATION       500.0   // mm/sec^2
#define DEFAULT_Z_ACCELERATION       400.0   // Reduced acceleration to prevent Z-axis stuttering

#define DEFAULT_X_MAX_TRAVEL         200.0   // mm - adjust to your machine
#define DEFAULT_Y_MAX_TRAVEL         200.0   // mm - adjust to your machine
#define DEFAULT_Z_MAX_TRAVEL         20.0    // mm - pen lift height (0-20mm)
