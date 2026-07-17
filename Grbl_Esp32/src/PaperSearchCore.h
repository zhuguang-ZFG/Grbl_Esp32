#pragma once

#include <cstdint>

enum class PaperSearchDecision : uint8_t {
    Continue,
    Found,
    TimedOut,
    StepLimit,
};

inline PaperSearchDecision paper_sensor_edge_decide(
    bool sensor_active, bool expected_active, uint32_t elapsed_ms, uint32_t steps_taken, uint32_t max_steps, uint32_t timeout_ms) {
    if (sensor_active == expected_active) {
        return PaperSearchDecision::Found;
    }
    if (steps_taken >= max_steps) {
        return PaperSearchDecision::StepLimit;
    }
    if (elapsed_ms >= timeout_ms) {
        return PaperSearchDecision::TimedOut;
    }
    return PaperSearchDecision::Continue;
}
