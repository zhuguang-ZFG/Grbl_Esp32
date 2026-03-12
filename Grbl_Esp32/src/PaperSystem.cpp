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
    // 面板电机用微秒延时，步频高、更顺滑少卡顿；拾落/进纸器保持 2ms 稳妥
    const uint32_t hi_us = (step_pin == PANEL_MOTOR_STEP_PIN) ? 150u : 2000u;
    const uint32_t lo_us = (step_pin == PANEL_MOTOR_STEP_PIN) ? 150u : 2000u;
    for (uint16_t i = 0; i < steps; i++) {
        digitalWrite(step_pin, HIGH);
        i2s_out_delay();
        delayMicroseconds(hi_us);
        digitalWrite(step_pin, LOW);
        i2s_out_delay();
        delayMicroseconds(lo_us);
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
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "Paper: [ESP901] [ESP911] [ESP912] [ESP913] [ESP910]");
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

// 简单的纸张传感器读取（true=感应到纸，false=未感应到）
static inline bool paper_sensor_active() {
    return digitalRead(PAPER_SENSOR_PIN) != 0;
}

// 设定某个 DIR/STEP 电机以给定方向运动 N 步
static void paper_dir_steps(uint8_t dir_pin, bool dir_level, uint8_t step_pin, uint32_t steps) {
    digitalWrite(dir_pin, dir_level);
#ifdef USE_I2S_OUT
    i2s_out_delay();
    delay(2);
#endif
    paper_step_pulses(step_pin, (uint16_t)steps);
}

#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM

// 一键自动换纸流程
Error paper_auto_change(void) {
    if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
        return Error::GcodeUnsupportedCommand;
    }

    // 统一切到 I2S 静态模式并打开换纸相关驱动
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

    // 1. 面板电机先运动，弹出旧纸（A4 长度 + 余量）
    paper_dir_steps(PANEL_MOTOR_DIR_PIN, PANEL_DIR_EJECT, PANEL_MOTOR_STEP_PIN, PANEL_EJECT_STEPS);

    // 2. 进纸器开始运动，直到纸张传感器“感应到纸”或达到上限
    {
        uint32_t steps = 0;
        digitalWrite(FEEDER_MOTOR_DIR_PIN, FEEDER_DIR_FORWARD);
#ifdef USE_I2S_OUT
        i2s_out_delay();
        delay(2);
#endif
        while (!paper_sensor_active() && steps < FEEDER_FIND_STEPS_MAX) {
            paper_step_pulses(FEEDER_MOTOR_STEP_PIN, 1);
            steps++;
        }
        if (!paper_sensor_active()) {
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning, "Paper auto: feeder find timeout");
            return Error::Ok;  // 不报错，只提示
        }
    }

    // 3. 传感器感应到纸后，松开拾落电机
    paper_dir_steps(CLAMP_MOTOR_DIR_PIN, CLAMP_DIR_RELEASE, CLAMP_MOTOR_STEP_PIN, CLAMP_TOGGLE_STEPS);

    // 4. 送纸器继续送入一段（约 5cm）
    paper_dir_steps(FEEDER_MOTOR_DIR_PIN, FEEDER_DIR_FORWARD, FEEDER_MOTOR_STEP_PIN, FEEDER_EXTRA_STEPS);

    // 5. 再次压紧拾落电机
    paper_dir_steps(CLAMP_MOTOR_DIR_PIN, CLAMP_DIR_CLAMP, CLAMP_MOTOR_STEP_PIN, CLAMP_TOGGLE_STEPS);

    // 6. 面板电机 + 送纸器快速送纸，直到传感器“看不到纸”为止或达到上限
    {
        uint32_t steps = 0;
        digitalWrite(PANEL_MOTOR_DIR_PIN, PANEL_DIR_FEED);
        digitalWrite(FEEDER_MOTOR_DIR_PIN, FEEDER_DIR_FORWARD);
#ifdef USE_I2S_OUT
        i2s_out_delay();
        delay(2);
#endif
        while (paper_sensor_active() && steps < PANEL_FAST_STEPS_MAX) {
            // 这里简单交织两电机的 STEP（1:1）
            paper_step_pulses(PANEL_MOTOR_STEP_PIN, 1);
            paper_step_pulses(FEEDER_MOTOR_STEP_PIN, 1);
            steps++;
        }
        // 送纸器停下
    }

    // 7. 面板电机反向，直到再次“感应到纸”或达到上限（回找定位点）
    {
        uint32_t steps = 0;
        digitalWrite(PANEL_MOTOR_DIR_PIN, PANEL_DIR_EJECT /* 反向：与 FEED 相反，若极性不对可调整配置宏 */);
#ifdef USE_I2S_OUT
        i2s_out_delay();
        delay(2);
#endif
        while (!paper_sensor_active() && steps < PANEL_BACK_STEPS_MAX) {
            paper_step_pulses(PANEL_MOTOR_STEP_PIN, 1);
            steps++;
        }
    }

    // 8. 面板电机再向送纸方向走固定步数，作为最终对位
    paper_dir_steps(PANEL_MOTOR_DIR_PIN, PANEL_DIR_FEED, PANEL_MOTOR_STEP_PIN, PANEL_FINAL_STEPS);

    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "Paper auto: done");
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
