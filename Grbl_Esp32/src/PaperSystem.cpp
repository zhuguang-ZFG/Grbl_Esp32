/*
 * PaperSystem.cpp - 换纸系统: M701/M711/M712/M713 �?[ESP901/911/912/913]
 * 74HC595 通过 I2S 扩展，需先切�?passthrough 并留足延时，电平才会真正输出�?
 */
#include "Grbl.h"
#ifdef USE_I2S_OUT
#    include "I2SOut.h"
#endif

#define PAPER_DISABLED 255

// 换纸流程结束状态码（上位机可解析 [PaperStatus] N 做分支）
#define PAPER_STATUS_OK                0
#define PAPER_STATUS_PAPER_PRESENT     1  // 开始时传感器仍有纸，无法弹旧纸
#define PAPER_STATUS_FEEDER_TIMEOUT   2  // 进纸阶段超时未触发传感器
#define PAPER_STATUS_SENSOR_NOT_FOUND 3  // 第7步回找后传感器未稳定（纸可能未到位）

// 步进脉宽常量：若机器头文件未定义，则使用下列默认值（见 custom_3axis_hr4988.h）
#ifndef PAPER_RAMP_STEPS
#define PAPER_RAMP_STEPS    40u
#define PAPER_RAMP_HI_US    400u
#define PAPER_RAMP_LO_US    400u
#define PAPER_NORMAL_HI_US  150u
#define PAPER_NORMAL_LO_US  150u
#define PAPER_CLAMP_HI_US   2000u
#define PAPER_CLAMP_LO_US   2000u
#endif
#ifndef PAPER_PANEL_FAST_RAMP_STEPS
#define PAPER_PANEL_FAST_RAMP_STEPS  PAPER_RAMP_STEPS
#endif

// 一键换纸流程是否正在执行（用于彩灯“快闪”与互斥）
static volatile bool paper_auto_change_running = false;

#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
bool paper_auto_change_is_running(void) {
    return paper_auto_change_running;
}
#endif

#ifdef PAPER_LED_PIN
#ifdef USE_I2S_OUT
// 前置声明：供 paper_led_set() 调用（定义在后文）
static void paper_ensure_i2s_passthrough(void);
#endif

// 按键彩灯：Q0/QA，HIGH=灭，LOW=亮
static void paper_led_set(bool on) {
#ifdef USE_I2S_OUT
    paper_ensure_i2s_passthrough();
#endif
    digitalWrite(PAPER_LED_PIN, on ? LOW : HIGH);
}

// 彩灯状态刷新（在主循环中周期调用，内部节流约 80ms）
// 状态：换纸中→快闪；空闲有纸→常亮；空闲无纸→慢闪；运行中有纸→亮，无纸→灭
void paper_led_update(void) {
    static uint32_t last_ms   = 0;
    static bool     led_on    = false;
    uint32_t        now_ms    = millis();
    if (now_ms - last_ms < 80u) {
        return;
    }
    last_ms = now_ms;

    bool paper_ok = (PAPER_SENSOR_PIN != PAPER_DISABLED) && (digitalRead(PAPER_SENSOR_PIN) == 0);
    bool idle     = (sys.state == State::Idle);

    if (paper_auto_change_running) {
        led_on = !led_on;
        paper_led_set(led_on);
        return;
    }
    if (idle) {
        if (paper_ok) {
            paper_led_set(true);
        } else {
            static uint32_t slow_last;
            if (now_ms - slow_last >= 500u) {
                slow_last = now_ms;
                led_on    = !led_on;
            }
            paper_led_set(led_on);
        }
        return;
    }
    paper_led_set(paper_ok);
}
#else
void paper_led_update(void) {}
#endif

// 每 YIELD_STEPS 步 yield 一次，避免长时间阻塞触发 ESP32 Interrupt Watchdog (Core 1 panic)
#define PAPER_YIELD_STEPS 50u

// 拾落夹紧后面板进纸：单步，前 PAPER_PANEL_FAST_RAMP_STEPS 缓起步，之后用 PAPER_PANEL_FAST_*（加速更早）
static void paper_one_step_panel_after_clamp(uint32_t step_index) {
    uint32_t hi_us = (step_index < PAPER_PANEL_FAST_RAMP_STEPS) ? PAPER_RAMP_HI_US : PAPER_PANEL_FAST_HI_US;
    uint32_t lo_us = (step_index < PAPER_PANEL_FAST_RAMP_STEPS) ? PAPER_RAMP_LO_US : PAPER_PANEL_FAST_LO_US;
    digitalWrite(PANEL_MOTOR_STEP_PIN, HIGH);
#ifdef USE_I2S_OUT
    i2s_out_delay();
#endif
    delayMicroseconds(hi_us);
    digitalWrite(PANEL_MOTOR_STEP_PIN, LOW);
#ifdef USE_I2S_OUT
    i2s_out_delay();
#endif
    delayMicroseconds(lo_us);
}

// 拾落夹紧后面板进纸：连续 steps 步，前 PAPER_PANEL_FAST_RAMP_STEPS 缓起步，之后用快速脉宽（步骤8 用）
static void paper_step_pulses_panel_after_clamp(uint32_t steps) {
#ifdef USE_I2S_OUT
    for (uint32_t i = 0; i < steps; i++) {
        uint32_t hi_us = (i < PAPER_PANEL_FAST_RAMP_STEPS) ? PAPER_RAMP_HI_US : PAPER_PANEL_FAST_HI_US;
        uint32_t lo_us = (i < PAPER_PANEL_FAST_RAMP_STEPS) ? PAPER_RAMP_LO_US : PAPER_PANEL_FAST_LO_US;
        digitalWrite(PANEL_MOTOR_STEP_PIN, HIGH);
        i2s_out_delay();
        delayMicroseconds(hi_us);
        digitalWrite(PANEL_MOTOR_STEP_PIN, LOW);
        i2s_out_delay();
        delayMicroseconds(lo_us);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) delay(1);
    }
#else
    for (uint32_t i = 0; i < steps; i++) {
        uint32_t hi_us = (i < PAPER_PANEL_FAST_RAMP_STEPS) ? 400u : PAPER_PANEL_FAST_HI_US;
        uint32_t lo_us = (i < PAPER_PANEL_FAST_RAMP_STEPS) ? 400u : PAPER_PANEL_FAST_LO_US;
        digitalWrite(PANEL_MOTOR_STEP_PIN, HIGH);
        delayMicroseconds(hi_us);
        digitalWrite(PANEL_MOTOR_STEP_PIN, LOW);
        delayMicroseconds(lo_us);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) delay(1);
    }
#endif
}

static void paper_step_pulses(uint8_t step_pin, uint16_t steps) {
#ifdef USE_I2S_OUT
    uint32_t hi_us, lo_us;
    for (uint16_t i = 0; i < steps; i++) {
        if (step_pin == PANEL_MOTOR_STEP_PIN) {
            if (i < PAPER_RAMP_STEPS) {
                hi_us = PAPER_RAMP_HI_US;
                lo_us = PAPER_RAMP_LO_US;
            } else {
                hi_us = PAPER_NORMAL_HI_US;
                lo_us = PAPER_NORMAL_LO_US;
            }
        } else if (step_pin == FEEDER_MOTOR_STEP_PIN) {
            if (i < PAPER_RAMP_STEPS) {
                hi_us = FEEDER_FEED_RAMP_HI_US;
                lo_us = FEEDER_FEED_RAMP_LO_US;
            } else {
                hi_us = FEEDER_FEED_NORMAL_HI_US;
                lo_us = FEEDER_FEED_NORMAL_LO_US;
            }
        } else {
            hi_us = PAPER_CLAMP_HI_US;
            lo_us = PAPER_CLAMP_LO_US;
        }
        digitalWrite(step_pin, HIGH);
        i2s_out_delay();
        delayMicroseconds(hi_us);
        digitalWrite(step_pin, LOW);
        i2s_out_delay();
        delayMicroseconds(lo_us);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) {
            delay(1);  // yield to RTOS, feed interrupt watchdog
        }
    }
#else
    for (uint16_t i = 0; i < steps; i++) {
        digitalWrite(step_pin, HIGH);
        delayMicroseconds(500);
        digitalWrite(step_pin, LOW);
        delayMicroseconds(500);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) {
            delay(1);  // yield to RTOS, feed interrupt watchdog
        }
    }
#endif
}

// 进纸器“找传感器”阶段专用步进：比默认快一倍（FEEDER_FIND_*）
static void paper_step_pulses_feeder_find(uint16_t steps) {
#ifdef USE_I2S_OUT
    uint32_t hi_us, lo_us;
    for (uint16_t i = 0; i < steps; i++) {
        if (i < PAPER_RAMP_STEPS) {
            hi_us = FEEDER_FIND_RAMP_HI_US;
            lo_us = FEEDER_FIND_RAMP_LO_US;
        } else {
            hi_us = FEEDER_FIND_NORMAL_HI_US;
            lo_us = FEEDER_FIND_NORMAL_LO_US;
        }
        digitalWrite(FEEDER_MOTOR_STEP_PIN, HIGH);
        i2s_out_delay();
        delayMicroseconds(hi_us);
        digitalWrite(FEEDER_MOTOR_STEP_PIN, LOW);
        i2s_out_delay();
        delayMicroseconds(lo_us);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) {
            delay(1);
        }
    }
#else
    for (uint16_t i = 0; i < steps; i++) {
        uint32_t hi_us = (i < PAPER_RAMP_STEPS) ? FEEDER_FIND_RAMP_HI_US : FEEDER_FIND_NORMAL_HI_US;
        uint32_t lo_us = (i < PAPER_RAMP_STEPS) ? FEEDER_FIND_RAMP_LO_US : FEEDER_FIND_NORMAL_LO_US;
        digitalWrite(FEEDER_MOTOR_STEP_PIN, HIGH);
        delayMicroseconds(hi_us);
        digitalWrite(FEEDER_MOTOR_STEP_PIN, LOW);
        delayMicroseconds(lo_us);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) {
            delay(1);
        }
    }
#endif
}

// 标记I2S是否已初始化（延迟到第一次需要时）
static bool paper_i2s_setup = false;

void paper_system_init(void) {
#ifdef PAPER_DRIVER_REF_PIN
    pinMode((int)PAPER_DRIVER_REF_PIN, OUTPUT);
    dacWrite((int)PAPER_DRIVER_REF_PIN, PAPER_DRIVER_REF_DAC);
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[Paper] DAC REF initialized: GPIO%u = %u (0-255), clamp/panel/feeder = %u/%u/%u",
                   (unsigned)PAPER_DRIVER_REF_PIN, (unsigned)PAPER_DRIVER_REF_DAC,
                   (unsigned)PAPER_REF_DAC_CLAMP, (unsigned)PAPER_REF_DAC_PANEL, (unsigned)PAPER_REF_DAC_FEEDER);
#endif
    // 默认关闭所有纸路电机使能（互斥两组的初始状态：全部失能）
    pinMode((int)PAPER_ENABLE_PIN, OUTPUT);
    digitalWrite(PAPER_ENABLE_PIN, HIGH);  // 面板 EN=HIGH → 禁用
#ifdef PAPER_DRIVER_ENABLE_PIN
    pinMode((int)PAPER_DRIVER_ENABLE_PIN, OUTPUT);
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, HIGH);  // 拾落 + 进纸器 EN=HIGH → 禁用
#endif

    if (PAPER_SENSOR_PIN != PAPER_DISABLED) {
        // GPIO34: 3.3V=NoP(无纸), 0V=HavP(有纸)
        pinMode((int)PAPER_SENSOR_PIN, INPUT);
        int initial_value = digitalRead(PAPER_SENSOR_PIN);
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "[Paper] System ready vM716 - GPIO34(HIGH=NoP LOW=HavP) initial=%d [ESP901] [ESP911/912/913] [ESP930] [ESP910]",
                       initial_value);
    } else {
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning, "[Paper] PAPER_SENSOR_PIN disabled (255)");
    }
#ifdef PAPER_LED_PIN
    paper_led_update();
#endif
    // 注意：I2S_OUT已在Grbl.cpp中全局初始化，这里只在需要时切到 passthrough
}


#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
void paper_get_status_str(char* buf, size_t len) {
    if (PAPER_SENSOR_PIN == PAPER_DISABLED || len < 32) {
        snprintf(buf, len, "paper system not configured");
        return;
    }
    // GPIO34: LOW(0)=有纸, HIGH(1)=无纸
    bool paper_ok = (digitalRead(PAPER_SENSOR_PIN) == 0);
    bool en_ok    = !digitalRead(PAPER_ENABLE_PIN);
#ifdef PAPER_DRIVER_ENABLE_PIN
    en_ok = en_ok || !digitalRead(PAPER_DRIVER_ENABLE_PIN);
#endif
    snprintf(buf, len, "Paper=%s MotorEn=%s", paper_ok ? "OK" : "No", en_ok ? "On" : "Off");
}

// 内部辅助函数：确保 I2S 处于 passthrough 模式
static void paper_ensure_i2s_passthrough(void) {
#ifdef USE_I2S_OUT
    if (!paper_i2s_setup) {
        i2s_out_set_passthrough();  // I2S已在Grbl.cpp全局初始化，这里仅设置passthrough模式
        paper_i2s_setup = true;
    }
    delay(I2S_OUT_DELAY_MS * 2);
    i2s_out_delay();
#endif
}

// 内部辅助函数：启用所有纸路驱动（面板 + 拾落 + 进纸器）
static void paper_enable_drivers(void) {
    paper_ensure_i2s_passthrough();
    digitalWrite(PAPER_ENABLE_PIN, LOW);
#ifdef PAPER_DRIVER_ENABLE_PIN
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, LOW);
#endif
#ifdef USE_I2S_OUT
    i2s_out_delay();
    delay(5);
#endif
}

// 内部辅助函数：禁用驱动
void paper_disable_drivers(void) {
    digitalWrite(PAPER_ENABLE_PIN, HIGH);
#ifdef PAPER_DRIVER_ENABLE_PIN
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, HIGH);
#endif
}

#ifdef PAPER_DRIVER_REF_PIN
// 按当前运行的电机切换 REF（拾落/面板/进纸器可单独设定 DAC 值，见 custom_3axis_hr4988.h 中 PAPER_REF_DAC_*）
static void paper_set_ref_dac(uint8_t dac_val) {
    dacWrite((int)PAPER_DRIVER_REF_PIN, dac_val);
}
#endif

// 仅使能面板电机（互斥：关闭拾落 + 进纸器）
static void paper_enable_panel_only(void) {
    paper_ensure_i2s_passthrough();
#ifdef PAPER_DRIVER_REF_PIN
    if (PAPER_REF_SOFTSTART_MS > 0) {
        paper_set_ref_dac(0);
    } else {
        paper_set_ref_dac(PAPER_REF_DAC_PANEL);
    }
#endif
    digitalWrite(PAPER_ENABLE_PIN, LOW);
#ifdef PAPER_DRIVER_ENABLE_PIN
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, HIGH);
#endif
#ifdef PAPER_DRIVER_REF_PIN
    if (PAPER_REF_SOFTSTART_MS > 0) {
        delay(PAPER_REF_SOFTSTART_MS);
        paper_set_ref_dac(PAPER_REF_DAC_PANEL);
    }
#endif
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperEn] panel_only: Q1=LOW, DRV_EN=HIGH");
}

// 仅使能拾落 + 进纸器（互斥：关闭面板）；REF 默认进纸器档，步进拾落前需再设 PAPER_REF_DAC_CLAMP
static void paper_enable_clamp_feeder_only(void) {
    paper_ensure_i2s_passthrough();
#ifdef PAPER_DRIVER_REF_PIN
    if (PAPER_REF_SOFTSTART_MS > 0) {
        paper_set_ref_dac(0);
    } else {
        paper_set_ref_dac(PAPER_REF_DAC_FEEDER);
    }
#endif
    digitalWrite(PAPER_ENABLE_PIN, HIGH);
#ifdef PAPER_DRIVER_ENABLE_PIN
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, LOW);
#endif
#ifdef PAPER_DRIVER_REF_PIN
    if (PAPER_REF_SOFTSTART_MS > 0) {
        delay(PAPER_REF_SOFTSTART_MS);
        paper_set_ref_dac(PAPER_REF_DAC_FEEDER);
    }
#endif
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperEn] clamp_feeder_only: Q1=HIGH, DRV_EN=LOW");
}

// 使能面板 + 进纸器（拾落抬起后二者同速送纸用；拾落也上电但不发步进）
static void paper_enable_panel_and_feeder(void) {
    paper_ensure_i2s_passthrough();
    uint8_t ref_target = (PAPER_REF_DAC_PANEL) > (PAPER_REF_DAC_FEEDER) ? (PAPER_REF_DAC_PANEL) : (PAPER_REF_DAC_FEEDER);
#ifdef PAPER_DRIVER_REF_PIN
    if (PAPER_REF_SOFTSTART_MS > 0) {
        paper_set_ref_dac(0);
    } else {
        paper_set_ref_dac(ref_target);
    }
#endif
    digitalWrite(PAPER_ENABLE_PIN, LOW);
#ifdef PAPER_DRIVER_ENABLE_PIN
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, LOW);
#endif
#ifdef PAPER_DRIVER_REF_PIN
    if (PAPER_REF_SOFTSTART_MS > 0) {
        delay(PAPER_REF_SOFTSTART_MS);
        paper_set_ref_dac(ref_target);
    }
#endif
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperEn] panel_and_feeder: Q1=LOW, DRV_EN=LOW");
}

// 面板与进纸器同速同步步进（每步两个电机各发一个脉冲，相同脉宽）
static void paper_step_pulses_panel_feeder_sync(uint32_t steps) {
#ifdef USE_I2S_OUT
    uint32_t hi_us, lo_us;
    for (uint32_t i = 0; i < steps; i++) {
        if (i < PAPER_RAMP_STEPS) {
            hi_us = PAPER_RAMP_HI_US;
            lo_us = PAPER_RAMP_LO_US;
        } else {
            hi_us = PAPER_NORMAL_HI_US;
            lo_us = PAPER_NORMAL_LO_US;
        }
        digitalWrite(PANEL_MOTOR_STEP_PIN, HIGH);
        digitalWrite(FEEDER_MOTOR_STEP_PIN, HIGH);
        i2s_out_delay();
        delayMicroseconds(hi_us);
        digitalWrite(PANEL_MOTOR_STEP_PIN, LOW);
        digitalWrite(FEEDER_MOTOR_STEP_PIN, LOW);
        i2s_out_delay();
        delayMicroseconds(lo_us);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) {
            delay(1);
        }
    }
#else
    for (uint32_t i = 0; i < steps; i++) {
        digitalWrite(PANEL_MOTOR_STEP_PIN, HIGH);
        digitalWrite(FEEDER_MOTOR_STEP_PIN, HIGH);
        delayMicroseconds(500);
        digitalWrite(PANEL_MOTOR_STEP_PIN, LOW);
        digitalWrite(FEEDER_MOTOR_STEP_PIN, LOW);
        delayMicroseconds(500);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) {
            delay(1);
        }
    }
#endif
}

Error paper_run_motor(uint8_t motor_ix, uint16_t steps) {
    if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
        return Error::GcodeUnsupportedCommand;
    }
    if (steps == 0) {
        steps = 200;
    }
    if (steps > 10000) {
        steps = 10000;
    }
    uint8_t     step_pin;
    const char* motor_name;
    if (motor_ix == 0) {
        step_pin   = CLAMP_MOTOR_STEP_PIN;
        motor_name = "Clamp";
        paper_enable_clamp_feeder_only();
#ifdef PAPER_DRIVER_REF_PIN
        paper_set_ref_dac(PAPER_REF_DAC_CLAMP);
#endif
    } else if (motor_ix == 1) {
        step_pin   = PANEL_MOTOR_STEP_PIN;
        motor_name = "Panel";
        paper_enable_panel_only();
    } else if (motor_ix == 2) {
        step_pin   = FEEDER_MOTOR_STEP_PIN;
        motor_name = "Feeder";
        paper_enable_clamp_feeder_only();
    } else {
        return Error::GcodeUnsupportedCommand;
    }
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperMotor] %s: jog start (%u steps)", motor_name, (unsigned)steps);
    paper_step_pulses(step_pin, steps);
    paper_disable_drivers();
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperMotor] %s: jog complete", motor_name);
    return Error::Ok;
}

// 仅使能换纸驱动（I2S passthrough + 拉低 EN），不动作；便于用 M64/M65 设方向后单独点动调试
void paper_enable_drivers_only(void) {
    paper_enable_drivers();
}

// 纸张传感器读取（true=感应到纸，false=未感应到）
// GPIO34: LOW(0)=有纸, HIGH(1)=无纸
static inline bool paper_sensor_active() {
    return digitalRead(PAPER_SENSOR_PIN) == 0;
}

// 纸张传感器防抖读取：连续采样，确保稳定（高速步进时传感器易抖动）
static inline bool paper_sensor_stable() {
    int count_low = 0;
    for (int i = 0; i < 3; i++) {
        if (digitalRead(PAPER_SENSOR_PIN) == 0) count_low++;
        delayMicroseconds(100);  // 100us 防抖窗口
    }
    return count_low >= 2;  // 至少2次读到 LOW 才认为有纸
}

// 设定方向并发送 N 步脉冲（DIR 先稳定再 STEP，避免丢步）
static void paper_dir_steps(uint8_t dir_pin, bool dir_level, uint8_t step_pin, uint32_t steps) {
    digitalWrite(dir_pin, dir_level);
#ifdef USE_I2S_OUT
    i2s_out_delay();
    delay(2);
#endif
    paper_step_pulses(step_pin, (uint16_t)steps);
}

#ifdef PAPER_EJECT_NORMAL_HI_US
// 出旧纸专用：使用更短脉宽（约 2 倍速），仅用于 Step1 面板弹出旧纸
static void paper_step_pulses_panel_eject(uint32_t steps) {
#ifdef USE_I2S_OUT
    for (uint32_t i = 0; i < steps; i++) {
        uint32_t hi_us = (i < PAPER_RAMP_STEPS) ? PAPER_EJECT_RAMP_HI_US : PAPER_EJECT_NORMAL_HI_US;
        uint32_t lo_us = (i < PAPER_RAMP_STEPS) ? PAPER_EJECT_RAMP_LO_US : PAPER_EJECT_NORMAL_LO_US;
        digitalWrite(PANEL_MOTOR_STEP_PIN, HIGH);
        i2s_out_delay();
        delayMicroseconds(hi_us);
        digitalWrite(PANEL_MOTOR_STEP_PIN, LOW);
        i2s_out_delay();
        delayMicroseconds(lo_us);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) delay(1);
    }
#else
    for (uint32_t i = 0; i < steps; i++) {
        uint32_t hi_us = (i < PAPER_RAMP_STEPS) ? PAPER_EJECT_RAMP_HI_US : PAPER_EJECT_NORMAL_HI_US;
        uint32_t lo_us = (i < PAPER_RAMP_STEPS) ? PAPER_EJECT_RAMP_LO_US : PAPER_EJECT_NORMAL_LO_US;
        digitalWrite(PANEL_MOTOR_STEP_PIN, HIGH);
        delayMicroseconds(hi_us);
        digitalWrite(PANEL_MOTOR_STEP_PIN, LOW);
        delayMicroseconds(lo_us);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) delay(1);
    }
#endif
}
#endif

// 一键自动换纸流程（[ESP910] / M30 调用）
// 步骤：1 弹旧纸 → 2 进纸器找纸 → 3 松夹 → 4 面板+进纸器同速送纸 → 5 夹紧 → 6 面板快送直到脱传感器 → 7 回找传感器 → 8 最终对位 → 9 失能
// 结束时会发送 [PaperStatus] N（0=成功，2=进纸超时，3=第7步未找到传感器；1 保留）
Error paper_auto_change(void) {
    if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
        return Error::GcodeUnsupportedCommand;
    }

    // 允许起始有纸：用于“开始队列前出旧纸”和“M30 后出本页再进下一页”。第 1 步会先弹旧纸，再进新纸。
    paper_auto_change_running = true;
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto] Starting auto paper change...");
    // 先全部失能，再按步骤使能需要运动的电机，避免不运动时电机仍带电
    paper_disable_drivers();
    delay(2);

    // 1. 面板电机先运动，弹出旧纸（A4 长度 + 余量）
    // 仅面板电机工作（组A），拾落 + 进纸器失能（组B）
    paper_enable_panel_only();
    // 方向：按你的机械，出旧纸与进新纸同向运动 → 使用 PANEL_DIR_EJECT（等于 PANEL_DIR_FEED）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-1] Ejecting old paper (%u steps)...", (unsigned)PANEL_EJECT_STEPS);
#ifdef PAPER_EJECT_NORMAL_HI_US
    digitalWrite(PANEL_MOTOR_DIR_PIN, PANEL_DIR_EJECT);
#ifdef USE_I2S_OUT
    i2s_out_delay();
    delay(2);
#endif
    paper_step_pulses_panel_eject(PANEL_EJECT_STEPS);
#else
    paper_dir_steps(PANEL_MOTOR_DIR_PIN, PANEL_DIR_EJECT, PANEL_MOTOR_STEP_PIN, PANEL_EJECT_STEPS);
#endif
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-1] Done");

    // 2. 进纸器开始运动，直到纸张传感器检测到纸或达到上限
    // 仅进纸器工作（组B），面板失能（组A）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-2] Feeder searching for paper (max %u steps)...", (unsigned)FEEDER_FIND_STEPS_MAX);
    {
        uint32_t steps = 0;
        bool     found = false;
        paper_enable_clamp_feeder_only();
        digitalWrite(FEEDER_MOTOR_DIR_PIN, FEEDER_DIR_FORWARD);
#ifdef USE_I2S_OUT
        i2s_out_delay();
        delay(2);
#endif
        while (steps < FEEDER_FIND_STEPS_MAX) {
            if (paper_sensor_stable()) {  // 改用防抖读取
                found = true;
                break;
            }
            paper_step_pulses_feeder_find(1);  // 找传感器阶段：加速一倍
            steps++;
            if (steps % PAPER_YIELD_STEPS == 0) {
                delay(1);  // yield to RTOS, avoid Interrupt wdt timeout
            }
        }
        if (!found) {
            paper_auto_change_running = false;
            paper_disable_drivers();  // 超时提前退出时也要关驱动，避免电机长时间使能
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning, "[PaperAuto-2] ERROR: Feeder timeout - sensor not triggered after %u steps", (unsigned)steps);
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperStatus] %d", PAPER_STATUS_FEEDER_TIMEOUT);
            return Error::Ok;  // Do not raise hard error, just warn
        }
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-2] Paper found at step %u", (unsigned)steps);
    }

    // 3. 传感器感应到纸后，松开拾落电机（面板+进纸器提前使能，拾落松开后面板不中断直接进入步骤4）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-3] Releasing clamp (%u steps)...", (unsigned)CLAMP_TOGGLE_STEPS);
    paper_enable_panel_and_feeder();  // 面板与进纸器先使能，步骤3只步进拾落，步骤4 不再切换使能
#ifdef PAPER_DRIVER_REF_PIN
    paper_set_ref_dac(PAPER_REF_DAC_CLAMP);
#endif
    paper_dir_steps(CLAMP_MOTOR_DIR_PIN, CLAMP_DIR_RELEASE, CLAMP_MOTOR_STEP_PIN, CLAMP_TOGGLE_STEPS);
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-3] Done");

    // 4. 拾落抬起后面板与进纸器同速送纸 6cm，然后面板+进纸器停止，仅拾落夹紧；夹紧完成后再同速送纸至 8cm，送纸器停止
    uint32_t steps_before_clamp = (uint32_t)(PAPER_ADVANCE_CM_CLAMP_START) * (uint32_t)(PAPER_STEPS_PER_CM);
    uint32_t total_feed_steps   = (uint32_t)(PAPER_ADVANCE_CM) * (uint32_t)(PAPER_STEPS_PER_CM);
    uint32_t steps_after_clamp  = (total_feed_steps > steps_before_clamp) ? (total_feed_steps - steps_before_clamp) : 0u;

    digitalWrite(PANEL_MOTOR_DIR_PIN, PANEL_DIR_FEED);
    digitalWrite(FEEDER_MOTOR_DIR_PIN, FEEDER_DIR_FORWARD);
#ifdef USE_I2S_OUT
    i2s_out_delay();
    delay(2);
#endif
#ifdef PAPER_DRIVER_REF_PIN
    paper_set_ref_dac((PAPER_REF_DAC_PANEL) > (PAPER_REF_DAC_FEEDER) ? (PAPER_REF_DAC_PANEL) : (PAPER_REF_DAC_FEEDER));
#endif
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-4] Panel+Feeder sync %.1fcm...", (float)PAPER_ADVANCE_CM_CLAMP_START);
    paper_step_pulses_panel_feeder_sync(steps_before_clamp);

    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-5] Clamping (%u steps, panel+feeder stopped)...", (unsigned)CLAMP_TOGGLE_STEPS);
#ifdef PAPER_DRIVER_REF_PIN
    paper_set_ref_dac(PAPER_REF_DAC_CLAMP);
#endif
    paper_dir_steps(CLAMP_MOTOR_DIR_PIN, CLAMP_DIR_CLAMP, CLAMP_MOTOR_STEP_PIN, CLAMP_TOGGLE_STEPS);
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-5] Done");

    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-4b] Panel+Feeder sync again to %.1fcm total...", (float)PAPER_ADVANCE_CM);
#ifdef PAPER_DRIVER_REF_PIN
    paper_set_ref_dac((PAPER_REF_DAC_PANEL) > (PAPER_REF_DAC_FEEDER) ? (PAPER_REF_DAC_PANEL) : (PAPER_REF_DAC_FEEDER));
#endif
    paper_step_pulses_panel_feeder_sync(steps_after_clamp);
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-4/5] Done (feeder stops at %.1fcm)", (float)PAPER_ADVANCE_CM);
    paper_enable_panel_only();

    // 6. 仅面板电机快速送纸，直到传感器“看不到纸”为止或达到上限（进纸器已在步骤5后失能）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-6] Panel fast feed until sensor loses paper (max %u steps)...", (unsigned)PANEL_FAST_STEPS_MAX);
    {
        uint32_t steps = 0;
        digitalWrite(PANEL_MOTOR_DIR_PIN, PANEL_DIR_FEED);
#ifdef USE_I2S_OUT
        i2s_out_delay();
        delay(2);
#endif
        while (paper_sensor_stable() && steps < PANEL_FAST_STEPS_MAX) {
            paper_one_step_panel_after_clamp(steps);  // 夹紧后面板进纸速度加倍
            steps++;
            if (steps % PAPER_YIELD_STEPS == 0) delay(1);
        }
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-6] Fast feed completed (%u steps, sensor=%s)", 
                       (unsigned)steps, paper_sensor_stable() ? "STILL_ACTIVE" : "lost");
    }

    // 7. 面板电机“回找传感器”，直到再次“感应到纸”或达到上限（回找定位点）
    bool step7_sensor_ok = true;
    paper_enable_panel_only();
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-7] Panel reverse to find paper again (max %u steps)...", (unsigned)PANEL_BACK_STEPS_MAX);
    {
        uint32_t steps = 0;
        digitalWrite(PANEL_MOTOR_DIR_PIN, PANEL_DIR_REVERSE);
#ifdef USE_I2S_OUT
        i2s_out_delay();
        delay(2);
#endif
        while (!paper_sensor_stable() && steps < PANEL_BACK_STEPS_MAX) {
            paper_step_pulses(PANEL_MOTOR_STEP_PIN, 1);
            steps++;
            if (steps % PAPER_YIELD_STEPS == 0) {
                delay(1);  // yield to RTOS, avoid Interrupt wdt timeout
            }
        }
        step7_sensor_ok = paper_sensor_stable();
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-7] Panel re-search completed (%u steps, sensor=%s)", 
                       (unsigned)steps, step7_sensor_ok ? "found" : "NOT_found");
        if (!step7_sensor_ok) {
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning, 
                           "[PaperAuto-7-WARNING] Sensor NOT stable after reverse search - paper may not be in correct position!");
        }
    }

    // 8. 面板电机再向送纸方向走固定步数，作为最终对位（夹紧后速度加倍）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-8] Final alignment (%u steps)...", (unsigned)PANEL_FINAL_STEPS);
    digitalWrite(PANEL_MOTOR_DIR_PIN, PANEL_DIR_FEED);
#ifdef USE_I2S_OUT
    i2s_out_delay();
    delay(2);
#endif
    paper_step_pulses_panel_after_clamp(PANEL_FINAL_STEPS);
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-8] Done");

    // 9. 换纸流程完成后，关闭换纸相关电机使能，防止长时间发热
    paper_disable_drivers();

    paper_auto_change_running = false;
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto] All steps completed successfully!");
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperStatus] %d", step7_sensor_ok ? PAPER_STATUS_OK : PAPER_STATUS_SENSOR_NOT_FOUND);
    return Error::Ok;
}

Error paper_system_mcode(uint16_t code, uint16_t steps, int8_t clamp_dir) {
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
            // GPIO34: LOW(0)=有纸, HIGH(1)=无纸
            int  raw_val  = digitalRead(PAPER_SENSOR_PIN);
            bool paper_ok = (raw_val == 0);
            bool en_ok    = !digitalRead(PAPER_ENABLE_PIN);
#ifdef PAPER_DRIVER_ENABLE_PIN
            en_ok = en_ok || !digitalRead(PAPER_DRIVER_ENABLE_PIN);
#endif
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info,
                           "M701: GPIO34=%d Paper=%s MotorEn=%s",
                           raw_val,
                           paper_ok ? "OK" : "No", en_ok ? "On" : "Off");
            return Error::Ok;
        }
        case 704: {  // 调试：直接读取 GPIO34 电平（HIGH=无纸，LOW=有纸）
            if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M704: paper sensor not configured");
                return Error::Ok;
            }
            int  raw       = digitalRead(PAPER_SENSOR_PIN);
            bool paper_ok  = (raw == 0);  // LOW(0)=有纸(OK), HIGH(1)=无纸(No)
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info,
                           "M704: GPIO%u raw=%d (HIGH=1/NoP, LOW=0/HavP) -> Paper=%s",
                           (unsigned)PAPER_SENSOR_PIN,
                           raw,
                           paper_ok ? "OK(有纸)" : "No(无纸)");
            return Error::Ok;
        }
        case 711:
        case 712:
        case 713: {
            if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M%u: paper system not configured", (unsigned)code);
                return Error::Ok;
            }
            uint16_t nsteps = (steps > 0 && steps <= 10000) ? steps : 200;
            const char* motor_name = (code == 711) ? "Clamp" : (code == 712) ? "Panel" : "Feeder";
            uint8_t step_pin = (code == 711) ? CLAMP_MOTOR_STEP_PIN : (code == 712) ? PANEL_MOTOR_STEP_PIN : FEEDER_MOTOR_STEP_PIN;
            
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M%u (%s): jog start (%u steps)", (unsigned)code, motor_name, (unsigned)nsteps);
            paper_enable_drivers();
            paper_step_pulses(step_pin, nsteps);
            paper_disable_drivers();
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M%u (%s): jog complete", (unsigned)code, motor_name);
            return Error::Ok;
        }
        case 716: {  // 新增：M716 Qd Pd - 拾落电机单独控制方向和步数
            if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M716: paper system not configured");
                return Error::Ok;
            }
            // 步数：0 表示使用 CLAMP_TOGGLE_STEPS
            uint16_t nsteps = (steps > 0 && steps <= 10000) ? steps : 220;  // 默认 220 步（夹紧和松开都一样）
            // 方向逻辑：Q=0 为夹紧(clamp)，Q=1 为松开(release)；默认为 0(夹紧)
            bool do_clamp = (clamp_dir != 1);  // Q=0 或未提供 → 夹紧; Q=1 → 松开
            
            bool dir_level = do_clamp ? CLAMP_DIR_CLAMP : CLAMP_DIR_RELEASE;

            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info,
                           "M716: Clamp motor %s %u steps (Q=%d)",
                           do_clamp ? "CLAMP(Q0)" : "RELEASE(Q1)",
                           (unsigned)nsteps,
                           (int)clamp_dir);
            paper_enable_drivers();
            paper_dir_steps(CLAMP_MOTOR_DIR_PIN, dir_level, CLAMP_MOTOR_STEP_PIN, nsteps);
            paper_disable_drivers();
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M716: done");

            return Error::Ok;
        }
        case 721: {  // 队列开始：自动执行一次换纸（出旧纸+进第一张），上位机在发第一页前发本行即可，无需人工干预
            if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "M721: paper system not configured");
                return Error::Ok;
            }
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[M721] Queue start: running one paper change...");
            Error e = paper_auto_change();
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[M721] Done.");
            return e;
        }
        default:
            if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
                return Error::GcodeUnsupportedCommand;
            }
            return Error::GcodeUnsupportedCommand;
    }
}
#endif
