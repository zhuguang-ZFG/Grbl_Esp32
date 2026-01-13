/*
  SmartM0.cpp - Smart M0 detection and paper change control
  Part of Grbl_ESP32

  Implements intelligent M0 detection:
  - Only triggers paper change if there is content after M0
  - Skips paper change if M0 is the last command
  - Automatically replies OK after paper change completion
  
  Copyright (c) 2024 Grbl_ESP32
*/

#include "Grbl.h"

#ifdef AUTO_PAPER_CHANGE_ENABLE
#include "SmartM0.h"

// === Smart M0 Detection State ===
typedef struct {
    bool m0_detected = false;              // M0 command detected
    bool m0_completed = false;            // M0 has been processed and OK sent
    bool pending_paper_change = false;     // Paper change is pending for next command
    bool content_after_m0 = false;         // Content found after M0
    uint32_t m0_timestamp = 0;            // When M0 was detected
    uint32_t content_check_timeout = 0;    // Timeout for content detection
    bool paper_change_active = false;      // Paper change currently in progress
    bool waiting_for_content = false;      // Waiting for content after M0
    uint8_t commands_in_queue = 0;        // Number of commands in queue
} smart_m0_state_t;

static smart_m0_state_t m0_state;

// === Function Prototypes ===
bool m0_has_content_in_queue();
void m0_start_paper_change();
void m0_skip_paper_change();
void m0_send_completion_ok();
void m0_complete();

// === Smart M0 Initialization ===
void smart_m0_init() {
    memset(&m0_state, 0, sizeof(smart_m0_state_t));
    m0_state.content_check_timeout = M0_CONTENT_CHECK_TIMEOUT_MS;
}

// === M0 Detection Trigger ===
void smart_m0_trigger() {
    if (!SMART_M0_DETECTION_ENABLE) {
        // Traditional M0 behavior - immediate paper change
        m0_start_paper_change();
        return;
    }

    m0_state.m0_detected = true;
    m0_state.m0_completed = false;
    m0_state.m0_timestamp = millis();
    m0_state.waiting_for_content = true;
    m0_state.content_after_m0 = false;
    m0_state.pending_paper_change = false;

    // Start content checking timer
    m0_state.content_check_timeout = millis() + M0_CONTENT_CHECK_TIMEOUT_MS;

    grbl_sendf(CLIENT_ALL, "[MSG: Smart M0 detected, checking for content...]\r\n");
}

// === M0 Complete Function ===
void m0_complete() {
    if (m0_state.m0_detected && !m0_state.m0_completed) {
        m0_state.m0_completed = true;
        grbl_sendf(CLIENT_ALL, "[MSG: M0 processing completed, OK sent]\r\n");
        
        // Send immediate OK for M0 completion
        grbl_sendf(CLIENT_ALL, "ok\r\n");
        
        if (!m0_state.content_after_m0) {
            // No content detected, reset M0 state completely
            m0_state.m0_detected = false;
            m0_state.waiting_for_content = false;
            m0_state.pending_paper_change = false;
            grbl_sendf(CLIENT_ALL, "[MSG: No paper change needed]\r\n");
        } else {
            // Content detected - set paper change pending for next command
            m0_state.pending_paper_change = true;
            grbl_sendf(CLIENT_ALL, "[MSG: Paper change pending for next command]\r\n");
        }
    }
}

// === Trigger Paper Change on Next Command ===
void smart_m0_on_next_command() {
    // This is called when a new G-code command is about to be processed
    if (m0_state.pending_paper_change && !m0_state.paper_change_active) {
        grbl_sendf(CLIENT_ALL, "[MSG: Next command detected, starting pending paper change]\r\n");
        m0_state.pending_paper_change = false;
        m0_state.m0_detected = false;
        m0_start_paper_change();
    }
}

// === Content Detection Update ===
void smart_m0_update() {
    if (!m0_state.waiting_for_content) {
        return;
    }

    // Check for content after M0
    m0_state.content_after_m0 = m0_has_content_in_queue();

    // Check if timeout reached or content detected
    uint32_t current_time = millis();
    if (m0_state.content_after_m0 || current_time > m0_state.content_check_timeout) {
        
        if (m0_state.content_after_m0) {
            grbl_sendf(CLIENT_ALL, "[MSG: Content detected after M0, paper change will trigger on next command]\r\n");
        } else {
            grbl_sendf(CLIENT_ALL, "[MSG: No content after M0, will skip paper change]\r\n");
        }

        // Complete M0 processing - this will send OK and set pending state
        m0_complete();
        m0_state.waiting_for_content = false;
    }
}

// === Check Queue for Content ===
bool m0_has_content_in_queue() {
    // Check if there are any pending commands in the planner buffer
    // This indicates there is content after M0 that needs to be executed
    
    // Use the planner buffer count to check for pending blocks
    uint8_t pending_blocks = plan_get_block_buffer_count();
    
    // If there are pending blocks (other than current), we assume there's content
    return (pending_blocks > 0);
}

// === Start Paper Change ===
void m0_start_paper_change() {
    m0_state.paper_change_active = true;
    
    // Trigger actual paper change system
    #ifdef AUTO_PAPER_CHANGE_ENABLE
    paper_change_start();
    #else
    // Simple delay simulation if paper change system not available
    delay(1000);
    m0_paper_change_complete();
    #endif
    
    grbl_sendf(CLIENT_ALL, "[MSG: Paper change initiated]\r\n");
}

// === Skip Paper Change ===
void m0_skip_paper_change() {
    m0_state.paper_change_active = false;
    m0_state.m0_detected = false;
    
    // Note: OK is already sent by m0_complete(), don't send duplicate
    grbl_sendf(CLIENT_ALL, "[MSG: M0 completed - no paper change required]\r\n");
}

// === Paper Change Completion Callback ===
void m0_paper_change_complete() {
    if (!m0_state.paper_change_active) {
        return;
    }

    m0_state.paper_change_active = false;
    m0_state.m0_detected = false;

    grbl_sendf(CLIENT_ALL, "[MSG: Paper change completed]\r\n");

    // Automatically send OK reply if enabled
    if (PAPER_CHANGE_AUTO_OK_REPLY) {
        m0_send_completion_ok();
    }
}

// === Send Completion OK ===
void m0_send_completion_ok() {
    grbl_sendf(CLIENT_ALL, "ok\r\n");
    grbl_sendf(CLIENT_ALL, "[MSG: Smart M0 paper change complete, resuming operation]\r\n");
}

// === Get Smart M0 Status ===
bool smart_m0_is_active() {
    return m0_state.paper_change_active || m0_state.waiting_for_content;
}

// === Emergency Stop Paper Change ===
void smart_m0_emergency_stop() {
    if (m0_state.paper_change_active) {
        grbl_sendf(CLIENT_ALL, "[MSG: Emergency stop triggered during paper change]\r\n");
        
        #ifdef AUTO_PAPER_CHANGE_ENABLE
        paper_change_stop();
        #endif
        
        m0_state.paper_change_active = false;
        m0_state.m0_detected = false;
        m0_state.waiting_for_content = false;
        
        // Send error status instead of OK
        grbl_sendf(CLIENT_ALL, "error: Paper change emergency stopped\r\n");
    }
}

// === Reset Smart M0 State ===
void smart_m0_reset() {
    memset(&m0_state, 0, sizeof(smart_m0_state_t));
    grbl_sendf(CLIENT_ALL, "[MSG: Smart M0 state reset]\r\n");
}

#endif // AUTO_PAPER_CHANGE_ENABLE