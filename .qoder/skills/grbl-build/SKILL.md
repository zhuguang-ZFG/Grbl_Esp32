---
name: grbl-build
description: Compile and upload Grbl_Esp32 firmware to ESP32. Use when building firmware, uploading to device, or troubleshooting build issues. Handles clean builds for Custom/ directory changes.
---

# Grbl_Esp32 Build & Upload

## Build Command

```powershell
cd d:\Grbl_Esp32; python -m platformio run -e release --target upload
```

## Clean Build (Required after Custom/ changes)

```powershell
cd d:\Grbl_Esp32; python -m platformio run -e release --target clean
# Then full rebuild:
cd d:\Grbl_Esp32; python -m platformio run -e release --target upload
```

## Critical: Custom/ Directory Cache Issue

PlatformIO caches object files in `.pio/build/`. When modifying files in `Grbl_Esp32/Custom/`, the build system may NOT detect changes and upload stale firmware.

**Symptoms**: Upload reports SUCCESS but device still runs old code.

**Fix**: Always run `--target clean` before rebuild when Custom/ files change.

## Build Statistics

After successful build, PlatformIO reports:
- `RAM: [===] 31.1%` - RAM usage (ESP32: 320KB)
- `Flash: [=========] 94.2%` - Flash usage (ESP32: 4MB)

## Upload Protocol

- Default: `esptool` via USB serial
- Auto-detected port (typically COM3)
- Baud rate: 921600

## Verify Upload

After upload, ESP32 automatically resets. Check serial output for:
```
Grbl 1.3a ['$' for help]
```

If old messages appear, perform clean build.
