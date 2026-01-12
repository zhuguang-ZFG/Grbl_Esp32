/*
  PaperChange.h - Complete paper change system header
  Part of Grbl_ESP32

  Copyright (c) 2024 Grbl_ESP32
*/

#pragma once

#ifdef AUTO_PAPER_CHANGE_ENABLE

#include "Grbl.h"

// === Paper Change Functions ===

// Initialize paper change system
void paper_change_init();

// Start automatic paper change sequence
void paper_change_start();

// Manual one-click paper change
void paper_change_one_click();

// Emergency stop paper change operation
void paper_change_stop();

// Update paper change state machine - called in main loop
void paper_change_update();

// Check if paper change system is currently active
bool paper_change_is_active();

// Get current paper change state string
const char* paper_change_get_state();

// Reset paper change system to initial state
void paper_change_reset();

#endif // AUTO_PAPER_CHANGE_ENABLE