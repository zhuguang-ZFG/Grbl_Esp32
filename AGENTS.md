# Grbl_Esp32 Agent Guide

This file is intended for AI coding agents working on the Grbl_Esp32 firmware repository. It describes the project architecture, build system, code organization, and development conventions as they actually exist in the repository.

## Project Overview

Grbl_Esp32 is a port of the [Grbl](https://github.com/gnea/grbl) CNC controller firmware to the Espressif ESP32, originally started by Bart Dring. The firmware runs inside the Arduino framework on FreeRTOS and is aimed at driving DIY CNC machines, laser cutters, plotters, and similar motion-control hardware.

**Important maintenance note:** This repository is in maintenance mode. The next-generation successor is [FluidNC](https://github.com/bdring/FluidNC). New features are targeted at FluidNC; Grbl_Esp32 only receives fixes for existing features.

The firmware supports up to 6 coordinated axes (XYZABC), dual-motor axes with auto-squaring, multiple spindle types (PWM, DAC, relay, RS485/VFD, laser, BESC), limit/homing switches, coolant control, SD card job execution, and a built-in web user interface. Connectivity options include USB serial, Bluetooth serial, Wi-Fi (station and access point), Telnet, and OTA updates.

## Technology Stack

- **Target MCU:** Espressif ESP32 (default board `esp32dev`).
- **Framework:** Arduino for ESP32 (`framework = arduino` in `platformio.ini`).
- **RTOS:** FreeRTOS (included via the Arduino-ESP32 core).
- **Build system:** [PlatformIO](https://platformio.org/) Core, with optional Arduino IDE support.
- **IDE support:** Visual Studio Code with the PlatformIO IDE extension (recommended), Visual Studio via a generated `.vcxproj` file, or the Arduino IDE.
- **Web UI build:** Node.js + Gulp 4 inside the `embedded/` directory.
- **License:** GPLv3 (see `LICENSE`).

## Project Structure

```
D:/Users/Grbl_Esp32
├── Grbl_Esp32/
│   ├── Grbl_Esp32.ino          # Arduino entry point (setup/loop)
│   ├── data/                   # Static web assets (index.html.gz, favicon.ico)
│   ├── src/                    # Main firmware source code
│   │   ├── Machines/           # Per-machine hardware definitions
│   │   ├── Motors/             # Stepper, servo, Trinamic, solenoid, unipolar, etc.
│   │   ├── Spindles/           # Spindle/laser driver implementations
│   │   ├── WebUI/              # Wi-Fi, BT, HTTP, Telnet, settings, notifications
│   │   └── tests/              # Sample G-code/NC files for manual testing
│   └── Custom/                 # Machine-specific extension code
├── libraries/                  # Out-of-tree bundled libraries
│   ├── arduinoWebSockets/
│   └── ESP32SSDP/
├── embedded/                   # Web UI source and Gulp build pipeline
│   ├── www/                    # Original HTML/JS/CSS assets
│   ├── gulpfile.js             # Gulp tasks to package www into index.html.gz
│   └── package.json
├── doc/                        # Documentation and CSV code tables
├── platformio.ini              # PlatformIO project configuration
├── debug.ini                   # Optional debug environment overrides
├── configure-features.py       # Toggle compile-time features in Config.h
├── build-machine.py            # Build for a single machine file
├── build-all.py / build-all.sh / build-all.ps1  # Build all machine files
├── generate_vcxproj.py         # Generate Visual Studio project files
└── CodingStyle.md              # Human-readable coding style guide
```

### Source Code Organization (`Grbl_Esp32/src/`)

- **`Grbl_Esp32.ino`** — minimal Arduino sketch; includes `src/Grbl.h` and calls `grbl_init()` / `run_once()`.
- **`Grbl.h` / `Grbl.cpp`** — top-level header and initialization/main-loop logic. `Grbl.h` pulls in all subsystem headers and declares weak hook functions for custom behavior.
- **`Config.h`** — compile-time configuration and feature toggles. The feature block between `CONFIGURE_EYECATCH_BEGIN` and `CONFIGURE_EYECATCH_END` is managed by `configure-features.py`.
- **`Machine.h`** — selects the active machine definition. It defaults to `Machines/custom_3axis_hr4988.h` unless `MACHINE_FILENAME` is defined externally (e.g. via `PLATFORMIO_BUILD_FLAGS`).
- **`MachineCommon.h`** — common machine-level defaults and helper macros.
- **`Defaults.h`** — default Grbl setting values.
- **Core subsystems:** `System.h/cpp`, `Protocol.cpp`, `GCode.cpp`, `Planner.cpp`, `Stepper.cpp`, `MotionControl.cpp`, `Limits.cpp`, `Probe.cpp`, `Jog.cpp`, `Report.cpp`, `Serial.cpp`, `Uart.cpp`, `Settings.cpp`, `SettingsDefinitions.cpp`, `Pins.cpp`, `I2SOut.cpp`, `CoolantControl.cpp`, `UserOutput.cpp`, etc.
- **`Motors/`** — motor driver classes (`StandardStepper`, `TrinamicDriver`, `RcServo`, `UnipolarMotor`, `Dynamixel2`, `Solenoid`, `NullMotor`, plus `Motors.cpp` for initialization).
- **`Spindles/`** — spindle abstraction and implementations (`PWMSpindle`, `Laser`, `RelaySpindle`, `DacSpindle`, `10vSpindle`, `BESCSpindle`, `VFDSpindle`, `HuanyangSpindle`, `YL620Spindle`, `TecoL510`, `H2ASpindle`, `NullSpindle`).
- **`WebUI/`** — networking stack: `WifiConfig`, `BTConfig`, `WebServer`, `WebSettings`, `Serial2Socket`, `TelnetServer`, `NotificationsService`, `InputBuffer`, `Commands`, `Authentication`, `ESPResponse`, `JSONEncoder`.
- **`CustomCode.cpp`** — includes the file named by `CUSTOM_CODE_FILENAME` if defined, otherwise provides weak stubs.

### Machine Definitions (`Grbl_Esp32/src/Machines/`)

Each `.h` file is a complete hardware pinout and default-settings package. Examples include:

- `test_drive.h` — safe, no-I/O configuration for first-time testing or OTA recovery.
- `custom_3axis_hr4988.h` — the repository default; a 3-axis plotter/writer using HR4988 stepper drivers, I2S-driven 74HC595 expander, paper-handling motors, and Bluetooth as the default radio.
- Other boards: `3axis_v4.h`, `mpcnc_v1p2.h`, `lowrider_v1p2.h`, `6_pack_stepstick_v1.h`, `polar_coaster.h`, `atari_1020.h`, etc.

A machine file defines pins, axis count, stepper/spindle types, default settings, homing cycles, and optional feature macros such as `CUSTOM_CODE_FILENAME` or `USE_I2S_OUT`.

### Custom Extensions (`Grbl_Esp32/Custom/`)

Machine-specific behavior is implemented by creating or editing a `.cpp` file in `Grbl_Esp32/Custom/` and pointing to it from the machine file:

```cpp
#define CUSTOM_CODE_FILENAME "Custom/paper_system.cpp"
```

The main code provides weak definitions for hooks such as `machine_init()`, `display_init()`, `user_defined_homing()`, `cartesian_to_motors()`, `kinematics_pre_homing()`, `user_tool_change()`, `user_defined_macro()`, and `user_m30()`. A template listing all weak hooks is in `Grbl_Esp32/Custom/custom_code_template.cpp`.

## Build System

### PlatformIO (recommended)

Key configuration is in `platformio.ini`:

- `src_dir = Grbl_Esp32`
- `lib_dir = libraries`
- `data_dir = Grbl_Esp32/data`
- Default environment: `release`
- Platform: `espressif32@3.0.0`
- Board: `esp32dev`
- Framework: `arduino`
- CPU: 240 MHz, flash 80 MHz QIO
- Partition table: `min_spiffs.csv`
- Monitor speed: 115200 bps with CRLF line endings and the ESP32 exception decoder

External library dependencies for `release` and `debug` environments:

- `TMCStepper@>=0.7.0,<1.0.0`
- `ESP8266 and ESP32 OLED driver for SSD1306 displays@^4.2.0`

### Common Build Commands

```bash
# Build the default machine selected in Grbl_Esp32/src/Machine.h
platformio run

# Build a specific machine without editing Machine.h
PLATFORMIO_BUILD_FLAGS=-DMACHINE_FILENAME=test_drive.h platformio run

# On Windows PowerShell
$env:PLATFORMIO_BUILD_FLAGS='-DMACHINE_FILENAME=test_drive.h'; platformio run

# Upload firmware
platformio run --target upload --upload-port COM7

# Monitor serial output
platformio device monitor

# Build and upload SPIFFS data partition (web UI)
platformio run --target uploadfs
```

### Helper Scripts

- `build-machine.py [-q] [-u] <machine_file.h>` — builds a single machine; `-u` also uploads.
- `build-all.py [-v]` / `build-all.sh [-v]` / `build-all.ps1` — builds every `.h` in `Grbl_Esp32/src/Machines/`. Output is filtered unless `-v` is given.
- `configure-features.py -e FEATURE [-d FEATURE]` — enables/disables compile-time features inside the `CONFIGURE_EYECATCH_*` block of `Config.h`. Valid features: `BLUETOOTH`, `WIFI`, `SD_CARD`, `HTTP`, `OTA`, `TELNET`, `TELNET_WELCOME_MSG`, `MDNS`, `SSDP`, `NOTIFICATIONS`, `SERIAL2SOCKET_IN`, `SERIAL2SOCKET_OUT`, `CAPTIVE_PORTAL`, `AUTHENTICATION`.
- `generate_vcxproj.py` — generates `Grbl_Esp32.vcxproj` and `.vcxproj.filters` for Visual Studio (see `VisualStudio.md`).
- `clear-flags.ps1` — clears the leftover `PLATFORMIO_BUILD_FLAGS` environment variable.

### Debug Environment

`debug.ini` contains optional overrides (upload port, debug probe). To use it, uncomment `extra_configs=debug.ini` in `platformio.ini`. It sets `MACHINE_FILENAME=test_drive.h` by default.

### Arduino IDE

The project can also be compiled with the Arduino IDE. Install the ESP32 board support package, open `Grbl_Esp32/Grbl_Esp32.ino`, select the ESP32 Dev Module, and verify/upload. Some advanced features (e.g. `MACHINE_FILENAME` injection) are not convenient in the Arduino IDE.

### Web UI Build

The web UI assets live in `embedded/www/` and are processed by Gulp:

```bash
cd embedded
npm install
gulp           # default development build
gulp package   # minified + gzipped output (produces index.html.gz)
```

The gzipped `index.html.gz` is placed in `Grbl_Esp32/data/` and uploaded to SPIFFS with `platformio run --target uploadfs`.

## Testing Strategy

- **Compile-all regression:** `build-all.py` (or `.sh`/`.ps1`) compiles every machine definition. This is the primary automated smoke test.
- **Feature-matrix regression:** The legacy `.travis.yml` shows the project historically tested:
  1. Wi-Fi enabled, Bluetooth disabled → build `test_drive.h`
  2. Bluetooth enabled, Wi-Fi disabled → build `test_drive.h`
  3. Both enabled → build all machines
- **Manual runtime tests:** `Grbl_Esp32/src/tests/` contains sample NC/G-code files such as `arcs_arrows.nc`, `parser.nc`, `spindle_testing.nc`, and `user_io.nc`. These are intended for manual machine/runtime verification, not automated unit tests.
- **Test-drive mode:** Compiling with `test_drive.h` creates a virtual machine with no pins driven. It is safe to run on an unattached ESP32 and lets you verify Wi-Fi, Bluetooth, and the web UI without hardware.

There are no formal unit-test frameworks in this repository; validation is build-based and hardware-in-the-loop.

## Code Style Guidelines

The project enforces style through `.clang-format`. Most modern IDEs pick it up automatically; run it manually before committing if your editor does not.

Key conventions (from `CodingStyle.md` and `.clang-format`):

- **Language standard:** C++11 (`Standard: Cpp11`).
- **Indentation:** 4 spaces, never tabs.
- **Column limit:** 140 characters.
- **Braces:** Allman/Stroustrup style (`BreakBeforeBraces: Allman`).
- **Pointer alignment:** left (`int* ptr`).
- **Includes:**
  - Use `"..."` for local/project headers and `<...>` for system/library headers.
  - A `.cpp` file should include its corresponding `.h` first, then other dependencies.
  - Never include a `.cpp` file.
- **Header guards:** use `#pragma once`.
- **Namespaces/classes:**
  - Filenames correspond to class names; folder names correspond to namespace names.
  - Classes/namespaces use `CamelCase`.
  - Class member functions use `snake_case`.
  - Class member variables use `_snake_case` with a leading underscore.
  - Namespaces should contain classes; avoid free functions without a class.
- **`using namespace`:** not allowed in headers except inside function bodies; use conservatively in `.cpp` files.
- **Machine definition files** should have `// clang-format off` at the top because clang-format does not handle dense pin tables well.
- You may temporarily disable formatting with `// clang-format off` / `// clang-format on`.

## Development Conventions

- **Do not edit `Config.h` directly to toggle features** unless you are making a one-off change. Use `configure-features.py` for scripted/automated changes so the eyecatch block stays consistent.
- **Do not hard-code the machine in `Machine.h` for CI/automation.** Pass `MACHINE_FILENAME` via `PLATFORMIO_BUILD_FLAGS`.
- **Custom behavior belongs in `Grbl_Esp32/Custom/`.** Prefer weak-function overrides over editing core files.
- **Pin numbers:** use `GPIO_NUM_xx` constants in machine files. `UNDEFINED_PIN` (`255`) means unassigned. I2S-expanded pins use `I2SO(n)`.
- **Serial monitor:** 115200 baud. The ESP32 boot log is always at 115200, so using a different baud rate hides boot messages.
- **Default radio mode** is selected in `Config.h` based on available features: Bluetooth preferred if both Bluetooth and Wi-Fi are enabled (unless `CONNECT_TO_SSID` is set).

## Security Considerations

- **Authentication is optional and weak.** `ENABLE_AUTHENTICATION` adds an admin/user password scheme, but the current implementation stores and transmits passwords in cleartext over unencrypted channels. The codebase itself comments that it should be treated as a "friendly suggestion" rather than effective security against a malicious attacker.
- **Bluetooth serial and Telnet expose the same G-code command surface as USB serial.** If you enable these, anyone within range can send motion and configuration commands unless the network is isolated.
- **OTA updates** are enabled by default (`ENABLE_OTA`). Ensure the device is on a trusted network.
- **mDNS/SSDP/Captive Portal** discovery services are enabled by default when Wi-Fi is on. This advertises the device on the local network.
- **SD card and web file access** can read, delete, and run arbitrary G-code files via `[ESP...]` commands.
- **Avoid committing secrets.** `Config.h` contains placeholder Wi-Fi credentials (`CONNECT_TO_SSID`, `SSID_PASSWORD`) that are commented out by default; do not un-comment and commit real credentials.

## Useful Reference Files

- `README.md` — project introduction and feature list.
- `CodingStyle.md` — detailed style guidance.
- `VisualStudio.md` — how to set up Visual Studio (not VS Code).
- `doc/Commands.txt` — reference for `[ESP...]` commands.
- `doc/csv/` — error codes, alarm codes, setting codes, build-option codes.
- `配置.md` — Chinese hardware/firmware configuration notes for the default `custom_3axis_hr4988.h` plotter/writer setup.
- `Grbl_Esp32/Custom/custom_code_template.cpp` — template for machine-specific code.

## Quick Start for Agents

1. Install PlatformIO Core and (optionally) the VS Code PlatformIO IDE.
2. Build the default configuration: `platformio run`.
3. For a safe first upload, switch to `test_drive.h`:
   - `PLATFORMIO_BUILD_FLAGS=-DMACHINE_FILENAME=test_drive.h platformio run --target upload`
4. Verify with the serial monitor: `platformio device monitor`.
5. When adding a feature or fixing a bug, run `build-all.py` to ensure all machine definitions still compile.
