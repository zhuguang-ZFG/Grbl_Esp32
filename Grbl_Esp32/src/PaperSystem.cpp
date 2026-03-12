/*
 * PaperSystem.cpp - 换纸系统 M701/M711/M712/M713，单一实现避免链接到错误 stub
 */
#include "Grbl.h"

// 未启用换纸的机器在 Config.h 中把引脚设为 255，此处用运行时判断
#define PAPER_DISABLED 255

static void paper_step_pulses(uint8_t step_pin, uint16_t steps) {
    for (uint16_t i = 0; i < steps; i++) {
        digitalWrite(step_pin, HIGH);
        delayMicroseconds(500);
        digitalWrite(step_pin, LOW);
        delayMicroseconds(500);
    }
}

void paper_system_init(void) {
    if (PAPER_SENSOR_PIN != PAPER_DISABLED) {
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "Paper M701/M711/M712/M713 ready");
    }
}

Error paper_system_mcode(uint16_t code) {
    if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
        return Error::GcodeUnsupportedCommand;
    }
    switch (code) {
        case 701: {
            bool paper_ok = digitalRead(PAPER_SENSOR_PIN);
            bool en_ok    = !digitalRead(PAPER_ENABLE_PIN);
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info,
                           "M701: Paper=%s MotorEn=%s",
                           paper_ok ? "OK" : "No", en_ok ? "On" : "Off");
            return Error::Ok;
        }
        case 711:
            digitalWrite(PAPER_ENABLE_PIN, LOW);
            paper_step_pulses(CLAMP_MOTOR_STEP_PIN, 200);
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M711: done");
            return Error::Ok;
        case 712:
            digitalWrite(PAPER_ENABLE_PIN, LOW);
            paper_step_pulses(PANEL_MOTOR_STEP_PIN, 200);
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M712: done");
            return Error::Ok;
        case 713:
            digitalWrite(PAPER_ENABLE_PIN, LOW);
            paper_step_pulses(FEEDER_MOTOR_STEP_PIN, 200);
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M713: done");
            return Error::Ok;
        default:
            return Error::GcodeUnsupportedCommand;
    }
}
