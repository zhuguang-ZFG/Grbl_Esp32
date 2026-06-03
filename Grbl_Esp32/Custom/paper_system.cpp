// paper_system.cpp
// Custom code for paper-handling system (paper sensor + paper-change motors)
// Enabled via CUSTOM_CODE_FILENAME in custom_3axis_hr4988.h
//
// 注意：本文件通过 CustomCode.cpp 间接包含，那里已包含 Grbl.h，
// 这里不需要再次 include。

static void license_load_from_nvs(void);  // 前向声明，供 machine_init 使用
static bool     paper_change_last_ok     = true;   // 最近一次 user_m30 触发的换纸结果（true=成功）
static uint32_t paper_change_cooldown_ms = 0xFFFFFFFF;  // Sentinel: 开机未换纸时不启用冷却

// 机器初始化钩子：在 Grbl 启动时调用（弱符号在 Grbl.cpp 中，这里覆盖）
// 打印芯片 ID（用于向厂商申请授权码），并从 NVS 恢复授权状态。
void machine_init() {
    uint64_t chipid = ESP.getEfuseMac();  // Arduino-ESP32 提供的芯片唯一 ID（基于 MAC）
    uint32_t id_high = (uint32_t)(chipid >> 32);
    uint32_t id_low  = (uint32_t)(chipid & 0xFFFFFFFFULL);

    grbl_msg_sendf(CLIENT_SERIAL,
                   MsgLevel::Info,
                   "[ESP32ID] ChipID (efuse MAC) = %08X%08X",
                   (unsigned)id_high,
                   (unsigned)id_low);

    license_load_from_nvs();  // 从片内 NVS（Flash）恢复授权，重启后仍有效
    // 不再打印期望授权码，用户须持 ChipID 向厂商索取 M800 P<授权码>，厂商用相同密钥生成
}

// === 授权与自定义 M 指令（带密钥，才算加密） ===
//
// M800 P<十进制授权码>：由 GCode.cpp 取 P 后调用 license_set_from_p_param(P)。
// 授权码 = F(ChipID, 密钥)；固件只校验不泄露期望值，用户须持 ChipID 向厂商索取授权码。
//
// 密钥：仅固件与厂商授权工具一致即可，编译后不可见；泄露则需更换密钥并重发固件/工具。
#define LICENSE_SECRET_KEY_H 0x8B3C9A1Fu  // 高 32 位密钥，可自行修改
#define LICENSE_SECRET_KEY_L 0xE72F4D06u  // 低 32 位密钥，可自行修改
//
// 授权码算法（伪代码，固件与厂商生成器须完全一致）：
//
// 输入:
//   ChipID_hi  = 芯片 ID 高 32 位（串口 [ESP32ID] ChipID = XXXXXXXX YYYYYYYY 的前 8 位十六进制）
//   ChipID_lo  = 芯片 ID 低 32 位（后 8 位十六进制）
//   KEY_H      = LICENSE_SECRET_KEY_H  // 0x8B3C9A1Fu
//   KEY_L      = LICENSE_SECRET_KEY_L  // 0xE72F4D06u
//
// 所有运算为 32 位无符号，溢出自然截断。
//
// 伪代码:
//
//   FUNCTION license_code( ChipID_hi, ChipID_lo, KEY_H, KEY_L ) -> U32
//
//     mix := ChipID_hi XOR ChipID_lo XOR KEY_H XOR KEY_L
//
//     mix := ROTATE_LEFT( mix, 7 )   // 即 (mix << 7) OR (mix >> 25)，仅低 32 位
//
//     mix := mix XOR KEY_H
//
//     rot_L := ROTATE_LEFT( KEY_L, 13 )   // (KEY_L << 13) OR (KEY_L >> 19)
//     mix   := mix XOR rot_L
//
//     RETURN mix   // 十进制即 M800 P 的数值，如 284719382
//
//   END
//
// 厂商端：读用户提供的 ChipID_hi / ChipID_lo，用同一 KEY_H/KEY_L 调用上述函数，
// 将返回值（十进制）作为授权码发给用户，用户发送 M800 P<返回值>。
//
// 用法：
//   1. 设备上电后串口会打印 [ESP32ID] ChipID = ...，用户把该 ChipID 发给厂商。
//   2. 厂商用**同一密钥**和相同算法在 PC 上算出授权码，把 M800 P<授权码> 发给用户。
//   3. 用户在设备串口发送该 M800 行，匹配则 [License] OK 并写入 NVS，重启仍有效。
//   4. 不匹配仅提示 [License] INVALID，不打印期望值，避免被穷举或仿造。

static bool     license_ok = false;
static uint32_t license_expected_code();  // 前向声明

#define LICENSE_NVS_NAMESPACE "license"
#define LICENSE_NVS_KEY_CODE  "code"

static void license_load_from_nvs() {
    Preferences prefs;
    if (!prefs.begin(LICENSE_NVS_NAMESPACE, true))
        return;
    uint32_t stored = prefs.getULong(LICENSE_NVS_KEY_CODE, 0);
    prefs.end();
    // 每次启动都用当前芯片 MAC 重算期望码比对，克隆芯片无法通过
    license_ok = (stored != 0 && stored == license_expected_code());
}

static void license_save_to_nvs() {
    Preferences prefs;
    if (!prefs.begin(LICENSE_NVS_NAMESPACE, false))
        return;
    prefs.putULong(LICENSE_NVS_KEY_CODE, license_ok ? license_expected_code() : 0);
    prefs.end();
}

static uint32_t license_expected_code()
{
    uint64_t chipid = ESP.getEfuseMac();
    uint32_t h      = (uint32_t)(chipid >> 32);
    uint32_t l      = (uint32_t)(chipid & 0xFFFFFFFFu);
    uint32_t mix    = h ^ l ^ LICENSE_SECRET_KEY_H ^ LICENSE_SECRET_KEY_L;
    mix             = (mix << 7) | (mix >> (32 - 7));
    mix            ^= LICENSE_SECRET_KEY_H;
    mix            ^= (LICENSE_SECRET_KEY_L << 13) | (LICENSE_SECRET_KEY_L >> 19);
    return mix;
}

// 覆盖 Grbl.cpp 中的弱符号，用于在核心代码中检查授权
bool check_license()
{
    return license_ok;
}

bool paper_last_change_ok() {
    return paper_change_last_ok;
}

// M800 P<十进制授权码>：GCode 解析层在 STEP 3 取 P 后调用，比较通过则授权并写入 NVS
bool license_set_from_p_param(uint32_t p_value)
{
    uint32_t expect = license_expected_code();
    if (p_value == expect) {
        license_ok = true;
        license_save_to_nvs();
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[License] OK for this chip (saved to NVS)");
        return true;
    }
    license_ok = false;
    grbl_msg_sendf(CLIENT_SERIAL,
                   MsgLevel::Warning,
                   "[License] INVALID (wrong code, get license from vendor with ChipID)");
    return false;
}

// Custom M-code handler
// M701/M711/M712/M713/M716 等纸张系统命令由 PaperSystem.cpp 的 paper_system_mcode() 统一处理
// M800 在 GCode.cpp 中作为 pending 处理并调用 license_set_from_p_param，不会进入此处
Error user_m_code(uint16_t code) {
    // 其他自定义 M 指令仍交给纸张系统处理（参见 PaperSystem.cpp）
    return Error::GcodeUnsupportedCommand;
}

// 当 GCode 中遇到 M30（当前页 G 代码文件结束）时，GCode.cpp 会在
// program_flow 处理完毕、缓冲区同步后调用 user_m30()。
// 这里把“换纸 = 换页”接进来：每次 M30 结束自动执行一套换纸流程。
void user_m30() {
    paper_change_last_ok = false;  // 默认失败，只有完整成功后置 true
    // 必须在 Idle 状态、且纸张系统已配置时才自动换纸，避免在报警/检查模式下误动作。
    if (sys.state != State::Idle) {
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "[PaperM30] Skipped auto-change: system not idle (state=%d)",
                       (int)sys.state);
        return;
    }
    // 这里只做一个简单的“纸路是否配置”检查：默认未配置时 PAPER_SENSOR_PIN=255（见 Config.h）
    if (PAPER_SENSOR_PIN == 255) {
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "[PaperM30] Skipped: paper system not configured");
        return;
    }

    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperM30] End of page (M30) detected, starting auto paper change...");
    Error e = paper_auto_change();
    if (e == Error::Ok) {
        // 仅在换纸成功后设置冷却（失败时允许立即按键重试）
        paper_change_cooldown_ms = millis();
        // 换纸完成后机械上 Z 已在抬笔极限（原点），将系统 Z 设为 0，避免下一页首条指令再让 Z 往“上”走
        sys_position[Z_AXIS] = 0;
        plan_sync_position();
        gc_sync_position();
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperM30] Auto paper change completed.");
        paper_change_last_ok = true;
    } else {
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Warning,
                       "[PaperM30] Auto paper change returned error=%d",
                       (int)e);
    }
}

// 一键换纸物理按键（GPIO35 / Macro0）：单次有效按下即入队 [ESP910]。
// GPIO35 仅有内部弱上拉，电机 EMI 可能拉低引脚：短采样去抖 + 500ms 节流 + 触发后 15s 按键冷却。
#define PAPER_BTN_DEBOUNCE_SAMPLES 5
#define PAPER_BTN_DEBOUNCE_MS      8
#define PAPER_BTN_THROTTLE_MS      500
#define PAPER_BTN_REARM_MS         15000  // 按键触发后屏蔽重复触发（换纸电机 EMI）

void user_defined_macro(uint8_t index) {
    if (index != 0) {
        return;
    }

    // controlCheckTask 在双沿都会调用；仅处理按下（LOW）
    if (digitalRead(PAPER_CHANGE_BTN_PIN) != LOW) {
        return;
    }

    static uint32_t last_accept_ms   = 0;
    static uint32_t last_trigger_ms  = 0;

    const uint32_t now = millis();
    if (now - last_accept_ms < PAPER_BTN_THROTTLE_MS) {
        return;
    }
    if (last_trigger_ms != 0 && (now - last_trigger_ms) < PAPER_BTN_REARM_MS) {
        return;
    }

    for (int i = 0; i < PAPER_BTN_DEBOUNCE_SAMPLES; i++) {
        if (i > 0) {
            delay(PAPER_BTN_DEBOUNCE_MS);
        }
        if (digitalRead(PAPER_CHANGE_BTN_PIN) != LOW) {
            return;
        }
    }
    last_accept_ms = now;

    if (sys.state != State::Idle) {
        return;
    }
    if (paper_auto_change_is_running()) {
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperBtn] Ignored: paper change already running");
        return;
    }

    last_trigger_ms = now;
    grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperBtn] Triggered, queuing [ESP910]...");

    char line[16];
    strcpy(line, "[ESP910]\r");
    WebUI::inputBuffer.push(line);

    strcpy(line, "M902\r");
    WebUI::inputBuffer.push(line);
}
