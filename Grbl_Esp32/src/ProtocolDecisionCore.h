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

    static bool should_defer_motion(const char* line, bool paper_change_running, bool modal_motion_active = false) {
        return paper_change_running && is_motion_line(line, modal_motion_active);
    }

    static bool defer_notice_due(uint32_t now_ms, uint32_t last_notice_ms, uint32_t interval_ms = 3000u) {
        return last_notice_ms == 0 || static_cast<uint32_t>(now_ms - last_notice_ms) >= interval_ms;
    }
};
