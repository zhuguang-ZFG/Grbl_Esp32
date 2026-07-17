#pragma once

#include <cstdint>

class LicenseCore {
public:
    static uint32_t code_for_chip(uint32_t chip_high, uint32_t chip_low, uint32_t key_high, uint32_t key_low) {
        uint32_t mix = chip_high ^ chip_low ^ key_high ^ key_low;
        mix = (mix << 7u) | (mix >> 25u);
        mix ^= key_high;
        mix ^= (key_low << 13u) | (key_low >> 19u);
        return mix;
    }

    static bool code_matches(uint32_t expected, uint32_t supplied) {
        return expected != 0u && expected == supplied;
    }
};
