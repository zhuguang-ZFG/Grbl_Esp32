// paper_system.cpp
// Custom code for paper-handling system (paper sensor + paper-change motors)
// Enabled via CUSTOM_CODE_FILENAME in custom_3axis_hr4988.h
//
// 注意：本文件通过 CustomCode.cpp 间接包含，那里已包含 Grbl.h，
// 这里不需要再次 include。

// 简单的步进脉冲输出（低速点动用）
static void paper_step_pulses(uint8_t step_pin, uint16_t steps) {
    const uint16_t pulse_us = 500;  // 单个脉冲高/低各 500us，大约 1kHz
    for (uint16_t i = 0; i < steps; ++i) {
        digitalWrite(step_pin, HIGH);
        delayMicroseconds(pulse_us);
        digitalWrite(step_pin, LOW);
        delayMicroseconds(pulse_us);
    }
}

// Simple custom M-code handler.
// 当前支持：
//   M701  - 打印纸张传感器 + 换纸电机使能状态
//   M711  - 拾落(压纸抬落)电机点动若干步
//   M712  - 面板电机点动若干步
//   M713  - 进纸器电机点动若干步
Error user_m_code(uint16_t code) {
    switch (code) {
        case 701: {
            bool paper_present   = (digitalRead(PAPER_SENSOR_PIN) == 0);  // LOW = 有纸, HIGH = 无纸
            bool enable_active   = !digitalRead(PAPER_ENABLE_PIN);  // LOW (at driver) = 使能
            int  raw_value       = digitalRead(PAPER_SENSOR_PIN);
            const char* paperStr = paper_present ? "有纸" : "无纸";
            const char* enStr    = enable_active ? "已使能" : "未使能";

            grbl_msg_sendf(CLIENT_SERIAL,
                           MsgLevel::Info,
                           "M701: 纸张状态=%s (GPIO34=%d), 换纸电机=%s",
                           paperStr,
                           raw_value,
                           enStr);
            return Error::Ok;
        }
        case 711: {
            // 拾落（压纸抬落）电机点动
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M711: Starting clamp motor jog (200 steps)...");
            digitalWrite(PAPER_ENABLE_PIN, LOW);  // 统一使能三个换纸电机
            paper_step_pulses(CLAMP_MOTOR_STEP_PIN, 200);
            digitalWrite(PAPER_ENABLE_PIN, HIGH);  // 完成后关闭使能
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M711: Clamp motor jog completed");
            return Error::Ok;
        }
        case 712: {
            // 面板电机点动
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M712: Starting panel motor jog (200 steps)...");
            digitalWrite(PAPER_ENABLE_PIN, LOW);
            paper_step_pulses(PANEL_MOTOR_STEP_PIN, 200);
            digitalWrite(PAPER_ENABLE_PIN, HIGH);  // 完成后关闭使能
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M712: Panel motor jog completed");
            return Error::Ok;
        }
        case 713: {
            // 进纸器电机点动
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M713: Starting feeder motor jog (200 steps)...");
            digitalWrite(PAPER_ENABLE_PIN, LOW);
            paper_step_pulses(FEEDER_MOTOR_STEP_PIN, 200);
            digitalWrite(PAPER_ENABLE_PIN, HIGH);  // 完成后关闭使能
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M713: Feeder motor jog completed");
            return Error::Ok;
        }
        default:
            // 交给默认 GCode 处理：返回“不支持的 M 命令”
            return Error::GcodeUnsupportedCommand;
    }
}

// Macro 按钮回调（Macro0 映射纸张传感器，仅用于在 ? 状态里显示 Pn:0）
// 为了避免误操作，这里按下时只打印提示，不触发任何运动。
void user_defined_macro(uint8_t index) {
    if (index == 0) {
        bool paper_present = (digitalRead(PAPER_SENSOR_PIN) == 0);  // LOW = 有纸, HIGH = 无纸
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "纸张传感器(Macro0): %s",
                       paper_present ? "有纸" : "无纸");
    }
}

