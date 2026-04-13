#pragma once
// clang-format off

/*
    custom_3axis_hr4988.h
    Part of Grbl_ESP32

    Pin assignments for custom 3-axis CNC with HR4988 drivers
    ESP32-D0WDQ6 with 40MHz crystal

    Hardware Configuration:
    - X Axis:  GPIO27 (STEP), GPIO26 (DIR)
    - Y1Y2 Axis: 32K_XN (GPIO33/STEP), 32K_XP (GPIO32/DIR) - dual motor ganged
    - Z Axis:  MTMS (GPIO14/STEP), MTDI (GPIO12/DIR) - Pen up/down control
    - Enable:   GPIO25 (shared for all 4 HR4988 drivers, nENABLE active low)

    Note: No limit switches or position sensors are used.
    Enable pin: Output LOW to enable steppers, HIGH to disable

    Pen Control:
    - Z=0mm: Pen up (travel position)
    - Z=5mm: Pen down (writing position)
*/

#define MACHINE_NAME "Custom 3-Axis HR4988"
#define PAIXI_MODEL_NAME "PX-WRITER-3A"
#define PAIXI_DEVICE_ID "PAIXI_WRITER_3AXIS"
#define PAIXI_SECURITY_PROTOCOL "paixi_serial_v1"
#define PAIXI_BUILD_TAG "SECURE_V1 PAIXI_WRITER"
#define PAIXI_FIRMWARE_NAME "Paixi Writer Firmware"
#define PAIXI_PEN_M3_M5_CONTROL
#define PAIXI_PEN_UP_Z 0.0f
#define PAIXI_PEN_DOWN_Z 5.0f
#define PAIXI_PEN_MOTION_FEED 600.0f

// Enable software debounce since no hardware R/C filters
#define ENABLE_SOFTWARE_DEBOUNCE

// NOTE: This configuration uses HARDWARE ganged motors (both Y motors share
// the same STEP/DIR signals). Therefore, Y2_STEP_PIN and Y2_DIRECTION_PIN
// are NOT defined. If you want software ganged motors with independent control,
// you would need to use different GPIO pins for the second motor.

// === Stepper Motor Definitions ===

// X Axis
#define X_STEP_PIN              GPIO_NUM_27
#define X_DIRECTION_PIN         GPIO_NUM_26

// Y Axis (Hardware ganged - both motors share STEP/DIR signals)
// Connected to 32K_XN (GPIO33) and 32K_XP (GPIO32) crystal pins
// NOTE: These are 32.768kHz crystal pins, may interfere with RTC functionality
#define Y_STEP_PIN              GPIO_NUM_33
#define Y_DIRECTION_PIN         GPIO_NUM_32

// Z Axis
// Connected to MTMS (GPIO14) and MTDI (GPIO12) JTAG pins
// NOTE: May interfere with debugging functionality
#define Z_STEP_PIN              GPIO_NUM_14
#define Z_DIRECTION_PIN         GPIO_NUM_12

// Shared stepper enable pin for all HR4988 drivers
// Active LOW (nENABLE): Output LOW to enable, HIGH to disable (HR4988 standard)
#define STEPPERS_DISABLE_PIN    GPIO_NUM_25

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
#define DEFAULT_Z_MAX_TRAVEL         20.0    // mm - pen travel range
