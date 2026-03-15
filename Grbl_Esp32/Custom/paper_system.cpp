// paper_system.cpp
// Custom code for paper-handling system (paper sensor + paper-change motors)
// Enabled via CUSTOM_CODE_FILENAME in custom_3axis_hr4988.h
//
// 注意：本文件通过 CustomCode.cpp 间接包含，那里已包含 Grbl.h，
// 这里不需要再次 include。

static void license_load_from_nvs(void);  // 前向声明，供 machine_init 使用

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
#define LICENSE_NVS_KEY_OK    "ok"

static void license_load_from_nvs() {
    Preferences prefs;
    if (!prefs.begin(LICENSE_NVS_NAMESPACE, true))  // true = 只读
        return;
    license_ok = (prefs.getUChar(LICENSE_NVS_KEY_OK, 0) != 0);
    prefs.end();
}

static void license_save_to_nvs() {
    Preferences prefs;
    if (!prefs.begin(LICENSE_NVS_NAMESPACE, false))  // false = 读写
        return;
    prefs.putUChar(LICENSE_NVS_KEY_OK, license_ok ? 1 : 0);
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
        // 换纸完成后机械上 Z 已在抬笔极限（原点），将系统 Z 设为 0，避免下一页首条指令再让 Z 往“上”走
        sys_position[Z_AXIS] = 0;
        plan_sync_position();
        gc_sync_position();
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
    // 换纸流程已在进行时不再排队，避免重复执行
    if (paper_auto_change_is_running()) {
        grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[PaperBtn] Ignored: paper change already running");
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
