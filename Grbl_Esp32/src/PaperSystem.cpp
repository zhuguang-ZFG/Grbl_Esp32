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
    // 面板/进纸器：前若干步用较慢脉宽起步，之后切换到高速 150us；拾落电机保持 2ms
    const uint16_t ramp_steps      = 40u;    // 起步缓慢的步数（可按需要调整）
    const uint32_t ramp_hi_us      = 400u;   // 起步阶段脉宽（高电平时间）
    const uint32_t ramp_lo_us      = 400u;   // 起步阶段低电平时间
    const uint32_t normal_hi_us    = 150u;   // 面板/进纸器正常高速脉宽
    const uint32_t normal_lo_us    = 150u;
    const uint32_t clamp_hi_us     = 2000u;  // 拾落电机脉宽
    const uint32_t clamp_lo_us     = 2000u;

    uint32_t hi_us, lo_us;
    for (uint16_t i = 0; i < steps; i++) {
        if (step_pin == PANEL_MOTOR_STEP_PIN || step_pin == FEEDER_MOTOR_STEP_PIN) {
            // 起步阶段：用更大的脉宽，减小冲击
            if (i < ramp_steps) {
                hi_us = ramp_hi_us;
                lo_us = ramp_lo_us;
            } else {
                hi_us = normal_hi_us;
                lo_us = normal_lo_us;
            }
        } else {
            // 拾落电机保持原来的 2ms 脉宽
            hi_us = clamp_hi_us;
            lo_us = clamp_lo_us;
        }
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

// 标记I2S是否已初始化（延迟到第一次需要时）
static bool paper_i2s_setup = false;

void paper_system_init(void) {
#ifdef PAPER_DRIVER_REF_PIN
    // REF 接 GPIO25 (DAC)：输出参考电压，进纸器驱动电流由此处决定
    pinMode((int)PAPER_DRIVER_REF_PIN, OUTPUT);
    dacWrite((int)PAPER_DRIVER_REF_PIN, PAPER_DRIVER_REF_DAC);
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[Paper] DAC REF initialized: GPIO%u = %u (0-255)", 
                   (unsigned)PAPER_DRIVER_REF_PIN, (unsigned)PAPER_DRIVER_REF_DAC);
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
static void paper_disable_drivers(void) {
    digitalWrite(PAPER_ENABLE_PIN, HIGH);
#ifdef PAPER_DRIVER_ENABLE_PIN
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, HIGH);
#endif
}

// 仅使能面板电机（互斥：关闭拾落 + 进纸器）
static void paper_enable_panel_only(void) {
    paper_ensure_i2s_passthrough();
    // 面板使能
    digitalWrite(PAPER_ENABLE_PIN, LOW);
#ifdef PAPER_DRIVER_ENABLE_PIN
    // 拾落 + 进纸器失能
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, HIGH);
#endif
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperEn] panel_only: Q1=LOW, DRV_EN=HIGH");
}

// 仅使能拾落 + 进纸器（互斥：关闭面板）
static void paper_enable_clamp_feeder_only(void) {
    paper_ensure_i2s_passthrough();
    // 面板失能
    digitalWrite(PAPER_ENABLE_PIN, HIGH);
#ifdef PAPER_DRIVER_ENABLE_PIN
    // 拾落 + 进纸器使能
    digitalWrite(PAPER_DRIVER_ENABLE_PIN, LOW);
#endif
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperEn] clamp_feeder_only: Q1=HIGH, DRV_EN=LOW");
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

// 设定某个 DIR/STEP 电机以给定方向运动 N 步
static void paper_dir_steps(uint8_t dir_pin, bool dir_level, uint8_t step_pin, uint32_t steps) {
    digitalWrite(dir_pin, dir_level);
#ifdef USE_I2S_OUT
    i2s_out_delay();
    delay(2);
#endif
    paper_step_pulses(step_pin, (uint16_t)steps);
}

// 一键自动换纸流程
Error paper_auto_change(void) {
    if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
        return Error::GcodeUnsupportedCommand;
    }

    // 初始状态检查：应该是"无纸"状态才能开始弹出旧纸
    if (paper_sensor_stable()) {
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning, 
                       "[PaperAuto-ERROR] Paper still present - cannot eject old paper! Aborting...");
        return Error::Ok;
    }

    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto] Starting auto paper change...");

    // 1. 面板电机先运动，弹出旧纸（A4 长度 + 余量）
    // 仅面板电机工作（组A），拾落 + 进纸器失能（组B）
    paper_enable_panel_only();
    // 方向：按你的机械，出旧纸与进新纸同向运动 → 使用 PANEL_DIR_EJECT（等于 PANEL_DIR_FEED）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-1] Ejecting old paper (%u steps)...", (unsigned)PANEL_EJECT_STEPS);
    paper_dir_steps(PANEL_MOTOR_DIR_PIN, PANEL_DIR_EJECT, PANEL_MOTOR_STEP_PIN, PANEL_EJECT_STEPS);
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
            paper_step_pulses(FEEDER_MOTOR_STEP_PIN, 1);
            steps++;
        }
        if (!found) {
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning, "[PaperAuto-2] ERROR: Feeder timeout - sensor not triggered after %u steps", (unsigned)steps);
            return Error::Ok;  // Do not raise hard error, just warn
        }
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-2] Paper found at step %u", (unsigned)steps);
    }

    // 3. 传感器感应到纸后，松开拾落电机（仅拾落 + 进纸器组使能）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-3] Releasing clamp (%u steps)...", (unsigned)CLAMP_TOGGLE_STEPS);
    paper_dir_steps(CLAMP_MOTOR_DIR_PIN, CLAMP_DIR_RELEASE, CLAMP_MOTOR_STEP_PIN, CLAMP_TOGGLE_STEPS);
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-3] Done");

    // 4. 送纸器继续送入一段（约 5cm）（仍然只有拾落 + 进纸器组使能）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-4] Feeder extra advance (%u steps)...", (unsigned)FEEDER_EXTRA_STEPS);
    paper_dir_steps(FEEDER_MOTOR_DIR_PIN, FEEDER_DIR_FORWARD, FEEDER_MOTOR_STEP_PIN, FEEDER_EXTRA_STEPS);
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-4] Done");

    // 5. 再次压紧拾落电机（仍然只有拾落 + 进纸器组使能）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-5] Clamping paper (%u steps)...", (unsigned)CLAMP_TOGGLE_STEPS);
    paper_dir_steps(CLAMP_MOTOR_DIR_PIN, CLAMP_DIR_CLAMP, CLAMP_MOTOR_STEP_PIN, CLAMP_TOGGLE_STEPS);
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-5] Done");

    // 6. 面板电机 + 送纸器快速送纸，直到传感器“看不到纸”为止或达到上限
    // 仅面板电机工作（组A），拾落 + 进纸器失能（组B）以减小反拖阻力
    paper_enable_panel_only();
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-6] Fast feeding (panel+feeder 1:1) until sensor loses paper (max %u steps)...", (unsigned)PANEL_FAST_STEPS_MAX);
    {
        uint32_t steps = 0;
        digitalWrite(PANEL_MOTOR_DIR_PIN, PANEL_DIR_FEED);
#ifdef USE_I2S_OUT
        i2s_out_delay();
        delay(2);
#endif
        while (paper_sensor_stable() && steps < PANEL_FAST_STEPS_MAX) {
            // 仅由面板电机送料，进纸器在本阶段保持不动，避免拖拽影响和带入第二张纸
            paper_step_pulses(PANEL_MOTOR_STEP_PIN, 1);
            steps++;
        }
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-6] Fast feed completed (%u steps, sensor=%s)", 
                       (unsigned)steps, paper_sensor_stable() ? "STILL_ACTIVE" : "lost");
    }

    // 7. 面板电机反向，直到再次“感应到纸”或达到上限（回找定位点）
    // 仅面板电机工作（组A）
    paper_enable_panel_only();
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-7] Panel reverse to find paper again (max %u steps)...", (unsigned)PANEL_BACK_STEPS_MAX);
    {
        uint32_t steps = 0;
        // 第7步专用反向方向：PANEL_DIR_REVERSE，不再与 EJECT/FEED 共用，避免相互影响
        digitalWrite(PANEL_MOTOR_DIR_PIN, PANEL_DIR_REVERSE);
#ifdef USE_I2S_OUT
        i2s_out_delay();
        delay(2);
#endif
        while (!paper_sensor_stable() && steps < PANEL_BACK_STEPS_MAX) {  // 改用防抖读取
            paper_step_pulses(PANEL_MOTOR_STEP_PIN, 1);
            steps++;
        }
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-7] Panel reverse completed (%u steps, sensor=%s)", 
                       (unsigned)steps, paper_sensor_stable() ? "found" : "NOT_found");
        
        // 验证第7步是否成功找到纸（关键检查点）
        if (!paper_sensor_stable()) {
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Warning, 
                           "[PaperAuto-7-WARNING] Sensor NOT stable after reverse search - paper may not be in correct position!");
        }
    }

    // 8. 面板电机再向送纸方向走固定步数，作为最终对位（仍然只有面板电机使能）
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-8] Final alignment (%u steps)...", (unsigned)PANEL_FINAL_STEPS);
    paper_dir_steps(PANEL_MOTOR_DIR_PIN, PANEL_DIR_FEED, PANEL_MOTOR_STEP_PIN, PANEL_FINAL_STEPS);
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto-8] Done");

    // 9. 换纸流程完成后，关闭换纸相关电机使能，防止长时间发热
    paper_disable_drivers();

    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperAuto] All steps completed successfully!");
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
        default:
            if (PAPER_SENSOR_PIN == PAPER_DISABLED) {
                return Error::GcodeUnsupportedCommand;
            }
            return Error::GcodeUnsupportedCommand;
    }
}
#endif
