#pragma once

#include <cstdint>

enum PaperPulseProfile : uint8_t {
    PaperPulsePanel,
    PaperPulseFeederFeed,
    PaperPulseFeederFind,
    PaperPulseClamp,
    PaperPulsePanelFast,
    PaperPulsePanelEject,
};

struct PaperPulseTiming {
    uint32_t high_us;
    uint32_t low_us;
};

struct PaperTimingConfig {
    bool             use_i2s;
    bool             eject_profile_enabled;
    uint32_t         ramp_steps;
    uint32_t         panel_fast_ramp_steps;
    PaperPulseTiming ramp;
    PaperPulseTiming normal;
    PaperPulseTiming clamp;
    PaperPulseTiming feeder_find_ramp;
    PaperPulseTiming feeder_find_normal;
    PaperPulseTiming feeder_feed_ramp;
    PaperPulseTiming feeder_feed_normal;
    PaperPulseTiming panel_fast;
    PaperPulseTiming eject_ramp;
    PaperPulseTiming eject_normal;
};

inline PaperPulseTiming paper_profile_timing_core(PaperPulseProfile profile, uint32_t step_index, const PaperTimingConfig& config) {
    if (!config.use_i2s) {
        if (profile == PaperPulseFeederFind) {
            return step_index < config.ramp_steps ? config.feeder_find_ramp : config.feeder_find_normal;
        }
        if (profile == PaperPulsePanelEject && config.eject_profile_enabled) {
            return step_index < config.ramp_steps ? config.eject_ramp : config.eject_normal;
        }
        if (profile == PaperPulsePanelFast) {
            return step_index < config.panel_fast_ramp_steps ? PaperPulseTiming { 400u, 400u } : config.panel_fast;
        }
        return PaperPulseTiming { 500u, 500u };
    }

    switch (profile) {
        case PaperPulseClamp:
            return config.clamp;
        case PaperPulseFeederFind:
            return step_index < config.ramp_steps ? config.feeder_find_ramp : config.feeder_find_normal;
        case PaperPulseFeederFeed:
            return step_index < config.ramp_steps ? config.feeder_feed_ramp : config.feeder_feed_normal;
        case PaperPulsePanelFast:
            return step_index < config.panel_fast_ramp_steps ? config.ramp : config.panel_fast;
        case PaperPulsePanelEject:
            if (config.eject_profile_enabled) {
                return step_index < config.ramp_steps ? config.eject_ramp : config.eject_normal;
            }
            return step_index < config.ramp_steps ? config.ramp : config.normal;
        case PaperPulsePanel:
        default:
            return step_index < config.ramp_steps ? config.ramp : config.normal;
    }
}

inline bool paper_sensor_stable_core(uint32_t low_samples, uint32_t sample_count, uint32_t threshold) {
    return sample_count > 0 && threshold > 0 && threshold <= sample_count && low_samples >= threshold;
}

inline bool paper_deadline_active(uint32_t now_ms, uint32_t deadline_ms) {
    return deadline_ms != 0 && static_cast<int32_t>(deadline_ms - now_ms) > 0;
}
