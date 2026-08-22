/*
 * PaperSystem.cpp - 换纸系统: M701/M711/M712/M713 �?[ESP901/911/912/913]
 * 74HC595 通过 I2S 扩展，需先切�?passthrough 并留足延时，电平才会真正输出�?
 */
#include "Grbl.h"
#ifdef USE_I2S_OUT
#    include "I2SOut.h"
#endif
#include <atomic>

#define PAPER_DISABLED 255

// 换纸流程结束状态码（上位机可解析 [PaperStatus] N 做分支）
#define PAPER_STATUS_OK                0
#define PAPER_STATUS_PAPER_PRESENT     1  // 开始时传感器仍有纸，无法弹旧纸
#define PAPER_STATUS_FEEDER_TIMEOUT   2  // 进纸阶段超时未触发传感器
#define PAPER_STATUS_SENSOR_NOT_FOUND 3  // 第7步回找后传感器未稳定（纸可能未到位）
#define PAPER_STATUS_JAM_TIMEOUT      4  // 传感器持续有纸超时（卡纸）
#define PAPER_STATUS_OUT_OF_PAPER     5  // 进纸超时无纸（缺纸）

// 传感器关键阶段超时时间：10s
#define PAPER_SENSOR_TIMEOUT_MS 10000u

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
// hutuji §9-E′（R20-GW-03）：由 volatile 改为 std::atomic——入口认领需要
// test-and-set 的原子性（volatile 只保证不优化掉访问，不保证读改写不可分割）。
// 其余读写点语义不变：atomic<bool> 的隐式 load/store 与原 volatile 一致。
static std::atomic<bool> paper_auto_change_running{false};
// 换纸成功后的面板低电流保持（写字/点动/回零前释放；LED 刷新也需跳过）
static volatile bool paper_panel_low_hold_active = false;

// 换纸开始后短时忽略上位机 0x18（蓝牙连接常误发软复位，会打断弹纸）
static uint32_t paper_ignore_host_reset_until_ms = 0;

// 蓝牙：SPP 连上后等上位机首条指令回完 ok/ack，再立刻预约并执行换纸
static bool paper_bt_connect_auto_change_pending = false;
static bool paper_bt_auto_change_after_ack_armed = false;

#if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
bool paper_auto_change_is_running(void) {
    return paper_auto_change_running;
}

bool paper_should_ignore_host_reset(void) {
    if (!paper_auto_change_running) {
        return false;
    }
    uint32_t now = millis();
    return paper_ignore_host_reset_until_ms != 0 && now < paper_ignore_host_reset_until_ms;
}
#endif

static void paper_enable_panel_only(void);
#ifdef PAPER_DRIVER_REF_PIN
static void paper_set_ref_dac(uint8_t dac_val);
#endif

#ifdef PAPER_LED_PIN
#ifdef USE_I2S_OUT
// 前置声明：供 paper_led_set() 调用（定义在后文）
static void paper_ensure_i2s_passthrough(void);
#endif

// 按键彩灯：Q0/QA，HIGH=灭，LOW=亮
static void paper_led_set(bool on) {
    static bool led_cached_on    = false;
    static bool led_cache_valid  = false;
    const bool  target_on        = on;
    if (led_cache_valid && (led_cached_on == target_on)) {
        return;
    }
#ifdef USE_I2S_OUT
    paper_ensure_i2s_passthrough();
#endif
    digitalWrite(PAPER_LED_PIN, target_on ? LOW : HIGH);
    led_cached_on   = target_on;
    led_cache_valid = true;
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

    // 雕刻/点动/回零时，以及换纸后面板低电流保持期间，不再刷新 74HC595。
    // LED 与 PANEL_MOTOR_STEP 同芯片；hold 时使能仍开，重复锁存易微步进回退对位。
    if (sys.state == State::Cycle || sys.state == State::Jog || sys.state == State::Homing || sys.state == State::Hold ||
        paper_panel_low_hold_active) {
        return;
    }

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

// 换纸流程里会长时间用 delay/延时打步进脉冲；
// 此期间主协议线程不会持续执行 st_prep_buffer()，导致 segment buffer 被 ISR 耗空而 st_go_idle。
// 在每个“yield 点”额外续料一次 segment buffer，尽量避免运动卡顿。
static inline bool paper_refill_segment_buffer_during_blocking() {
    // 只在运动相关状态下续料，避免无意义计算
    if (sys.state == State::Cycle || sys.state == State::Hold || sys.state == State::SafetyDoor || sys.state == State::Homing ||
        sys.state == State::Sleep || sys.state == State::Jog) {
        st_prep_buffer();
    }
    protocol_service_during_blocking();
    return sys.abort;
}

static inline bool paper_blocking_abort_requested(void) {
    return paper_refill_segment_buffer_during_blocking();
}

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
        if ((i + 1) % PAPER_YIELD_STEPS == 0) {
            paper_refill_segment_buffer_during_blocking();
            delay(1);
        }
    }
#else
    for (uint32_t i = 0; i < steps; i++) {
        uint32_t hi_us = (i < PAPER_PANEL_FAST_RAMP_STEPS) ? 400u : PAPER_PANEL_FAST_HI_US;
        uint32_t lo_us = (i < PAPER_PANEL_FAST_RAMP_STEPS) ? 400u : PAPER_PANEL_FAST_LO_US;
        digitalWrite(PANEL_MOTOR_STEP_PIN, HIGH);
        delayMicroseconds(hi_us);
        digitalWrite(PANEL_MOTOR_STEP_PIN, LOW);
        delayMicroseconds(lo_us);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) {
            paper_refill_segment_buffer_during_blocking();
            delay(1);
        }
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
            paper_refill_segment_buffer_during_blocking();
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
            paper_refill_segment_buffer_during_blocking();
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
            paper_refill_segment_buffer_during_blocking();
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
            paper_refill_segment_buffer_during_blocking();
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
#    ifndef Z_REF_DAC
#        define Z_REF_DAC PAPER_DRIVER_REF_DAC
#    endif
    // 上电给 Z/共脚 REF 写字电流；纸路动作时再按电机切换
    dacWrite((int)PAPER_DRIVER_REF_PIN, (int)Z_REF_DAC);
    grbl_msg_sendf(CLIENT_SERIAL,
                   MsgLevel::Info,
                   "[Paper] DAC REF initialized: GPIO%u Z=%u (0-255), clamp/panel/feeder = %u/%u/%u",
                   (unsigned)PAPER_DRIVER_REF_PIN,
                   (unsigned)Z_REF_DAC,
                   (unsigned)PAPER_REF_DAC_CLAMP,
                   (unsigned)PAPER_REF_DAC_PANEL,
                   (unsigned)PAPER_REF_DAC_FEEDER);
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
    paper_ensure_i2s_passthrough();
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
    // hutuji §9-B2′：Changing 供 S3 经 [ESP901] 跨 session 判换纸中（abort/断连分流）
    snprintf(buf,
             len,
             "Paper=%s MotorEn=%s PanelHold=%s Changing=%s",
             paper_ok ? "OK" : "No",
             en_ok ? "On" : "Off",
             paper_panel_low_hold_active ? "On" : "Off",
             paper_auto_change_is_running() ? "On" : "Off");
}

// 内部辅助函数：确保 I2S 处于 passthrough 模式（仅首次做长延时，避免主循环反复阻塞/锁存 595）
static void paper_ensure_i2s_passthrough(void) {
#ifdef USE_I2S_OUT
    if (!paper_i2s_setup) {
        i2s_out_set_passthrough();  // I2S已在Grbl.cpp全局初始化，这里仅设置passthrough模式
        paper_i2s_setup = true;
        delay(I2S_OUT_DELAY_MS * 2);
        i2s_out_delay();
    }
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
    paper_panel_low_hold_active = false;
    digitalWrite(PAPER_ENABLE_PIN, HIGH);
#ifdef PAPER_DRIVER_ENABLE_PIN
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, HIGH);
#endif
#ifdef PAPER_DRIVER_REF_PIN
#    ifndef Z_REF_DAC
#        define Z_REF_DAC PAPER_DRIVER_REF_DAC
#    endif
    // 纸路失能后恢复共脚 Z 电流，避免 REF 停在纸路电机值上
    paper_set_ref_dac((uint8_t)Z_REF_DAC);
#endif
}

// 纸路点动/使能前强制结束 hold（与写字期保持无关）
static void paper_force_release_panel_hold(const char* reason) {
    if (!paper_panel_low_hold_active) {
        return;
    }
    paper_disable_drivers();
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto] Panel hold released (%s)", reason);
}

// XYZ 运动即将开始：是否结束面板低电流保持。
// PAPER_PANEL_HOLD_DURING_WRITE=1：面板继续使能，但把共脚 REF 升到 Z_REF_DAC（写字 Z 扭矩）。
// 纸路点动/软复位仍走 paper_force_release_panel_hold。
void paper_release_panel_hold_for_xyz_motion(void) {
#if defined(PAPER_PANEL_HOLD_DURING_WRITE) && PAPER_PANEL_HOLD_DURING_WRITE
    if (!paper_panel_low_hold_active) {
        return;
    }
#    ifdef PAPER_DRIVER_REF_PIN
#        ifndef Z_REF_DAC
#            define Z_REF_DAC PAPER_DRIVER_REF_DAC
#        endif
    paper_set_ref_dac((uint8_t)Z_REF_DAC);
    static bool z_ref_logged = false;
    if (!z_ref_logged) {
        z_ref_logged = true;
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "[PaperAuto] Panel hold keep + Z REF=%u for write",
                       (unsigned)Z_REF_DAC);
    }
#    endif
#else
    paper_force_release_panel_hold("XYZ motion");
#endif
}

// 换纸成功：仅面板低电流保持；写字期默认继续保持（PAPER_PANEL_HOLD_DURING_WRITE）
static void paper_enter_panel_low_hold(void) {
    paper_ensure_i2s_passthrough();
    digitalWrite(PANEL_MOTOR_STEP_PIN, LOW);
    digitalWrite(PANEL_MOTOR_DIR_PIN, PANEL_DIR_FEED);
#ifdef USE_I2S_OUT
    i2s_out_delay();
#endif
#ifdef PAPER_DRIVER_REF_PIN
#    ifndef PAPER_REF_DAC_PANEL_HOLD
#        define PAPER_REF_DAC_PANEL_HOLD ((PAPER_REF_DAC_PANEL) / 2)
#    endif
    paper_set_ref_dac((uint8_t)PAPER_REF_DAC_PANEL_HOLD);
#endif
    digitalWrite(PAPER_ENABLE_PIN, LOW);
#ifdef PAPER_DRIVER_ENABLE_PIN
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, HIGH);  // 拾落/进纸器关
#endif
#ifdef USE_I2S_OUT
    i2s_out_delay();
#endif
    paper_panel_low_hold_active = true;
    grbl_msg_sendf(CLIENT_SERIAL,
                   MsgLevel::Info,
                   "[PaperAuto] Panel low-current hold (REF=%u, clamp/feeder off; during_write=%u)",
#ifdef PAPER_DRIVER_REF_PIN
                   (unsigned)PAPER_REF_DAC_PANEL_HOLD,
#else
                   0u,
#endif
#if defined(PAPER_PANEL_HOLD_DURING_WRITE) && PAPER_PANEL_HOLD_DURING_WRITE
                   1u
#else
                   0u
#endif
    );
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
            paper_refill_segment_buffer_during_blocking();
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
            paper_refill_segment_buffer_during_blocking();
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
    } else if (motor_ix == 1) {
        step_pin   = PANEL_MOTOR_STEP_PIN;
        motor_name = "Panel";
    } else if (motor_ix == 2) {
        step_pin   = FEEDER_MOTOR_STEP_PIN;
        motor_name = "Feeder";
    } else {
        return Error::GcodeUnsupportedCommand;
    }
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperMotor] %s: jog start (%u steps)", motor_name, (unsigned)steps);
    paper_force_release_panel_hold("paper motor jog");
    if (motor_ix == 0) {
        paper_enable_clamp_feeder_only();
#ifdef PAPER_DRIVER_REF_PIN
        paper_set_ref_dac(PAPER_REF_DAC_CLAMP);
#endif
    } else if (motor_ix == 1) {
        paper_enable_panel_only();
    } else {
        paper_enable_clamp_feeder_only();
    }
    paper_step_pulses(step_pin, steps);
    paper_disable_drivers();
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperMotor] %s: jog complete", motor_name);
    return Error::Ok;
}

// 仅使能换纸驱动（I2S passthrough + 拉低 EN），不动作；便于用 M64/M65 设方向后单独点动调试
void paper_enable_drivers_only(void) {
    paper_force_release_panel_hold("paper enable drivers");
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
        if ((i + 1) % PAPER_YIELD_STEPS == 0) {
            paper_refill_segment_buffer_during_blocking();
            delay(1);
        }
    }
#else
    for (uint32_t i = 0; i < steps; i++) {
        uint32_t hi_us = (i < PAPER_RAMP_STEPS) ? PAPER_EJECT_RAMP_HI_US : PAPER_EJECT_NORMAL_HI_US;
        uint32_t lo_us = (i < PAPER_RAMP_STEPS) ? PAPER_EJECT_RAMP_LO_US : PAPER_EJECT_NORMAL_LO_US;
        digitalWrite(PANEL_MOTOR_STEP_PIN, HIGH);
        delayMicroseconds(hi_us);
        digitalWrite(PANEL_MOTOR_STEP_PIN, LOW);
        delayMicroseconds(lo_us);
        if ((i + 1) % PAPER_YIELD_STEPS == 0) {
            paper_refill_segment_buffer_during_blocking();
            delay(1);
        }
    }
#endif
}
#endif

static Error paper_auto_change_abort_cleanup(const char* reason) {
    paper_auto_change_running      = false;
    paper_ignore_host_reset_until_ms = 0;
    paper_btn_arm_post_change_cooldown();
    st_go_idle();
    motors_set_disable(true);
    paper_disable_drivers();
    plan_reset();
    plan_sync_position();
    gc_sync_position();
    sys.state                      = State::Idle;
    sys_rt_exec_state.value        = 0;
    sys_rt_exec_state.bit.cycleStart = false;
    cycle_stop                     = false;
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto] Aborted: %s", reason);
    return Error::MessageFailed;
}

// 一键自动换纸流程（[ESP910] / M30 调用）
// 步骤：1 弹旧纸 → 2 进纸器找纸 → 3 松夹 → 4 面板+进纸器同速送纸 → 5 夹紧 → 6 面板快送直到脱传感器 → 7 回找传感器 → 8 最终对位 → 9 失能
// 结束时会发送 [PaperStatus] N（0=成功，2=进纸超时，3=第7步未找到传感器；1 保留）
Error paper_auto_change(void) {
    if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
        return Error::GcodeUnsupportedCommand;
    }

    // hutuji §9-E′（R20-GW-03）：入口原子认领，防两个任务并发驱动同一组换纸 GPIO。
    // 竞争者有二：clientCheckTask（[ESP910] → WebServer.cpp:469 system_execute_line）
    // 与 loopTask（M30 → user_m30 → 本函数），同优先级同核（SUPPORT_TASK_CORE）。
    // WebSettings.cpp:1050 的 running 预检是 fast-path advisory（负责 web 侧 "busy"
    // 文本应答），本处是唯一真正的互斥点，覆盖预检到入口之间的 check-then-act 窗口，
    // 并同时兜住无预检的 M30 / M721(:1226) / BT 连接(:1113) 三条路径。
    // 位置不能上移到 PAPER_DISABLED 早退之前：那条早退不清 running，认领会永久泄漏。
    // 用 std::atomic CAS 而非 portMUX 临界区：本仓同形先例是 Stepper.cpp:210 的
    // busy.compare_exchange_strong（Serial.cpp:66 的 portMUX 保护的是多字段缓冲区，
    // 范式不同）；且 running 另有 8 处跨任务读（:55/:59/:115/:362、Protocol.cpp:131/
    // :233、System.cpp:314、WebSettings.cpp:1050），临界区只护写侧、护不到它们，
    // atomic 让全部读写一并定义化，还不必在换纸全程（依赖步进 ISR）里关中断。
    // 只加互斥：零机械常量、零时序、零成功路径改动；失败者不清 running（归赢家所有）、
    // 不碰任何 GPIO/电机/状态字，也不发 [PaperStatus]——那是“换纸流程结束码”，
    // 认领失败时流程根本没开始，发终态码会让上位机误判本次换纸已跑完并失败。
    // 返回 Error::MessageFailed(90) 与本函数既有失败出口（abort_cleanup :708、
    // :841 缺纸/进纸超时、:962 卡纸）同码：M30 路径无论如何都被收敛成 error:90
    // （Custom/paper_system.cpp:182 user_m30 + GCode.cpp:1653/1766/1787），M721 与
    // [ESP910] 原样回传本码，WebSettings.cpp:1055 只在 Ok 时打 "done" 故仍能区分
    // “拒了/跑完”；不用 IdleError(8)，因为 §7 把 error:8 定义为“不算失败、上位机须
    // 重发该行”，会诱发 S3 重发换纸命令 → 双次换纸。
    bool expected = false;
    if (!paper_auto_change_running.compare_exchange_strong(expected, true)) {
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto] Busy: change already in progress");
        return Error::MessageFailed;
    }

    // 允许起始有纸：用于“开始队列前出旧纸”和“M30 后出本页再进下一页”。第 1 步会先弹旧纸，再进新纸。
    paper_btn_reset_press_state();
    paper_ignore_host_reset_until_ms = millis() + 8000u;
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
    if (sys.abort) {
        return paper_auto_change_abort_cleanup("host reset after eject");
    }
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-1] Done");

    // 2. 进纸器开始运动，直到纸张传感器检测到纸或达到上限
    // 仅进纸器工作（组B），面板失能（组A）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-2] Feeder searching for paper (max %u steps)...", (unsigned)FEEDER_FIND_STEPS_MAX);
    {
        uint32_t steps = 0;
        bool     found = false;
        bool     timeout_10s = false;
        uint32_t t0_ms = millis();
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
            if ((millis() - t0_ms) >= PAPER_SENSOR_TIMEOUT_MS) {
                timeout_10s = true;
                break;
            }
            paper_step_pulses_feeder_find(1);  // 找传感器阶段：加速一倍
            steps++;
            if (steps % PAPER_YIELD_STEPS == 0) {
                if (paper_blocking_abort_requested()) {
                    return paper_auto_change_abort_cleanup("host reset during feeder search");
                }
            }
        }
        if (sys.abort) {
            return paper_auto_change_abort_cleanup("host reset during feeder search");
        }
        if (!found) {
            paper_auto_change_running        = false;
            paper_ignore_host_reset_until_ms = 0;
            paper_btn_arm_post_change_cooldown();
            // 缺纸/进纸异常：立即停机并关闭驱动，等待下次从 Step1 重新开始
            st_go_idle();
            motors_set_disable(true);
            paper_disable_drivers();
            if (timeout_10s) {
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning,
                               "[PaperAuto-2] OUT_OF_PAPER: no paper detected within %u ms (steps=%u), stop and reset to Step1",
                               (unsigned)PAPER_SENSOR_TIMEOUT_MS, (unsigned)steps);
                grbl_msg_sendf(CLIENT_ALL, MsgLevel::Info, "[PaperStatus] %d", PAPER_STATUS_OUT_OF_PAPER);  // hutuji §9-B1′
            } else {
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning,
                               "[PaperAuto-2] ERROR: Feeder timeout - sensor not triggered after %u steps",
                               (unsigned)steps);
                grbl_msg_sendf(CLIENT_ALL, MsgLevel::Info, "[PaperStatus] %d", PAPER_STATUS_FEEDER_TIMEOUT);  // hutuji §9-B1′
            }
            return Error::MessageFailed;  // 返回非OK，避免上层误判“Auto paper change completed”
        }
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-2] Paper found at step %u", (unsigned)steps);
    }

    // 3. 传感器感应到纸后，松开拾落电机（面板+进纸器提前使能，拾落松开后面板不中断直接进入步骤4）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-3] Releasing clamp (%u steps)...", (unsigned)CLAMP_TOGGLE_STEPS);
    paper_enable_panel_and_feeder();  // 面板与进纸器先使能（含软启动，会清零DAC）
#ifdef PAPER_DRIVER_REF_PIN
    paper_set_ref_dac(PAPER_REF_DAC_CLAMP);  // 【Superpowers-主动控制】软启动后设置拾落电流
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info,
                   "[PaperMotor] Clamp: freq=%uHz, ref_voltage_mV=%u (DAC=%u)",
                   (unsigned)(1000000 / (PAPER_CLAMP_HI_US + PAPER_CLAMP_LO_US)),
                   (unsigned)(PAPER_REF_DAC_CLAMP * 3300 / 255),
                   (unsigned)PAPER_REF_DAC_CLAMP);
#endif
    paper_dir_steps(CLAMP_MOTOR_DIR_PIN, CLAMP_DIR_RELEASE, CLAMP_MOTOR_STEP_PIN, CLAMP_TOGGLE_STEPS);
    if (sys.abort) {
        return paper_auto_change_abort_cleanup("host reset after clamp release");
    }
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
    // 【Superpowers-状态可见】上报面板+进纸器同步参数
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info,
                   "[PaperMotor] Panel+Feeder sync: panel_dac=%u, feeder_dac=%u, freq=%uHz",
                   (unsigned)PAPER_REF_DAC_PANEL,
                   (unsigned)PAPER_REF_DAC_FEEDER,
                   (unsigned)(1000000 / (PAPER_NORMAL_HI_US + PAPER_NORMAL_LO_US)));
#endif
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-4] Panel+Feeder sync %.1fcm...", (float)PAPER_ADVANCE_CM_CLAMP_START);
    paper_step_pulses_panel_feeder_sync(steps_before_clamp);
    if (sys.abort) {
        return paper_auto_change_abort_cleanup("host reset during panel+feeder sync");
    }

    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-5] Clamping (%u steps, panel+feeder stopped)...", (unsigned)CLAMP_TOGGLE_STEPS);
#ifdef PAPER_DRIVER_REF_PIN
    paper_set_ref_dac(PAPER_REF_DAC_CLAMP);
    // 【Superpowers-预通知】夹紧操作前上报
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info,
                   "[PaperMotor] Clamp: freq=%uHz, ref_voltage_mV=%u (DAC=%u)",
                   (unsigned)(1000000 / (PAPER_CLAMP_HI_US + PAPER_CLAMP_LO_US)),
                   (unsigned)(PAPER_REF_DAC_CLAMP * 3300 / 255),
                   (unsigned)PAPER_REF_DAC_CLAMP);
#endif
    paper_dir_steps(CLAMP_MOTOR_DIR_PIN, CLAMP_DIR_CLAMP, CLAMP_MOTOR_STEP_PIN, CLAMP_TOGGLE_STEPS);
    if (sys.abort) {
        return paper_auto_change_abort_cleanup("host reset after clamp");
    }
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-5] Done");

    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-4b] Panel+Feeder sync again to %.1fcm total...", (float)PAPER_ADVANCE_CM);
#ifdef PAPER_DRIVER_REF_PIN
    paper_set_ref_dac((PAPER_REF_DAC_PANEL) > (PAPER_REF_DAC_FEEDER) ? (PAPER_REF_DAC_PANEL) : (PAPER_REF_DAC_FEEDER));
#endif
    paper_step_pulses_panel_feeder_sync(steps_after_clamp);
    if (sys.abort) {
        return paper_auto_change_abort_cleanup("host reset during panel+feeder sync (2)");
    }
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-4/5] Done (feeder stops at %.1fcm)", (float)PAPER_ADVANCE_CM);
    paper_enable_panel_only();

    // 6. 仅面板电机快速送纸，直到传感器“看不到纸”为止或达到上限（进纸器已在步骤5后失能）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-6] Panel fast feed until sensor loses paper (max %u steps)...", (unsigned)PANEL_FAST_STEPS_MAX);
    {
        uint32_t steps = 0;
        bool     jam_timeout = false;
        uint32_t t0_ms = millis();
        digitalWrite(PANEL_MOTOR_DIR_PIN, PANEL_DIR_FEED);
#ifdef USE_I2S_OUT
        i2s_out_delay();
        delay(2);
#endif
        while (paper_sensor_stable() && steps < PANEL_FAST_STEPS_MAX) {
            if ((millis() - t0_ms) >= PAPER_SENSOR_TIMEOUT_MS) {
                jam_timeout = true;
                break;
            }
            paper_one_step_panel_after_clamp(steps);  // 夹紧后面板进纸速度加倍
            steps++;
            if (steps % PAPER_YIELD_STEPS == 0) {
                if (paper_blocking_abort_requested()) {
                    return paper_auto_change_abort_cleanup("host reset during fast feed");
                }
            }
        }
        if (sys.abort) {
            return paper_auto_change_abort_cleanup("host reset during fast feed");
        }
        bool sensor_still_active = paper_sensor_stable();
        if (jam_timeout || sensor_still_active) {
            paper_auto_change_running        = false;
            paper_ignore_host_reset_until_ms = 0;
            paper_btn_arm_post_change_cooldown();
            // 卡纸：立即停机并关闭驱动，等待下次从 Step1 重新开始
            st_go_idle();
            motors_set_disable(true);
            paper_disable_drivers();
            if (jam_timeout) {
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning,
                               "[PaperAuto-6] JAM: sensor stayed active for %u ms (steps=%u), stop and reset to Step1",
                               (unsigned)PAPER_SENSOR_TIMEOUT_MS, (unsigned)steps);
            } else {
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning,
                               "[PaperAuto-6] JAM: sensor still active after max steps=%u (actual=%u), stop and reset to Step1",
                               (unsigned)PANEL_FAST_STEPS_MAX, (unsigned)steps);
            }
            grbl_msg_sendf(CLIENT_ALL, MsgLevel::Info, "[PaperStatus] %d", PAPER_STATUS_JAM_TIMEOUT);  // hutuji §9-B1′
            return Error::MessageFailed;
        }
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-6] Fast feed completed (%u steps, sensor=%s)", 
                       (unsigned)steps, sensor_still_active ? "STILL_ACTIVE" : "lost");
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
                if (paper_blocking_abort_requested()) {
                    return paper_auto_change_abort_cleanup("host reset during panel re-search");
                }
            }
        }
        if (sys.abort) {
            return paper_auto_change_abort_cleanup("host reset during panel re-search");
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
    if (sys.abort) {
        return paper_auto_change_abort_cleanup("host reset during final alignment");
    }
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-8] Done");

    // 9. settle → 面板低电流保持（防失能回退）；写字期是否保持见 PAPER_PANEL_HOLD_DURING_WRITE
#ifndef PANEL_FINAL_SETTLE_MS
#    define PANEL_FINAL_SETTLE_MS 200u
#endif
#ifndef PAPER_PANEL_HOLD_AFTER_CHANGE
#    define PAPER_PANEL_HOLD_AFTER_CHANGE 1
#endif
    if (PANEL_FINAL_SETTLE_MS > 0) {
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-8] Settle hold %u ms...", (unsigned)PANEL_FINAL_SETTLE_MS);
        delay(PANEL_FINAL_SETTLE_MS);
        if (sys.abort) {
            return paper_auto_change_abort_cleanup("during settle hold");
        }
    }
#if PAPER_PANEL_HOLD_AFTER_CHANGE
    paper_enter_panel_low_hold();
#else
    paper_disable_drivers();
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto] Paper drivers disabled (no panel hold)");
#endif

    paper_auto_change_running        = false;
    paper_ignore_host_reset_until_ms = 0;
    paper_btn_arm_post_change_cooldown();
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto] All steps completed successfully!");
    grbl_msg_sendf(CLIENT_ALL, MsgLevel::Info, "[PaperStatus] %d", step7_sensor_ok ? PAPER_STATUS_OK : PAPER_STATUS_SENSOR_NOT_FOUND);  // hutuji §9-B1′
    return Error::Ok;
}

void paper_on_soft_reset_restart(void) {
    // 仅取消已预约；保留 SPP 连上后的 after_ack_armed（连接后上位机常发 0x18）
    paper_bt_connect_auto_change_pending = false;
    // 软复位后全局状态已失效：必须结束面板低电流保持，避免带电残留
    if (paper_panel_low_hold_active) {
        paper_disable_drivers();
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto] Panel hold cleared on soft reset");
    }
}

void paper_bt_on_spp_connected(void) {
#if !defined(PAPER_AUTO_CHANGE_ON_BT_CONNECT) || !PAPER_AUTO_CHANGE_ON_BT_CONNECT
    return;
#endif
    paper_bt_connect_auto_change_pending = false;
    paper_bt_auto_change_after_ack_armed  = true;
}

void paper_bt_on_spp_disconnected(void) {
    paper_bt_connect_auto_change_pending = false;
    paper_bt_auto_change_after_ack_armed = false;
}

static bool paper_bt_schedule_auto_change_after_checks(void) {
#if !defined(PAPER_AUTO_CHANGE_ON_BT_CONNECT) || !PAPER_AUTO_CHANGE_ON_BT_CONNECT
    return false;
#endif
    if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
        return false;
    }
    if (paper_auto_change_is_running() || paper_bt_connect_auto_change_pending) {
        return false;
    }
    paper_bt_connect_auto_change_pending = true;
    grbl_msg_sendf(CLIENT_SERIAL,
                   MsgLevel::Info,
                   "[PaperBtConnect] Host ack done, auto-change scheduled (wait Idle)");
    return true;
}

void paper_bt_on_first_host_ack(void) {
#if !defined(PAPER_AUTO_CHANGE_ON_BT_CONNECT) || !PAPER_AUTO_CHANGE_ON_BT_CONNECT
    return;
#endif
    if (!paper_bt_auto_change_after_ack_armed) {
        return;
    }
    paper_bt_auto_change_after_ack_armed = false;
    if (!paper_bt_schedule_auto_change_after_checks()) {
        return;
    }
    // 应答已发出，立刻尝试执行（若已 Idle）
    paper_poll_bt_connect_auto_change();
}

void paper_poll_bt_connect_auto_change(void) {
#if !defined(PAPER_AUTO_CHANGE_ON_BT_CONNECT) || !PAPER_AUTO_CHANGE_ON_BT_CONNECT
    return;
#endif
    if (!paper_bt_connect_auto_change_pending) {
        return;
    }
    if (sys.state != State::Idle) {
        return;
    }
    if (paper_auto_change_is_running()) {
        return;
    }

    paper_bt_connect_auto_change_pending = false;
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperBtConnect] Running auto paper change...");
    Error e = paper_auto_change();
    if (e == Error::Ok) {
        sys_position[Z_AXIS] = 0;
        plan_sync_position();
        gc_sync_position();
        paper_mark_first_page_change_done();
        paper_btn_arm_post_change_cooldown();
        paper_btn_arm_bt_suppress();
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperBtConnect] Auto paper change completed.");
    } else {
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Warning,
                       "[PaperBtConnect] Auto paper change failed, error=%d",
                       (int)e);
    }
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
            paper_force_release_panel_hold("M71x jog");
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
            // 步数：0 表示使用 CLAMP_TOGGLE_STEPS（与自动换纸流程保持一致）
            uint16_t nsteps = (steps > 0 && steps <= 10000) ? steps : (uint16_t)CLAMP_TOGGLE_STEPS;
            // 方向逻辑：Q=0 为夹紧(clamp)，Q=1 为松开(release)；默认为 0(夹紧)
            bool do_clamp = (clamp_dir != 1);  // Q=0 或未提供 → 夹紧; Q=1 → 松开
            
            bool dir_level = do_clamp ? CLAMP_DIR_CLAMP : CLAMP_DIR_RELEASE;

            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info,
                           "M716: Clamp motor %s %u steps (Q=%d)",
                           do_clamp ? "CLAMP(Q0)" : "RELEASE(Q1)",
                           (unsigned)nsteps,
                           (int)clamp_dir);
            paper_force_release_panel_hold("M716 jog");
            paper_enable_drivers();
#ifdef PAPER_DRIVER_REF_PIN
            paper_set_ref_dac(PAPER_REF_DAC_CLAMP);  // 【Superpowers-主动控制】确保拾落电机使用优化后的电流
#endif
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
