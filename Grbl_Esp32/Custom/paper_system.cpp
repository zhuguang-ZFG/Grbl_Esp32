// paper_system.cpp
// Custom code for paper-handling system (paper sensor + paper-change motors)
// Enabled via CUSTOM_CODE_FILENAME in custom_3axis_hr4988.h
//
// 注意：本文件通过 CustomCode.cpp 间接包含，那里已包含 Grbl.h，
// 这里不需要再次 include。

// 机器初始化钩子：在 Grbl 启动时调用（弱符号在 Grbl.cpp 中，这里覆盖）
// 这里打印一次 ESP32 芯片 ID，后续可用于程序加密绑定。
void machine_init() {
    uint64_t chipid = ESP.getEfuseMac();  // Arduino-ESP32 提供的芯片唯一 ID（基于 MAC）
    uint32_t id_high = (uint32_t)(chipid >> 32);
    uint32_t id_low  = (uint32_t)(chipid & 0xFFFFFFFFULL);

    grbl_msg_sendf(CLIENT_SERIAL,
                   MsgLevel::Info,
                   "[ESP32ID] ChipID (efuse MAC) = %08X%08X",
                   (unsigned)id_high,
                   (unsigned)id_low);
}

// Custom M-code handler
// M701/M711/M712/M713/M716 等纸张系统命令由 PaperSystem.cpp 的 paper_system_mcode() 统一处理
Error user_m_code(uint16_t code) {
    // 所有纸张相关命令都由 paper_system_mcode() 处理，此函数返回不支持
    return Error::GcodeUnsupportedCommand;
}

// 当 GCode 中遇到 M30（当前页 G 代码文件结束）时，GCode.cpp 会在
// program_flow 处理完毕、缓冲区同步后调用 user_m30()。
// 这里把“换纸 = 换页”接进来：每次 M30 结束自动执行一套换纸流程。
void user_m30() {
    // 必须在 Idle 状态、且纸张系统已配置时才自动换纸，避免在报警/检查模式下误动作。
    if (sys.state != State::Idle) {
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "[PaperM30] Skipped auto-change: system not idle (state=%d)",
                       (int)sys.state);
        return;
    }
    if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "[PaperM30] Skipped auto-change: paper system not configured");
        return;
    }

    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperM30] End of page (M30) detected, starting auto paper change...");
    Error e = paper_auto_change();
    if (e == Error::Ok) {
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperM30] Auto paper change completed.");
    } else {
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Warning,
                       "[PaperM30] Auto paper change returned error=%d",
                       (int)e);
    }
}

// 一键换纸物理按键（接在 GPIO35，对应 Macro0）回调。
// 要求：“按键按下松开后去抖，加入到一键换纸流程中”
//
// 设计说明：
// - 硬件：PAPER_CHANGE_BTN_PIN = GPIO35，实测接法为 LOW=按下，HIGH=松开（外部下拉）。
// - 上面在 custom_3axis_hr4988.h 中把 MACRO_BUTTON_0_PIN 映射为 PAPER_CHANGE_BTN_PIN，
//   并通过 INVERT_CONTROL_PIN_MASK 让 Macro0 变为“低电平有效”，仅在按下时产生事件。
// - 这里再做一次软件去抖：两次有效触发之间至少间隔 500ms，防止长时间抖动或误连按。
// - 为了复用现有 [ESP910] 逻辑，这里不直接调用 paper_auto_change()，
//   而是向 WebUI::inputBuffer 注入一行 “[ESP910]”，由原有处理流程执行一键换纸。
void user_defined_macro(uint8_t index) {
    if (index != 0) {
        return;
    }

    // 必须在 Idle 状态才能开始一键换纸
    if (sys.state != State::Idle) {
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "[PaperBtn] Ignored: system not idle (state=%d)",
                       (int)sys.state);
        return;
    }

    // 简单的软件去抖：500ms 内只响应一次
    static uint32_t last_trigger_ms = 0;
    uint32_t        now_ms          = millis();
    if (now_ms - last_trigger_ms < 500u) {
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "[PaperBtn] Ignored: debounce (%lu ms since last)",
                       (unsigned long)(now_ms - last_trigger_ms));
        return;
    }
    last_trigger_ms = now_ms;

    // 记录当前按键电平，便于调试（LOW=按下，HIGH=松开）
    int raw_level = digitalRead(PAPER_CHANGE_BTN_PIN);
    grbl_msg_sendf(CLIENT_SERIAL,
                   MsgLevel::Info,
                   "[PaperBtn] Triggered (GPIO35=%d), queuing [ESP910] auto-change...",
                   raw_level);

    // 向 WebUI 输入缓冲区注入一条 [ESP910] 命令，由已有 paperAutoHandler -> paper_auto_change() 执行换纸流程
    char line[16];
    strcpy(line, "[ESP910]\r");
    WebUI::inputBuffer.push(line);
}
