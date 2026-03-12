/*
 * PaperSystem.cpp - 换纸系统: M701/M711/M712/M713 �?[ESP901/911/912/913]
 * 74HC595 通过 I2S 扩展，需先切�?passthrough 并留足延时，电平才会真正输出�?
 */
#include "Grbl.h"
#ifdef USE_I2S_OUT
#    include "I2SOut.h"
#endif

#define PAPER_DISABLED 255

static void paper_step_pulses(uint8_t step_pin, uint16_t steps) {
#ifdef USE_I2S_OUT
    for (uint16_t i = 0; i < steps; i++) {
        digitalWrite(step_pin, HIGH);
        i2s_out_delay();
        delay(2);
        digitalWrite(step_pin, LOW);
        i2s_out_delay();
        delay(2);
    }
#else
    for (uint16_t i = 0; i < steps; i++) {
        digitalWrite(step_pin, HIGH);
        delayMicroseconds(500);
        digitalWrite(step_pin, LOW);
        delayMicroseconds(500);
    }
#endif
}

void paper_system_init(void) {
    if (PAPER_SENSOR_PIN != PAPER_DISABLED) {
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "Paper: [ESP901] [ESP911] [ESP912] [ESP913]");
    }
}

#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
void paper_get_status_str(char* buf, size_t len) {
    if (PAPER_SENSOR_PIN == PAPER_DISABLED || len < 32) {
        snprintf(buf, len, "paper system not configured");
        return;
    }
    bool paper_ok = digitalRead(PAPER_SENSOR_PIN);
    bool en_ok    = !digitalRead(PAPER_ENABLE_PIN);
#ifdef PAPER_DRIVER_ENABLE_PIN
    en_ok = en_ok || !digitalRead(PAPER_DRIVER_ENABLE_PIN);
#endif
    snprintf(buf, len, "Paper=%s MotorEn=%s", paper_ok ? "OK" : "No", en_ok ? "On" : "Off");
}

Error paper_run_motor(uint8_t motor_ix) {
    if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
        return Error::GcodeUnsupportedCommand;
    }
    uint8_t step_pin;
    if (motor_ix == 0) {
        step_pin = CLAMP_MOTOR_STEP_PIN;
    } else if (motor_ix == 1) {
        step_pin = PANEL_MOTOR_STEP_PIN;
    } else if (motor_ix == 2) {
        step_pin = FEEDER_MOTOR_STEP_PIN;
    } else {
        return Error::GcodeUnsupportedCommand;
    }
#ifdef USE_I2S_OUT
    i2s_out_set_passthrough();
    delay(I2S_OUT_DELAY_MS * 2);
    i2s_out_delay();
#endif
    digitalWrite(PAPER_ENABLE_PIN, LOW);
#ifdef PAPER_DRIVER_ENABLE_PIN
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, LOW);
#endif
#ifdef USE_I2S_OUT
    i2s_out_delay();
    delay(5);
#endif
    paper_step_pulses(step_pin, 200);
    return Error::Ok;
}
#endif

Error paper_system_mcode(uint16_t code) {
    if (code == 189) {
        code = 701;
    } else if (code == 199) {
        code = 711;
    } else if (code == 209) {
        code = 712;
    } else if (code == 219) {
        code = 713;
    }
    switch (code) {
        case 701: {
            if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M701: paper system not configured");
                return Error::Ok;
            }
            bool paper_ok = digitalRead(PAPER_SENSOR_PIN);
            bool en_ok    = !digitalRead(PAPER_ENABLE_PIN);
#ifdef PAPER_DRIVER_ENABLE_PIN
    en_ok = en_ok || !digitalRead(PAPER_DRIVER_ENABLE_PIN);
#endif
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info,
                           "M701: Paper=%s MotorEn=%s",
                           paper_ok ? "OK" : "No", en_ok ? "On" : "Off");
            return Error::Ok;
        }
        case 711:
        case 712:
        case 713: {
            if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M%u: paper system not configured", (unsigned)code);
                return Error::Ok;
            }
#ifdef USE_I2S_OUT
            i2s_out_set_passthrough();
            delay(I2S_OUT_DELAY_MS * 2);
            i2s_out_delay();
#endif
            digitalWrite(PAPER_ENABLE_PIN, LOW);
#ifdef PAPER_DRIVER_ENABLE_PIN
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, LOW);
#endif
#ifdef USE_I2S_OUT
            i2s_out_delay();
            delay(5);
#endif
            uint8_t step_pin = (code == 711) ? CLAMP_MOTOR_STEP_PIN : (code == 712) ? PANEL_MOTOR_STEP_PIN : FEEDER_MOTOR_STEP_PIN;
            paper_step_pulses(step_pin, 200);
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M%u: done", (unsigned)code);
            return Error::Ok;
        }
        default:
            if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
                return Error::GcodeUnsupportedCommand;
            }
            return Error::GcodeUnsupportedCommand;
    }
}
