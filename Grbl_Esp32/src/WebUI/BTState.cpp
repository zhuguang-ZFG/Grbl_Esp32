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

// Recovery state, driven by bt_state_update() in the main loop.
static bool                 bt_recovery_active   = false;
static uint8_t              bt_recovery_step     = 0;
static uint32_t             bt_recovery_ts       = 0;
static std::atomic<uint8_t> bt_recovery_attempts{0};

// Planner 饥饿阈值：与 Protocol.cpp 中的 PLANNER_STARVE_THRESHOLD 一致。
// 当运动中 planner 可用块低于此值时，跳过所有可能阻塞的 BT TX 操作，
// 避免 SerialBT.write() 的 xQueueSend 1000ms 超时导致段缓冲欠载。
static const uint8_t BT_PLANNER_STARVE_THRESHOLD = 8;

static bool bt_planner_is_starving(void) {
    return sys.state == State::Cycle && plan_get_block_buffer_available() < BT_PLANNER_STARVE_THRESHOLD;
}

// TX ring buffer for congested-but-critical messages. Protected by bt_tx_mux.
static uint8_t      bt_tx_ring[BT_TX_RING_SIZE];
static size_t       bt_tx_ring_head = 0;
static size_t       bt_tx_ring_tail = 0;
static size_t       bt_tx_ring_used = 0;
// Generation counter: bumped on every reset. bt_tx_flush captures it before
// releasing the lock to call SerialBT.write(); if a SPP CLOSE/reset happens
// concurrently and resets the ring, the generation mismatch tells flush to
// drop its stale advance instead of underflowing bt_tx_ring_used (size_t).
static uint32_t     bt_tx_ring_gen  = 0;
static portMUX_TYPE bt_tx_mux       = portMUX_INITIALIZER_UNLOCKED;

// Forward declaration — bt_state_on_event (SPP callback) needs to reset
// the ring on disconnect, but the helper is defined later in the file.
static void bt_tx_ring_reset(void);

void bt_state_init(void) {
    bt_state.store(BTState::Idle);
    bt_last_event_ms.store(0);
    bt_recovery_active   = false;
    bt_recovery_step     = 0;
    bt_recovery_ts       = 0;
    bt_recovery_attempts.store(0);
    bt_tx_ring_head      = 0;
    bt_tx_ring_tail      = 0;
    bt_tx_ring_used      = 0;
    bt_tx_ring_gen       = 0;
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
            bt_state_set(BTState::Advertising);
            break;

#    ifdef ESP_SPP_UNINIT_EVT
        case ESP_SPP_UNINIT_EVT:
#    endif
#    ifdef ESP_SPP_SRV_STOP_EVT
        case ESP_SPP_SRV_STOP_EVT:
#    endif
#    if defined(ESP_SPP_UNINIT_EVT) || defined(ESP_SPP_SRV_STOP_EVT)
            bt_state_set(BTState::Idle);
            break;
#    endif

        case ESP_SPP_SRV_OPEN_EVT: {
            // 连接建立：清空旧缓冲，避免重连后执行半条旧指令
            client_reset_read_buffer(CLIENT_BT);
            bt_state_set(BTState::Connected);
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
            bt_state_set(BTState::Advertising);
            break;

        case ESP_SPP_CONG_EVT:
            bt_state_set(param->cong.cong ? BTState::Congested : BTState::Connected);
            break;

        case ESP_SPP_WRITE_EVT:
            bt_state_set(param->write.cong ? BTState::Congested : BTState::Connected);
            break;

        case ESP_SPP_DATA_IND_EVT: {
            BTState s = bt_state_get();
            if (s == BTState::Advertising) {
                // 在 DATA_IND 之前已收到 SRV_OPEN，保险起见切到 Connected
                bt_state_set(BTState::Connected);
            }
        } break;

        default:
            break;
    }
}

// 判断 BT 消息是否关键：ok/error、状态报告、报警、普通 MSG 都是关键；
// 调试/诊断类 MSG 在拥塞时可丢弃，避免阻塞控制面。
bool bt_tx_message_is_critical(const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return false;
    }
    if (text[0] == 'o' && text[1] == 'k') {
        return true;
    }
    if (strncmp(text, "error:", 6) == 0 || strncmp(text, "error ", 6) == 0) {
        return true;
    }
    if (text[0] == '<') {
        return true;
    }
    if (strncmp(text, "[MSG:", 5) == 0) {
        // grbl_msg_sendf 把消息包装为 [MSG:...\r\n，所以诊断前缀在 [MSG: 之后
        const char* inner = text + 5;
        if (strncmp(inner, "[BT-EOL", 7) == 0) {
            return false;
        }
        if (strncmp(inner, "[PaperDiag]", 11) == 0) {
            return false;
        }
        if (strncmp(inner, "[BTState]", 9) == 0) {
            return false;
        }
        return true;
    }
    if (strncmp(text, "ALARM:", 6) == 0 || strncmp(text, "ALM:", 4) == 0) {
        return true;
    }
    return false;
}

static size_t bt_tx_ring_free(void) {
    return BT_TX_RING_SIZE - bt_tx_ring_used;
}

static bool bt_tx_ring_push(const char* data, size_t len) {
    if (len > bt_tx_ring_free()) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        bt_tx_ring[bt_tx_ring_head] = data[i];
        bt_tx_ring_head             = (bt_tx_ring_head + 1) % BT_TX_RING_SIZE;
    }
    bt_tx_ring_used += len;
    return true;
}

static void bt_tx_ring_reset(void) {
    bt_tx_ring_head = 0;
    bt_tx_ring_tail = 0;
    bt_tx_ring_used = 0;
    bt_tx_ring_gen++;
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
        bool ring_has_data = (bt_tx_ring_used > 0);
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

        // 运动中 planner 即将饥饿时，不做可能阻塞的 SerialBT.write()——
        // xQueueSend 的 1000ms 超时会让主循环停顿，导致段缓冲欠载、电机停转。
        // 关键消息直接入环等待后续 flush，非关键消息丢弃。
        if (bt_planner_is_starving()) {
            if (critical) {
                vTaskEnterCritical(&bt_tx_mux);
                bool ok = bt_tx_ring_push(text, len);
                vTaskExitCritical(&bt_tx_mux);
                return ok;
            }
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
    // 运动中 planner 即将饥饿时，不做可能阻塞的 SerialBT.write()——
    // xQueueSend 的 1000ms 超时会让主循环停顿，导致段缓冲欠载、电机停转。
    if (bt_planner_is_starving()) {
        return;
    }

    while (true) {
        vTaskEnterCritical(&bt_tx_mux);
        if (bt_tx_ring_used == 0) {
            vTaskExitCritical(&bt_tx_mux);
            break;
        }

        // 取出从 tail 开始的一段连续字节（不跨越环尾或 head）
        size_t contiguous;
        if (bt_tx_ring_head > bt_tx_ring_tail) {
            contiguous = bt_tx_ring_head - bt_tx_ring_tail;
        } else {
            contiguous = BT_TX_RING_SIZE - bt_tx_ring_tail;
        }
        if (contiguous > 64) {
            contiguous = 64;
        }

        char    chunk[64];
        size_t  local_tail = bt_tx_ring_tail;  // avoid touching shared state while writing
        // 捕获 reset 代际：释放锁调 SerialBT.write() 期间，SPP CLOSE 回调可能
        // 并发 bt_tx_ring_reset() 把 used 清 0。重进锁后若代际已变，说明本次
        // 推进所依赖的 tail/used 已失效，必须放弃 advance，否则 size_t 减法下溢。
        uint32_t gen_before = bt_tx_ring_gen;
        vTaskExitCritical(&bt_tx_mux);

        // 将连续段复制到本地后再写，避免持锁期间调用 SerialBT.write()
        memcpy(chunk, &bt_tx_ring[local_tail], contiguous);
        size_t written = WebUI::SerialBT.write((const uint8_t*)chunk, contiguous);
        if (written == 0) {
            break;
        }

        vTaskEnterCritical(&bt_tx_mux);
        if (bt_tx_ring_gen != gen_before) {
            // 期间 ring 被 reset（链路断开/重连）——本次 write 的字节已被新
            // 连接的 SRV_OPEN 流程接管或丢弃，绝不能动 tail/used，否则 used 下溢。
            vTaskExitCritical(&bt_tx_mux);
            break;
        }
        size_t advance = (written < contiguous) ? written : contiguous;
        bt_tx_ring_tail = (bt_tx_ring_tail + advance) % BT_TX_RING_SIZE;
        bt_tx_ring_used -= advance;
        vTaskExitCritical(&bt_tx_mux);

        if (written < contiguous) {
            break;
        }
    }
}

static void bt_execute_recovery(uint32_t now_ms) {
    // 恢复期间回调可能把 bt_state 改回 Idle/Advertising；用 recovery_active 标志保证流程不被中断
    bt_state_set(BTState::Recovering);

    switch (bt_recovery_step) {
        case 0:  // 结束 SPP
            vTaskEnterCritical(&bt_tx_mux);
            bt_tx_ring_reset();
            vTaskExitCritical(&bt_tx_mux);
            WebUI::SerialBT.end();
            bt_recovery_step = 1;
            bt_recovery_ts   = now_ms;
            grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[BTState] SPP ended for recovery");
            break;

        case 1:  // 冷却
            if (now_ms - bt_recovery_ts < BT_RECOVERY_COOLDOWN_MS) {
                return;
            }
            bt_recovery_step = 2;
            bt_recovery_ts   = now_ms;
            break;

        case 2: {  // 重新启动 SPP
            uint8_t attempts = bt_recovery_attempts.fetch_add(1) + 1;
            String bt_name = WebUI::bt_config.BTname();
            if (WebUI::SerialBT.begin(bt_name.c_str())) {
                bt_recovery_attempts.store(0);
                bt_recovery_active   = false;
                // begin() 可能已触发 SRV_OPEN_EVT 回调（客户端在恢复窗口内重连），
                // 此时 bt_state 已被回调设为 Connected；不要覆盖它。
                BTState cur = bt_state_get();
                if (cur != BTState::Connected && cur != BTState::Congested) {
                    bt_state_set(BTState::Advertising);
                }
                grbl_msg_sendf(CLIENT_SERIAL, MsgLevel::Info, "[BTState] SPP restarted, advertising as %s", bt_name.c_str());
            } else if (attempts >= BT_RECOVERY_MAX_RETRIES) {
                bt_recovery_active = false;
                bt_state_set(BTState::Idle);
                grbl_msg_sendf(CLIENT_SERIAL,
                               MsgLevel::Info,
                               "[BTState] SPP restart failed %u times, giving up",
                               attempts);
            } else {
                // 重试：回到 step 0 重新 end/begin
                bt_recovery_step = 0;
                grbl_msg_sendf(CLIENT_SERIAL,
                               MsgLevel::Info,
                               "[BTState] SPP restart failed (attempt %u/%u), retrying",
                               attempts,
                               BT_RECOVERY_MAX_RETRIES);
            }
        } break;

        default:
            bt_recovery_step = 0;
            break;
    }
}

void bt_state_update(void) {
    uint32_t now_ms = millis();
    BTState  s      = bt_state_get();

    // 1. 恢复流程优先级最高：即使回调改动了 bt_state，也要把恢复进行到底
    if (bt_recovery_active) {
        bt_execute_recovery(now_ms);
        return;
    }

    // 2. 假连接检测
    if (s == BTState::Connected || s == BTState::Congested) {
        if (now_ms - bt_last_event_ms.load() > BT_LINK_SILENCE_TIMEOUT_MS) {
            if (bt_recovery_attempts.load() >= BT_RECOVERY_MAX_RETRIES) {
                bt_state_set(BTState::Idle);
                grbl_msg_sendf(CLIENT_SERIAL,
                               MsgLevel::Info,
                               "[BTState] Link silent, max recovery retries (%u) exceeded",
                               BT_RECOVERY_MAX_RETRIES);
                return;
            }
            bt_recovery_active   = true;
            bt_recovery_step     = 0;
            bt_recovery_ts       = now_ms;
            bt_state_set(BTState::Recovering);
            grbl_msg_sendf(CLIENT_SERIAL,
                           MsgLevel::Info,
                           "[BTState] Link silent for %u ms, entering recovery",
                           BT_LINK_SILENCE_TIMEOUT_MS);
        }
    }

    // 3. 拥塞解除后刷出缓存的关键消息
    if (bt_state_get() == BTState::Connected) {
        vTaskEnterCritical(&bt_tx_mux);
        bool need_flush = (bt_tx_ring_used > 0);
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
