---
name: grbl-custom-code
description: Add custom code to Grbl_Esp32 via the Custom/ directory. Use when creating custom machine behaviors, hooking into Grbl lifecycle, or adding custom M-codes.
---

# Grbl_Esp32 Custom Code

## Architecture

Custom code is loaded via `CustomCode.cpp`:
```cpp
#ifdef CUSTOM_CODE_FILENAME
#    include CUSTOM_CODE_FILENAME  // e.g., "Custom/paper_system.cpp"
#endif
```

Define `CUSTOM_CODE_FILENAME` in your machine header (`src/Machines/xxx.h`):
```cpp
#define CUSTOM_CODE_FILENAME "Custom/paper_system.cpp"
#define USE_USER_M_CODES
```

## Available Hook Points

| Hook | Signature | When Called |
|------|-----------|-------------|
| `machine_init()` | `void machine_init()` | Grbl startup, before main loop |
| `user_m_code()` | `Error user_m_code(uint16_t code)` | Unrecognized M-code |
| `user_m30()` | `void user_m30()` | After M30 (program end) |
| `check_license()` | `bool check_license()` | License verification |
| `paper_last_change_ok()` | `bool paper_last_change_ok()` | Paper change status |

## Include Requirements

Custom files are included inside `CustomCode.cpp` after `#include "Grbl.h"`. Do NOT re-include Grbl.h:

```cpp
// paper_system.cpp
// Included via CustomCode.cpp, Grbl.h already included

static void my_hook() {
    // Direct access to all Grbl symbols
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[MyHook] Running");
    sys.state  // System state
    // ... all Grbl globals available
}
```

## Adding Custom M-Codes

1. Define in machine header:
```cpp
#define USE_USER_M_CODES
```

2. Implement handler in custom file:
```cpp
Error user_m_code(uint16_t code) {
    switch (code) {
        case 701:
            // Your custom M701 logic
            return Error::Ok;
        case 702:
            // Your custom M702 logic
            return Error::Ok;
    }
    return Error::GcodeUnsupportedCommand;
}
```

## Sending Messages

```cpp
grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[Tag] Message: %d", value);
grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning, "[Tag] Warning message");
grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Error, "[Tag] Error message");
```

## WebUI Input Buffer

Inject commands into Grbl's input buffer:
```cpp
char line[32];
strcpy(line, "[ESP910]\r");
WebUI::inputBuffer.push(line);
```

## NVS (Non-Volatile Storage)

```cpp
Preferences prefs;
prefs.begin("my_namespace", false);  // false = read/write
prefs.putULong("key", value);
prefs.end();

prefs.begin("my_namespace", true);   // true = read-only
uint32_t val = prefs.getULong("key", 0);
prefs.end();
```

## Important Notes

- Custom files run in Grbl's main task context
- Avoid long blocking operations (use `delay()` sparingly)
- For debouncing, use `ENABLE_CONTROL_SW_DEBOUNCE` + `user_defined_macro()`
- Always perform clean build after modifying Custom/ files
