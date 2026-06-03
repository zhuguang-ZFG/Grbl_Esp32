---
name: grbl-paper-system
description: Paper handling system for Grbl_Esp32 - auto paper change, sensors, motors, and related commands. Use when debugging paper change issues, modifying paper system behavior, or working with paper sensors/motors.
---

# Paper Handling System

## Overview

The paper system controls automatic paper changing for a pen plotter/CNC with:
- 3 motors: Panel (送纸), Feeder (进纸器), Clamp (拾落)
- Paper sensor on GPIO34 (LOW=paper present, HIGH=no paper)
- Physical button on GPIO35 (LOW=pressed) for manual trigger

## Commands

| Command | Description |
|---------|-------------|
| `[ESP910]` | Auto paper change (full cycle) |
| `[ESP901]` | Paper sensor status |
| `[ESP911]` | Panel motor jog forward |
| `[ESP912]` | Panel motor jog reverse |
| `[ESP913]` | Feeder motor jog |
| `[ESP930]` | Feeder feed paper |
| `M701` | Paper system init |
| `M711` | Panel motor forward |
| `M712` | Panel motor reverse |
| `M713` | Feeder motor forward |
| `M716` | Paper sensor status |

## Auto Change Flow (`paper_auto_change()`)

```
Step 1: Eject old paper (panel motor, 8000 steps)
Step 2: Feeder search for new paper (max 12000 steps)
Step 3: Feed paper to clamp position
Step 4: Clamp (lift motor down)
Step 5: Feed paper to final position
Step 6: Panel advance (clamp still down)
Step 7: Lift motor up (release paper)
Step 8: Panel final advance
```

## Error Codes

| PaperStatus | Meaning |
|-------------|---------|
| 0 | Success |
| 2 | Feeder timeout - sensor not triggered |
| Other | Various motor/sensor failures |

## Hardware Configuration (custom_3axis_hr4988.h)

```cpp
// DAC Current (GPIO25 shared)
#define PAPER_DRIVER_REF_DAC    80    // Default (XYZ+paper)
#define PAPER_REF_DAC_CLAMP   160    // Clamp motor
#define PAPER_REF_DAC_PANEL   90     // Panel motor
#define PAPER_REF_DAC_FEEDER  110    // Feeder motor

// Paper Sensor
#define PAPER_SENSOR_PIN     GPIO_NUM_34  // HIGH=no paper, LOW=paper

// Paper Change Button
#define PAPER_CHANGE_BTN_PIN GPIO_NUM_35  // LOW=pressed
```

## Button Debounce (Long Press)

```cpp
#define PAPER_BTN_STABLE_SAMPLES  10    // 10 samples
#define PAPER_BTN_STABLE_MS       800   // 800ms stable confirm
#define PAPER_BTN_HOLD_MS         200   // 200ms continued hold
// Total: ~1 second long press to trigger
```

## I2S Shift Register (74HC595D)

Outputs Q0-Q7 control paper system:
- Q0: LED (HIGH=off, LOW=on)
- Q1: HR4988 Enable (LOW=enable, HIGH=disable)
- Q2: Clamp DIR
- Q3: Clamp STEP
- Q4: Panel DIR
- Q5: Panel STEP
- Q6: Feeder DIR
- Q7: Feeder STEP

## Debugging

Check serial output for:
```
[MSG:[PaperAuto] Starting auto paper change...]
[MSG:[PaperAuto-1] Ejecting old paper (8000 steps)...]
[MSG:[PaperAuto-1] Done]
[MSG:[PaperAuto-2] Feeder searching for paper...]
```

If Step 2 fails with "Feeder timeout", check:
1. Paper is loaded in feeder
2. Feeder motor direction is correct
3. Paper sensor is connected and working
