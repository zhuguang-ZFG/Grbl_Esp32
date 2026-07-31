#pragma once

#include <cstdint>

enum class ProtocolDistanceMode : uint8_t {
    Absolute,
    Incremental,
};

enum class ProtocolUnitsMode : uint8_t {
    Mm,
    Inches,
};

enum class ProtocolFeedMode : uint8_t {
    UnitsPerMin,
    InverseTime,
};

struct ProtocolModalState {
    bool motion_active = false;
    ProtocolDistanceMode distance = ProtocolDistanceMode::Absolute;
    ProtocolUnitsMode units = ProtocolUnitsMode::Mm;
    ProtocolFeedMode feed = ProtocolFeedMode::UnitsPerMin;
};
class ProtocolDecisionCore {
public:
    static bool is_motion_gcode_g0_g3(const char* line) {
        if (line == nullptr) {
            return false;
        }
        bool in_parenthesis_comment = false;
        while (*line != '\0' && *line != '\r' && *line != '\n') {
            if (*line == ';' && !in_parenthesis_comment) {
                break;
            }
            if (*line == '(') {
                in_parenthesis_comment = true;
                ++line;
                continue;
            }
            if (*line == ')') {
                in_parenthesis_comment = false;
                ++line;
                continue;
            }
            if (!in_parenthesis_comment && (*line == 'G' || *line == 'g')) {
                const char* cursor = line + 1;
                while (*cursor == ' ' || *cursor == '\t') {
                    ++cursor;
                }
                if (*cursor >= '0' && *cursor <= '9') {
                    uint32_t code = 0;
                    do {
                        code = code * 10u + static_cast<uint32_t>(*cursor - '0');
                        ++cursor;
                    } while (*cursor >= '0' && *cursor <= '9');
                    if (code <= 3u && *cursor != '.') {
                        return true;
                    }
                }
            }
            ++line;
        }
        return false;
    }

    static bool has_axis_word(const char* line) {
        if (line == nullptr) {
            return false;
        }
        const char* first = line;
        while (*first == ' ' || *first == '\t') {
            ++first;
        }
        if (*first == '$' || *first == '[') {
            return false;
        }
        bool in_parenthesis_comment = false;
        while (*line != '\0' && *line != '\r' && *line != '\n') {
            if (*line == ';' && !in_parenthesis_comment) {
                break;
            }
            if (*line == '(') {
                in_parenthesis_comment = true;
            } else if (*line == ')') {
                in_parenthesis_comment = false;
            } else if (!in_parenthesis_comment) {
                char word = *line >= 'a' && *line <= 'z' ? static_cast<char>(*line - 'a' + 'A') : *line;
                if (word == 'X' || word == 'Y' || word == 'Z' || word == 'A' || word == 'B' || word == 'C') {
                    const char* value = line + 1;
                    while (*value == ' ' || *value == '\t') {
                        ++value;
                    }
                    if (*value == '+' || *value == '-') {
                        ++value;
                    }
                    bool has_digit = false;
                    while (*value >= '0' && *value <= '9') {
                        has_digit = true;
                        ++value;
                    }
                    if (*value == '.') {
                        ++value;
                        while (*value >= '0' && *value <= '9') {
                            has_digit = true;
                            ++value;
                        }
                    }
                    if (has_digit) {
                        return true;
                    }
                }
            }
            ++line;
        }
        return false;
    }
    static bool has_g_word(const char* line) {
        if (line == nullptr) {
            return false;
        }
        bool in_parenthesis_comment = false;
        while (*line != '\0' && *line != '\r' && *line != '\n') {
            if (*line == ';' && !in_parenthesis_comment) {
                break;
            }
            if (*line == '(') {
                in_parenthesis_comment = true;
            } else if (*line == ')') {
                in_parenthesis_comment = false;
            } else if (!in_parenthesis_comment && (*line == 'G' || *line == 'g')) {
                return true;
            }
            ++line;
        }
        return false;
    }

    static bool has_g_code(const char* line, uint32_t expected_code) {
        if (line == nullptr) {
            return false;
        }
        bool in_parenthesis_comment = false;
        while (*line != '\0' && *line != '\r' && *line != '\n') {
            if (*line == ';' && !in_parenthesis_comment) {
                break;
            }
            if (*line == '(') {
                in_parenthesis_comment = true;
                ++line;
                continue;
            }
            if (*line == ')') {
                in_parenthesis_comment = false;
                ++line;
                continue;
            }
            if (!in_parenthesis_comment && (*line == 'G' || *line == 'g')) {
                const char* cursor = line + 1;
                while (*cursor == ' ' || *cursor == '\t') {
                    ++cursor;
                }
                if (*cursor >= '0' && *cursor <= '9') {
                    uint32_t code = 0;
                    do {
                        code = code * 10u + static_cast<uint32_t>(*cursor - '0');
                        ++cursor;
                    } while (*cursor >= '0' && *cursor <= '9');
                    if (code == expected_code && *cursor != '.') {
                        return true;
                    }
                }
            }
            ++line;
        }
        return false;
    }

    static bool motion_modal_after(const char* line, bool modal_motion_active) {
        if (has_g_code(line, 80u)) {
            return false;
        }
        for (uint32_t code = 0; code <= 3u; ++code) {
            if (has_g_code(line, code)) {
                return true;
            }
        }
        return modal_motion_active;
    }
    static ProtocolModalState modal_after(const char* line, ProtocolModalState state) {
        state.motion_active = motion_modal_after(line, state.motion_active);
        if (has_g_code(line, 90u)) {
            state.distance = ProtocolDistanceMode::Absolute;
        } else if (has_g_code(line, 91u)) {
            state.distance = ProtocolDistanceMode::Incremental;
        }
        if (has_g_code(line, 20u)) {
            state.units = ProtocolUnitsMode::Inches;
        } else if (has_g_code(line, 21u)) {
            state.units = ProtocolUnitsMode::Mm;
        }
        if (has_g_code(line, 93u)) {
            state.feed = ProtocolFeedMode::InverseTime;
        } else if (has_g_code(line, 94u)) {
            state.feed = ProtocolFeedMode::UnitsPerMin;
        }
        return state;
    }
    static bool has_g38_code(const char* line) {
        if (line == nullptr) {
            return false;
        }
        bool in_parenthesis_comment = false;
        while (*line != '\0' && *line != '\r' && *line != '\n') {
            if (*line == ';' && !in_parenthesis_comment) {
                break;
            }
            if (*line == '(') {
                in_parenthesis_comment = true;
            } else if (*line == ')') {
                in_parenthesis_comment = false;
            } else if (!in_parenthesis_comment && (*line == 'G' || *line == 'g')) {
                const char* cursor = line + 1;
                while (*cursor == ' ' || *cursor == '\t') {
                    ++cursor;
                }
                if (*cursor == '3' && *(cursor + 1) == '8') {
                    const char boundary = *(cursor + 2);
                    if (boundary == '.' || boundary == ' ' || boundary == '\t' || boundary == '\0') {
                        return true;
                    }
                }
            }
            ++line;
        }
        return false;
    }
    static bool is_motion_line(const char* line, bool modal_motion_active = false) {
        if (is_motion_gcode_g0_g3(line)) {
            return true;
        }
        if (!modal_motion_active || !has_axis_word(line)) {
            return false;
        }
        // G90/G91, G20/G21, and G93/G94 may share a block with inherited motion.
        // Coordinate-setting and homing commands must remain non-motion here.
        return !has_g_code(line, 10u) && !has_g_code(line, 28u) && !has_g_code(line, 30u) && !has_g_code(line, 92u) && !has_g38_code(line);
    }

    // Homing / jog system commands that drive axes (not covered by is_motion_line).
    static bool is_homing_or_jog_system_line(const char* line) {
        if (line == nullptr || *line != '$') {
            return false;
        }
        const char* p = line + 1;
        while (*p == ' ' || *p == '\t') {
            ++p;
        }
        char c = *p;
        // $H (homing); $J=... (jogging). Fail-closed: also treat $H* as homing family.
        if (c == 'H' || c == 'h' || c == 'J' || c == 'j') {
            return true;
        }
        return false;
    }

    // During paper change: defer axis motion AND homing/probe/jog that can fight paper bit-bang.
    // Keep is_motion_line() semantics unchanged (license / modal policy still treat G28/G38 as non-motion).
    static bool should_defer_motion(const char* line, bool paper_change_running, bool modal_motion_active = false) {
        if (!paper_change_running || line == nullptr) {
            return false;
        }
        if (is_motion_line(line, modal_motion_active)) {
            return true;
        }
        // Explicit machine moves excluded from is_motion_line on purpose (G10/G28/G30/G38/G92 split).
        if (has_g_code(line, 28u) || has_g_code(line, 30u) || has_g38_code(line)) {
            return true;
        }
        if (is_homing_or_jog_system_line(line)) {
            return true;
        }
        return false;
    }

    // 仅恢复正常 Cycle 的段缓存断粮；所有有意停止意图都必须继续走原停止分支。
    static bool should_resume_segment_underflow(bool underflow, bool cycle_stopped, bool planner_has_block,
                                                bool was_cycle, bool end_motion, bool execute_hold,
                                                bool motion_cancel, bool soft_limit) {
        return underflow && cycle_stopped && planner_has_block && was_cycle && !end_motion && !execute_hold &&
               !motion_cancel && !soft_limit;
    }

    struct CycleStopInput {
        bool underflow             = false;
        bool cycle_stopped         = false;
        bool planner_has_block     = false;
        bool was_cycle            = false;
        bool end_motion           = false;
        bool execute_hold          = false;
        bool motion_cancel         = false;
        bool soft_limit            = false;
        bool hold_completion_state = false;
        bool jog_cancel            = false;
        bool safety_door_ajar      = false;
    };

    // 把分支优先级与 prep/wake/清理顺序固定在可由产品和 host 共用的执行器中。
    template <typename Ops>
    static bool apply_cycle_stop_transition(const CycleStopInput& input, Ops& ops) {
        if (!input.cycle_stopped) {
            return false;
        }

        bool resumed_segment_underflow = false;
        if (input.hold_completion_state && !input.soft_limit && !input.jog_cancel) {
            ops.reinitialize_cycle_plan();
            if (input.execute_hold) {
                ops.set_hold_complete();
            }
            ops.clear_execute_hold();
            ops.clear_execute_sys_motion();
        } else if (should_resume_segment_underflow(input.underflow, input.cycle_stopped,
                                                    input.planner_has_block, input.was_cycle,
                                                    input.end_motion, input.execute_hold,
                                                    input.motion_cancel, input.soft_limit)) {
            ops.clear_end_motion();
            ops.set_cycle_state();
            ops.prep_buffer();
            ops.wake_up();
            resumed_segment_underflow = true;
        } else {
            if (input.jog_cancel) {
                ops.clear_step_control();
                ops.reset_plan();
                ops.reset_stepper();
                ops.sync_gcode_position();
                ops.sync_plan_position();
            }
            if (input.safety_door_ajar) {
                ops.clear_jog_cancel();
                ops.set_hold_complete();
                ops.set_safety_door_state();
            } else {
                ops.clear_suspend();
                ops.set_idle_state();
            }
        }
        ops.clear_cycle_stop();
        return resumed_segment_underflow;
    }

    static bool defer_notice_due(uint32_t now_ms, uint32_t last_notice_ms, uint32_t interval_ms = 3000u) {
        return last_notice_ms == 0 || static_cast<uint32_t>(now_ms - last_notice_ms) >= interval_ms;
    }
};
