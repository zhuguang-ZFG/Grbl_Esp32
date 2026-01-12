/*
  SmartM0.h - Smart M0 detection and paper change control header
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#pragma once

#ifdef AUTO_PAPER_CHANGE_ENABLE

#include "Grbl.h"

// === Smart M0 Detection Functions ===

// Initialize smart M0 detection system
void smart_m0_init();

// Trigger M0 detection - called when M0 command is parsed
void smart_m0_trigger();

// Called when the next G-code command is about to be processed
void smart_m0_on_next_command();

// Update smart M0 state machine - called in main loop
void smart_m0_update();

// Called when paper change system completes its operation
void m0_paper_change_complete();

// Check if smart M0 system is currently active (paper change or waiting)
bool smart_m0_is_active();

// Emergency stop paper change operation
void smart_m0_emergency_stop();

// Reset smart M0 state to initial condition
void smart_m0_reset();

#endif // AUTO_PAPER_CHANGE_ENABLE