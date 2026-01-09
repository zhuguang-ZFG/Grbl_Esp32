#pragma once
// clang-format off

/*
    custom_3axis_hr4988.h
    Part of Grbl_ESP32

    Pin assignments for custom 3-axis CNC with HR4988 drivers
    ESP32-D0WDQ6 with 40MHz crystal

    Hardware Configuration:
    - X Axis:  GPIO27 (STEP), GPIO26 (DIR)
    - Y Axis:  GPIO13 (STEP), GPIO12 (DIR) - dual motor ganged (Y1Y2)
    - Z Axis:  GPIO17 (STEP), GPIO18 (DIR)
    - Enable:   GPIO25 (shared for all 4 HR4988 drivers, active LOW)

    Note: No limit switches or position sensors are used.
    Enable pin: Output LOW to enable steppers, HIGH to disable
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
#define X_STEP_PIN              GPIO_NUM_27
#define X_DIRECTION_PIN         GPIO_NUM_26

// Y Axis (Hardware ganged - both motors share STEP/DIR signals)
// Y1 and Y2 motors are connected in parallel to the same signals
#define Y_STEP_PIN              GPIO_NUM_13
#define Y_DIRECTION_PIN         GPIO_NUM_12

// Z Axis
#define Z_STEP_PIN              GPIO_NUM_17
#define Z_DIRECTION_PIN         GPIO_NUM_18

// Shared stepper enable pin for all HR4988 drivers
// Active LOW: Output LOW to enable, HIGH to disable
#define STEPPERS_DISABLE_PIN    GPIO_NUM_25

// Note: No limit switches are used in this configuration
// Uncomment and configure if you add limit switches later
// #define X_LIMIT_PIN             GPIO_NUM_XX
// #define Y_LIMIT_PIN             GPIO_NUM_XX
// #define Z_LIMIT_PIN             GPIO_NUM_XX

// === Default Settings ===

#define DEFAULT_STEP_PULSE_MICROSECONDS     3
#define DEFAULT_STEPPER_IDLE_LOCK_TIME      255  // Keep steppers enabled

#define DEFAULT_STEPPING_INVERT_MASK        0  // uint8_t
#define DEFAULT_DIRECTION_INVERT_MASK        0  // uint8_t
#define DEFAULT_INVERT_ST_ENABLE             1  // boolean (1 = active low enable)

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

// Motor parameters (adjust based on your motors)
#define DEFAULT_X_STEPS_PER_MM       200.0
#define DEFAULT_Y_STEPS_PER_MM       200.0
#define DEFAULT_Z_STEPS_PER_MM       200.0

#define DEFAULT_X_MAX_RATE           5000.0  // mm/min
#define DEFAULT_Y_MAX_RATE           5000.0  // mm/min
#define DEFAULT_Z_MAX_RATE           3000.0  // mm/min

#define DEFAULT_X_ACCELERATION       500.0   // mm/sec^2
#define DEFAULT_Y_ACCELERATION       500.0   // mm/sec^2
#define DEFAULT_Z_ACCELERATION       300.0   // mm/sec^2

#define DEFAULT_X_MAX_TRAVEL         200.0   // mm - adjust to your machine
#define DEFAULT_Y_MAX_TRAVEL         200.0   // mm - adjust to your machine
#define DEFAULT_Z_MAX_TRAVEL         50.0    // mm - adjust to your machine
