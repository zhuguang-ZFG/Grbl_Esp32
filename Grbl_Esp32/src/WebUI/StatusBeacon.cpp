/*
  StatusBeacon.cpp - read-only UDP status beacon (§9-H′)

  广播面：UDP :2325 广播（255.255.255.255），1s 心跳 + 状态变化即发。
  内容 = `<state|Changing=On|Seq=N>`——state 用 report_state_text() 的现役
  语义（只读调用，不碰该函数本身），Changing 取 paper_auto_change_is_running()
  （奎享多页任务在页间换纸时 sys.state 是 Idle，观察者必须据此抑制「完成」
  误报）。Seq 为本次启动内发送序号，观察者据此区分新旧包。

  只读观测面纪律（§9-H′）：
  - 不占 Telnet 客户端槽（UDP 无连接），奎享占 :23/串口时照常广播；
  - 不改 report_realtime_status()/report_state_text()，不进任何运动/换纸路径；
  - 发送失败静默丢弃（诊断面，绝不反压主任务）；
  - 无重传面（UDP 丢了下一秒再来，观察者按超时判 stale）。
*/

#include "../Grbl.h"

#if defined(ENABLE_WIFI) && defined(ENABLE_STATUS_BEACON)

#    include "StatusBeacon.h"
#    include <WiFi.h>
#    include "../Report.h"

namespace WebUI {
    static WiFiUDP  beacon_udp;
    static uint32_t beacon_seq          = 0;   // 本次启动内发送序号
    static uint32_t beacon_last_send_ms = 0;
    static char     beacon_last_flags[32];     // `<state|Changing=xx` 缓存，去掉 |Seq
    static bool     beacon_running      = false;

    // UDP 端口避开 :23(Telnet)/:80(WebUI) 等现役面；2325 无现役占用。
    static constexpr uint16_t BEACON_PORT      = 2325;
    static constexpr uint32_t BEACON_PERIOD_MS = 1000;

    // 单独组一行状态，不复用 report_realtime_status()（固定 200B 缓冲 + 无边界
    // strcat，§9 明确否决扩字段）；这里 48 字节 snprintf 有界，格式是合法
    // Grbl 状态行子集，观察者沿用既有 `<Idle|...>` 解析路径。
    // 返回写入的旗标段（`<state|Changing=xx`），供变化检测比较。
    static size_t beacon_build_line(char* buf, size_t len, char* flags, size_t flags_len) {
        const char* state = report_state_text();
        if (state == nullptr) {
            state = "Unknown";
        }
        const char* changing = paper_auto_change_is_running() ? "On" : "Off";
        snprintf(buf, len, "<%s|Changing=%s|Seq=%lu>", state, changing,
                 static_cast<unsigned long>(beacon_seq));
        snprintf(flags, flags_len, "<%s|Changing=%s", state, changing);
        return strlen(buf);
    }

    static void beacon_send(const char* line, size_t len) {
        // 广播在本 LAN 内（STA/AP 模式都成立）；失败只丢包。
        beacon_udp.beginPacket(IPAddress(255, 255, 255, 255), BEACON_PORT);
        beacon_udp.write(reinterpret_cast<const uint8_t*>(line), len);
        beacon_udp.endPacket();
    }

    void StatusBeacon::begin() {
        if (beacon_running) {
            return;
        }
        if (beacon_udp.begin(BEACON_PORT) != 1) {
            grbl_sendf(CLIENT_ALL, "[MSG:Status beacon bind failed]\r\n");
            return;
        }
        beacon_seq          = 0;
        beacon_last_send_ms = millis() - BEACON_PERIOD_MS;  // begin 后首个 handle 即发
        beacon_last_flags[0] = 0;
        beacon_running = true;
        grbl_sendf(CLIENT_ALL, "[MSG:Status beacon on UDP :%u]\r\n", BEACON_PORT);
    }

    void StatusBeacon::end() {
        if (!beacon_running) {
            return;
        }
        beacon_udp.stop();
        beacon_running = false;
    }

    void StatusBeacon::handle() {
        if (!beacon_running) {
            return;
        }
        uint32_t now = millis();
        char     line[48];
        char     flags[sizeof(beacon_last_flags)];
        beacon_seq++;
        beacon_build_line(line, sizeof(line), flags, sizeof(flags));
        bool changed = strcmp(flags, beacon_last_flags) != 0;
        if (!changed && (now - beacon_last_send_ms) < BEACON_PERIOD_MS) {
            beacon_seq--;  // 未发送就不占用序号，观察者见到的 Seq 严格随发送递增
            return;
        }
        beacon_send(line, strlen(line));
        strncpy(beacon_last_flags, flags, sizeof(beacon_last_flags) - 1);
        beacon_last_flags[sizeof(beacon_last_flags) - 1] = 0;
        beacon_last_send_ms                              = now;
    }
}
#endif
