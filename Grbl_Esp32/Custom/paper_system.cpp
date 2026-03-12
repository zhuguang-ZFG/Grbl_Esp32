// paper_system.cpp
// Custom code for paper-handling system (paper sensor + paper-change motors)
// Enabled via CUSTOM_CODE_FILENAME in custom_3axis_hr4988.h
//
// 注意：本文件通过 CustomCode.cpp 间接包含，那里已包含 Grbl.h，
// 这里不需要再次 include。

// Custom M-code handler
// M701/M711/M712/M713/M716 等纸张系统命令由 PaperSystem.cpp 的 paper_system_mcode() 统一处理
Error user_m_code(uint16_t code) {
    // 所有纸张相关命令都由 paper_system_mcode() 处理，此函数返回不支持
    return Error::GcodeUnsupportedCommand;
}

// Macro 按钮回调（Macro0 映射纸张传感器）
void user_defined_macro(uint8_t index) {
    if (index == 0) {
        bool paper_present = (digitalRead(PAPER_SENSOR_PIN) == 0);  // LOW = 有纸, HIGH = 无纸
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "纸张传感器(Macro0): %s",
                       paper_present ? "有纸" : "无纸");
    }
}
