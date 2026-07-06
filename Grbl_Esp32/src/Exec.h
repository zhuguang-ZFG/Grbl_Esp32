#pragma once

#include <map>

// System executor bit map. Used internally by realtime protocol as realtime command flags,
// which notifies the main program to execute the specified realtime command asynchronously.
// NOTE: The system executor uses an unsigned 8-bit volatile variable (8 flag limit.) The default
// flags are always false, so the realtime protocol only needs to check for a non-zero value to
// know when there is a realtime command to execute.
struct ExecStateBits {
    uint8_t statusReport : 1;
    uint8_t cycleStart : 1;
    uint8_t cycleStop : 1;  // Unused, per cycle_stop variable
    uint8_t feedHold : 1;
    uint8_t reset : 1;
    uint8_t safetyDoor : 1;
    uint8_t motionCancel : 1;
    uint8_t sleep : 1;
};

union ExecState {
    uint8_t       value;
    ExecStateBits bit;
};

static_assert(sizeof(ExecStateBits) == sizeof(uint8_t), "ExecStateBits is not an uint8");

// sys_rt_exec_state 原子位操作的 mask，与 ExecStateBits 位序一一对应。
// 用于 system_rt_exec_set()/system_rt_exec_clear()。
constexpr uint8_t EXEC_STATUS_REPORT = 0x01;  // bit.statusReport
constexpr uint8_t EXEC_CYCLE_START   = 0x02;  // bit.cycleStart
constexpr uint8_t EXEC_CYCLE_STOP    = 0x04;  // bit.cycleStop (unused, per cycle_stop var)
constexpr uint8_t EXEC_FEED_HOLD     = 0x08;  // bit.feedHold
constexpr uint8_t EXEC_RESET         = 0x10;  // bit.reset
constexpr uint8_t EXEC_SAFETY_DOOR   = 0x20;  // bit.safetyDoor
constexpr uint8_t EXEC_MOTION_CANCEL = 0x40;  // bit.motionCancel
constexpr uint8_t EXEC_SLEEP         = 0x80;  // bit.sleep

// Override bit maps. Realtime bitflags to control feed, rapid, spindle, and coolant overrides.
// Spindle/coolant and feed/rapids are separated into two controlling flag variables.

struct ExecAccessoryBits {
    uint8_t spindleOvrStop : 1;
    uint8_t coolantFloodOvrToggle : 1;
    uint8_t coolantMistOvrToggle : 1;
};

union ExecAccessory {
    uint8_t           value;
    ExecAccessoryBits bit;
};

// Alarm codes.
enum class ExecAlarm : uint8_t {
    None               = 0,
    HardLimit          = 1,
    SoftLimit          = 2,
    AbortCycle         = 3,
    ProbeFailInitial   = 4,
    ProbeFailContact   = 5,
    HomingFailReset    = 6,
    HomingFailDoor     = 7,
    HomingFailPulloff  = 8,
    HomingFailApproach = 9,
    SpindleControl     = 10,
};

extern std::map<ExecAlarm, const char*> AlarmNames;
