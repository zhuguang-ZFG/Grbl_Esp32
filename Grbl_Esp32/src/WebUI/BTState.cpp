/*
  BTState.cpp - Bluetooth SPP connection state machine

  Copyright (c) 2014 Luc Lebosse. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "../Grbl.h"

#ifdef ENABLE_BLUETOOTH

#    include <atomic>

#    include "BTState.h"
#    include "BTConfig.h"
#    include "../Serial.h"
#    include "../Planner.h"

// Connection state is written by the Bluetooth SPP callback task and read by the main loop.
static std::atomic<BTState>  bt_state{BTState::Idle};
static std::atomic<uint32_t> bt_last_event_ms{0};

// Soft-drop bookkeeping (no end()/begin() recovery — that path IWDT-panics).
static uint32_t             bt_recovery_ts = 0;
static std::atomic<uint8_t> bt_recovery_attempts{0};

// Planner 饥饿阈值：与 Protocol.cpp 中的 PLANNER_STARVE_THRESHOLD 一致。
// 当运动中 planner 排队块低于此值时，跳过所有可能阻塞的 BT TX 操作，
// 避免 SerialBT.write() 的 xQueueSend 1000ms 超时导致段缓冲欠载。
static const uint8_t BT_PLANNER_STARVE_THRESHOLD = 8;

static bool bt_planner_is_starving(void) {
    // 饥饿 = 排队块少。plan_get_block_buffer_available() 返回空闲槽数（(SIZE-1)-排队），
    // 语义相反；必须用 plan_get_block_buffer_count()（真实排队块数）。
    return sys.state == State::Cycle && plan_get_block_buffer_count() < BT_PLANNER_STARVE_THRESHOLD;
}

// TX ring buffer for congested-but-critical messages. Protected by bt_tx_mux.
static uint8_t      bt_tx_ring_storage[BT_TX_RING_SIZE];
static BTTxRing     bt_tx_ring(bt_tx_ring_storage, BT_TX_RING_SIZE);
// BTTxRing owns the reset generation used to reject stale flush advances.
static portMUX_TYPE bt_tx_mux       = portMUX_INITIALIZER_UNLOCKED;

// Forward declaration — bt_state_on_event (SPP callback) needs to reset
// the ring on disconnect, but the helper is defined later in the file.
static void bt_tx_ring_reset(void);

void bt_state_init(void) {
    bt_state.store(BTState::Idle);
    bt_last_event_ms.store(0);
    bt_recovery_ts = 0;
    bt_recovery_attempts.store(0);
    bt_tx_ring.initialize();
}

BTState bt_state_get(void) {
    return bt_state.load();
}

static void bt_state_set(BTState new_state) {
    bt_state.store(new_state);
}

void bt_state_on_event(esp_spp_cb_event_t event, esp_spp_cb_param_t* param) {
    bt_last_event_ms.store(millis());
    switch (event) {
        case ESP_SPP_INIT_EVT:
        case ESP_SPP_START_EVT:
            bt_state_set(bt_state_reduce(bt_state_get(), BTLinkEvent::Started));
            break;

#    ifdef ESP_SPP_UNINIT_EVT
        case ESP_SPP_UNINIT_EVT:
#    endif
#    ifdef ESP_SPP_SRV_STOP_EVT
        case ESP_SPP_SRV_STOP_EVT:
#    endif
#    if defined(ESP_SPP_UNINIT_EVT) || defined(ESP_SPP_SRV_STOP_EVT)
            bt_state_set(bt_state_reduce(bt_state_get(), BTLinkEvent::Stopped));
            break;
#    endif

        case ESP_SPP_SRV_OPEN_EVT: {
            // 连接建立：清空旧缓冲，避免重连后执行半条旧指令
            client_reset_read_buffer(CLIENT_BT);
            bt_state_set(bt_state_reduce(bt_state_get(), BTLinkEvent::Opened));
            bt_recovery_attempts.store(0);  // 成功连接后重置恢复计数
#    if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
            paper_btn_arm_bt_suppress();
            paper_bt_on_spp_connected();
#    endif
        } break;

        case ESP_SPP_CLOSE_EVT:
#    if defined(GRBL_PAPER_SYSTEM) && GRBL_PAPER_SYSTEM
            paper_bt_on_spp_disconnected();
#    endif
            // 断开：清空读写缓冲，防止重连后旧数据污染新连接
            client_reset_read_buffer(CLIENT_BT);
            vTaskEnterCritical(&bt_tx_mux);
            bt_tx_ring_reset();
            vTaskExitCritical(&bt_tx_mux);
            bt_state_set(bt_state_reduce(bt_state_get(), BTLinkEvent::Closed));
            break;

        case ESP_SPP_CONG_EVT:
            bt_state_set(bt_state_reduce(bt_state_get(), BTLinkEvent::CongestionChanged, param->cong.cong));
            break;

        case ESP_SPP_WRITE_EVT:
            bt_state_set(bt_state_reduce(bt_state_get(), BTLinkEvent::WriteCompleted, param->write.cong));
            break;

        case ESP_SPP_DATA_IND_EVT: {
            bt_state_set(bt_state_reduce(bt_state_get(), BTLinkEvent::DataReceived));
        } break;

        default:
            break;
    }
}

// 判断 BT 消息是否关键：ok/error、状态报告、报警、普通 MSG 都是关键；
// 调试/诊断类 MSG 在拥塞时可丢弃，避免阻塞控制面。
bool bt_tx_message_is_critical(const char* text) {
    return bt_tx_message_is_critical_core(text);
}

static bool bt_tx_ring_push(const char* data, size_t len) {
    return bt_tx_ring.push(data, len);
}

static void bt_tx_ring_reset(void) {
    bt_tx_ring.reset();
}

bool bt_tx_send(const char* text, size_t len, bool critical) {
    if (text == nullptr) {
        return false;
    }
    if (len == 0) {
        return true;
    }

    if (!WebUI::SerialBT.hasClient()) {
        return false;
    }

    BTState s = bt_state_get();
    if (s == BTState::Idle || s == BTState::Advertising || s == BTState::Recovering) {
        return false;
    }

    if (s == BTState::Connected) {
        // 如果 ring buffer 已有积压数据，新消息必须排在后面，避免直接写入
        // 的字节先于积压数据到达对端，导致消息交错（例如 "ok" 插入到
        // 一条被截断的 error 报文中间）。
        vTaskEnterCritical(&bt_tx_mux);
        bool ring_has_data = (bt_tx_ring.used() > 0);
        vTaskExitCritical(&bt_tx_mux);

        if (ring_has_data) {
            // 积压未清：关键消息入环保持 FIFO 顺序，非关键消息丢弃
            if (critical) {
                vTaskEnterCritical(&bt_tx_mux);
                bool ok = bt_tx_ring_push(text, len);
                vTaskExitCritical(&bt_tx_mux);
                return ok;
            }
            return false;
        }

        // 运动中 planner 饥饿时丢弃非关键消息（调试/诊断 MSG），减少 BT 负载。
        // 关键消息（ok/error/状态）绝不能因饥饿被抑制：它们是流式协议的流控 ACK，
        // 上位机收到 ok 才会发下一行 G 代码来补给 planner。若饥饿时把 ok 压进环、
        // 再被 flush 的饥饿门禁挡住，就会形成「饥饿→抑制 ok→上位机停发→更饥饿」
        // 的死锁（表现为写不了字）。因此关键消息落到下面的直写路径，尽快发出。
        if (bt_planner_is_starving() && !critical) {
            return false;
        }

        size_t written = WebUI::SerialBT.write((const uint8_t*)text, len);
        if (written == len) {
            return true;
        }
        // 底层队列已满：关键消息入环，非关键消息丢弃
        if (critical && written < len) {
            vTaskEnterCritical(&bt_tx_mux);
            bool ok = bt_tx_ring_push(text + written, len - written);
            vTaskExitCritical(&bt_tx_mux);
            return ok;
        }
        return false;
    }

    // Congested：只缓存关键消息
    if (critical) {
        vTaskEnterCritical(&bt_tx_mux);
        bool ok = bt_tx_ring_push(text, len);
        vTaskExitCritical(&bt_tx_mux);
        return ok;
    }
    return false;
}

void bt_tx_flush(void) {
    if (!WebUI::SerialBT.hasClient()) {
        return;
    }
    // 注意：不能因 planner 饥饿而跳过 flush。ring 里只装关键消息（push 仅在
    // critical 时调用），其中包括流控 ACK（ok/error）。饥饿期恰恰是写字最密集、
    // 上位机最需要 ok 来续发 G 代码的时候；跳过 flush 会让 ok 卡死在环里，
    // 形成「饥饿→ok 不出→上位机停发→更饥饿」的死锁。flush 只在 Connected
    // （非 Congested）态被调用，此时底层写队列有空间、SerialBT.write() 不阻塞。

    while (true) {
        vTaskEnterCritical(&bt_tx_mux);
        if (bt_tx_ring.used() == 0) {
            vTaskExitCritical(&bt_tx_mux);
            break;
        }

        char    chunk[64];
        uint32_t gen_before = 0;
        size_t   contiguous = bt_tx_ring.copy_contiguous((uint8_t*)chunk, sizeof(chunk), &gen_before);
        vTaskExitCritical(&bt_tx_mux);

        size_t written = WebUI::SerialBT.write((const uint8_t*)chunk, contiguous);
        if (written == 0) {
            break;
        }

        vTaskEnterCritical(&bt_tx_mux);
        if (!bt_tx_ring.advance(written, gen_before)) {
            // 期间 ring 被 reset（链路断开/重连）——本次 write 的字节已被新
            // 连接的 SRV_OPEN 流程接管或丢弃，绝不能动 tail/used，否则 used 下溢。
            vTaskExitCritical(&bt_tx_mux);
            break;
        }
        vTaskExitCritical(&bt_tx_mux);

        if (written < contiguous) {
            break;
        }
    }
}

// 假连接自救：只 disconnect()/降级状态，绝不 SerialBT.end()/begin()。
// 实测 end→begin 会触发 rwbt.c ASSERT_PARAM + Interrupt WDT，整机重启后 PC 仍占着死 COM，
// 上位机继续发 G92 却收不到 ok（双口日志 22:38–22:40）。
static void bt_soft_drop_stale_link(uint32_t now_ms, uint32_t silent_ms, const char* reason) {
    if (bt_recovery_attempts.load() >= BT_RECOVERY_MAX_RETRIES) {
        bt_state_set(BTState::Advertising);
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "[BTState] Soft-drop skipped: max retries (%u) exceeded (%s)",
                       BT_RECOVERY_MAX_RETRIES,
                       reason);
        return;
    }
    if (bt_recovery_ts != 0 && (now_ms - bt_recovery_ts) < BT_RECOVERY_COOLDOWN_MS) {
        return;
    }
    bt_recovery_ts = now_ms;
    bt_recovery_attempts.fetch_add(1);

    vTaskEnterCritical(&bt_tx_mux);
    bt_tx_ring_reset();
    vTaskExitCritical(&bt_tx_mux);

    if (WebUI::SerialBT.hasClient()) {
        WebUI::SerialBT.disconnect();
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "[BTState] Soft-drop stale link (%s, silent=%u ms): disconnect()",
                       reason,
                       silent_ms);
        // CLOSE_EVT 会清缓冲并置 Advertising；勿在此 end()/begin()
    } else {
        client_reset_read_buffer(CLIENT_BT);
        bt_state_set(BTState::Advertising);
        grbl_msg_sendf(CLIENT_SERIAL,
                       MsgLevel::Info,
                       "[BTState] Soft-drop stale link (%s, silent=%u ms): demote (no client)",
                       reason,
                       silent_ms);
    }
}

void bt_state_update(void) {
    uint32_t now_ms = millis();
    BTState  s      = bt_state_get();

    // 假连接自救：仅 Congested 或 TX ring 有积压且长时间无 SPP 事件 → soft-drop。
    // 纯 Connected + 无待发（上位机停发/? 变稀）不拆——对齐 d26b4d05「能写」行为；
    // 误拆后 PC 常仍占着死 COM，G92 永远无 ok（双口 23:19 COM5）。
    // 换纸中也跳过。禁止 end()/begin()（IWDT）。
    if (s == BTState::Connected || s == BTState::Congested) {
        uint32_t last      = bt_last_event_ms.load();
        uint32_t silent_ms = (now_ms >= last) ? (now_ms - last) : 0u;
        if (silent_ms > BT_LINK_SILENCE_TIMEOUT_MS && !paper_auto_change_is_running()) {
            vTaskEnterCritical(&bt_tx_mux);
            bool tx_pending = (bt_tx_ring.used() > 0);
            vTaskExitCritical(&bt_tx_mux);
            const bool stale = (s == BTState::Congested) || tx_pending;
            if (stale) {
                const char* reason = (s == BTState::Congested) ? "congested_silent" : "tx_pending_silent";
                bt_soft_drop_stale_link(now_ms, silent_ms, reason);
            }
        }
    }

    // 拥塞解除后刷出缓存的关键消息
    if (bt_state_get() == BTState::Connected) {
        vTaskEnterCritical(&bt_tx_mux);
        bool need_flush = (bt_tx_ring.used() > 0);
        vTaskExitCritical(&bt_tx_mux);
        if (need_flush) {
            bt_tx_flush();
        }
    }
}

bool bt_state_is_connected(void) {
    BTState s = bt_state_get();
    return s == BTState::Connected || s == BTState::Congested;
}

bool bt_state_can_tx(void) {
    BTState s = bt_state_get();
    return s == BTState::Connected;
}

uint32_t bt_state_last_activity_ms(void) {
    return bt_last_event_ms.load();
}

#endif
