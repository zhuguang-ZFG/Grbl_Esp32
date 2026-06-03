---
name: grbl-machine-config
description: Machine configuration for Grbl_Esp32 - pin assignments, stepper settings, motor parameters. Use when modifying machine hardware config, changing pin assignments, or adjusting stepper/motor settings.
---

# Machine Configuration

## File Location

Machine configs are in `Grbl_Esp32/src/Machines/`. Each `.h` file defines one machine.

Select machine in `platformio.ini`:
```ini
build_flags = -DMACHINE_FILENAME=custom_3axis_hr4988.h
```

Or in `Config.h`:
```cpp
#define MACHINE_FILENAME "custom_3axis_hr4988.h"
```

## Configuration Sections

### 1. Machine Identity
```cpp
#define MACHINE_NAME "Custom 3-Axis HR4988"
#define GRBL_PAPER_SYSTEM 1           // Enable paper system
#define CUSTOM_CODE_FILENAME "Custom/paper_system.cpp"
#define USE_USER_M_CODES
```

### 2. Axis Pin Definitions
```cpp
// X Axis
#define X_STEP_PIN              GPIO_NUM_2
#define X_DIRECTION_PIN         GPIO_NUM_15

// Y Axis (Hardware ganged)
#define Y_STEP_PIN              GPIO_NUM_13
#define Y_DIRECTION_PIN         GPIO_NUM_12

// Z Axis (Pen up/down)
#define Z_STEP_PIN              GPIO_NUM_14
#define Z_DIRECTION_PIN         GPIO_NUM_27

// Shared enable (active LOW)
#define STEPPERS_DISABLE_PIN    GPIO_NUM_4
```

### 3. Default Settings
```cpp
#define DEFAULT_X_STEPS_PER_MM              200.0
#define DEFAULT_Y_STEPS_PER_MM              200.0
#define DEFAULT_Z_STEPS_PER_MM              400.0

#define DEFAULT_X_MAX_RATE                  5000.0   // mm/min
#define DEFAULT_Y_MAX_RATE                  5000.0
#define DEFAULT_Z_MAX_RATE                  1000.0

#define DEFAULT_X_ACCELERATION              500.0    // mm/sec^2
#define DEFAULT_Y_ACCELERATION              500.0
#define DEFAULT_Z_ACCELERATION              400.0

#define DEFAULT_X_MAX_TRAVEL                200.0    // mm
#define DEFAULT_Y_MAX_TRAVEL                200.0
#define DEFAULT_Z_MAX_TRAVEL                20.0

#define DEFAULT_STEPPER_IDLE_LOCK_TIME      255      // Always enabled
#define DEFAULT_DIRECTION_INVERT_MASK       bit(Z_AXIS)  // Z inverted
```

### 4. DAC Current (GPIO25)
All HR4988 drivers share GPIO25 DAC for reference voltage:
```cpp
#define PAPER_DRIVER_REF_PIN    GPIO_NUM_25
#define PAPER_DRIVER_REF_DAC    80     // Default: ~0.31A
#define PAPER_REF_DAC_CLAMP   160      // Clamp: ~0.62A
#define PAPER_REF_DAC_PANEL   90       // Panel: ~0.35A
#define PAPER_REF_DAC_FEEDER  110      // Feeder: ~0.43A
```

Formula: `I = DAC / 255 * 3.3V / (8 * 0.1Ω)`

### 5. I2S Shift Register (74HC595D)
```cpp
#define USE_I2S_OUT
#define I2S_OUT_DATA            GPIO_NUM_21
#define I2S_OUT_BCK             GPIO_NUM_16
#define I2S_OUT_WS              GPIO_NUM_17
```

### 6. Control Pins
```cpp
// Macro0 = Paper change button (GPIO35, LOW=pressed)
#define MACRO_BUTTON_0_PIN      PAPER_CHANGE_BTN_PIN

// Invert mask: bit4 (Macro0) + bit0-3 (standard)
#define INVERT_CONTROL_PIN_MASK B00011111
```

### 7. Timing Parameters
```cpp
// Step pulse timing
#define STEP_PULSE_DELAY                15   // µs
#define DEFAULT_STEP_PULSE_MICROSECONDS 10   // µs

// Panel motor speeds
#define PAPER_RAMP_STEPS         40u    // Ramp-up steps
#define PAPER_RAMP_HI_US         400u   // Ramp high time
#define PAPER_RAMP_LO_US         400u   // Ramp low time
#define PAPER_NORMAL_HI_US       150u   // Normal high time
#define PAPER_NORMAL_LO_US       150u   // Normal low time
```

## Runtime Commands

After upload, settings can be adjusted via serial:
```bash
$0=10    Step pulse time (µs)
$1=255   Stepper idle lock time (ms, 255=always)
$2=0     Step port invert mask
$3=4     Direction port invert mask (bit2=Z)
```

## Important Notes

- Pin assignments must match actual hardware wiring
- DAC values affect motor current and torque
- Direction masks (`*_DIR_*`) may need adjustment per motor orientation
- Always verify with serial monitor after changes
