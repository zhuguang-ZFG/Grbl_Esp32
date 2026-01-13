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

#    include "PaperChange.h"

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
// Smart M0系统初始化函数 - 在GCode解析器初始化时调用
void smart_m0_init() {
    memset(&m0_state, 0, sizeof(smart_m0_state_t));  // 将M0状态结构体全部清零
    m0_state.content_check_timeout = M0_CONTENT_CHECK_TIMEOUT_MS; // 设置默认内容检测超时时间(2秒)
}

// === M0 Detection Trigger ===
// M0检测触发函数 - 当G-code解析器遇到M0指令时调用
void smart_m0_trigger() {
    if (!SMART_M0_DETECTION_ENABLE) {
        // Traditional M0 behavior - immediate paper change - 传统模式：立即触发换纸
        m0_start_paper_change();  // 直接启动物理换纸
        return;
    }

    // Smart mode setup - 智能模式：设置检测状态
    m0_state.m0_detected = true;                    // 标记M0已检测
    m0_state.m0_completed = false;                  // 标记M0尚未完成
    m0_state.m0_timestamp = millis();               // 记录M0检测时间戳
    m0_state.waiting_for_content = true;           // 开始等待内容检测
    m0_state.content_after_m0 = false;             // 重置内容检测标志
    m0_state.pending_paper_change = false;          // 重置待换纸标志

    // Start content checking timer - 启动内容检测计时器
    m0_state.content_check_timeout = millis() + M0_CONTENT_CHECK_TIMEOUT_MS; // 设置2秒超时

    grbl_sendf(CLIENT_ALL, "[MSG: Smart M0 detected, checking for content...]\r\n"); // 发送检测开始消息
}

// === M0 Complete Function ===
// M0完成函数 - 完成M0处理并发送ok响应
void m0_complete() {
    if (m0_state.m0_detected && !m0_state.m0_completed) {  // 确保M0已检测且未完成
        m0_state.m0_completed = true;  // 标记M0处理完成
        grbl_sendf(CLIENT_ALL, "[MSG: M0 processing completed, OK sent]\r\n"); // 发送完成消息
        
        // Send immediate OK for M0 completion - 立即发送ok响应给上位机
        grbl_sendf(CLIENT_ALL, "ok\r\n");  // 这是关键：M0立即返回ok，不阻塞上位机
        
        if (!m0_state.content_after_m0) {
            // No content detected, reset M0 state completely - 无内容检测到，完全重置M0状态
            m0_state.m0_detected = false;       // 清除M0检测标志
            m0_state.waiting_for_content = false; // 清除等待内容标志
            m0_state.pending_paper_change = false;// 清除待换纸标志
            grbl_sendf(CLIENT_ALL, "[MSG: No paper change needed]\r\n"); // 发送跳过换纸消息
        } else {
            // Content detected - set paper change pending for next command - 有内容检测到，设置待换纸状态
            m0_state.pending_paper_change = true;  // 标记有待处理的换纸操作
            grbl_sendf(CLIENT_ALL, "[MSG: Paper change pending for next command]\r\n"); // 发送待换纸消息
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
// 内容检测更新函数 - 在Protocol主循环中被调用，持续检测M0后的内容
void smart_m0_update() {
    if (!m0_state.waiting_for_content) {  // 检查是否正在等待内容检测
        return;  // 不在等待状态，直接返回
    }

    // Check for content after M0 - 检查M0后的内容
    m0_state.content_after_m0 = m0_has_content_in_queue();  // 检查规划器缓冲区是否有待处理内容

    // Check if timeout reached or content detected - 检查是否超时或检测到内容
    uint32_t current_time = millis();  // 获取当前系统时间
    if (m0_state.content_after_m0 || current_time > m0_state.content_check_timeout) {  // 超时或检测到内容
        
        if (m0_state.content_after_m0) {
            grbl_sendf(CLIENT_ALL, "[MSG: Content detected after M0, paper change will trigger on next command]\r\n"); // 有内容消息
        } else {
            grbl_sendf(CLIENT_ALL, "[MSG: No content after M0, will skip paper change]\r\n"); // 无内容消息
        }

        // Complete M0 processing - this will send OK and set pending state - 完成M0处理
        m0_complete();  // 调用M0完成函数，发送ok并设置相应状态
        m0_state.waiting_for_content = false;  // 清除等待内容标志
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

/**
 * @brief 检查SmartM0是否处于活动状态。
 *
 * 该函数用于判断当前是否有纸张更换操作正在进行，或者系统是否正在等待内容输入。如果任一条件满足，则认为SmartM0是活跃的。
 *
 * @return 如果有纸张更换操作正在进行或系统正在等待内容输入，则返回true；否则返回false。
 */
bool smart_m0_is_active() {
    return m0_state.paper_change_active || m0_state.waiting_for_content;
}

/**
 * @brief 在纸张更换操作期间启动紧急停止。
 *
 * 此函数旨在安全地停止正在进行的纸张更换过程，并重置相关状态变量。它会向所有客户端发送消息，表明已触发紧急停止。如果启用了自动纸张更换功能，它还会调用 `paper_change_stop()` 以确保硬件处于安全状态。最后，它会更新状态以反映纸张更换不再处于活动状态，并向所有客户端发送错误消息。
 *
 * @note 此函数应在纸张更换期间检测到紧急情况时调用，例如长时间按下纸张更换按钮。
 */
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

/**
 * @brief 重置智能M0状态。
 *
 * 此函数将智能M0状态结构体的所有字段重置为初始值（通常是0或false），并发送一条消息通知所有客户端状态已重置。这在需要从头开始处理M0命令或清理状态时非常有用。
 */
void smart_m0_reset() {
    memset(&m0_state, 0, sizeof(smart_m0_state_t));
    grbl_sendf(CLIENT_ALL, "[MSG: Smart M0 state reset]\r\n");
}

#endif // AUTO_PAPER_CHANGE_ENABLE