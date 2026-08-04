/*
  TelnetServer.cpp -  telnet server functions class

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

#if defined(ENABLE_WIFI) && defined(ENABLE_TELNET)

#    include "WifiServices.h"

#    include "TelnetServer.h"
#    include "WifiConfig.h"
#    include <WiFi.h>
#    include <lwip/sockets.h>

namespace WebUI {
    Telnet_Server telnet_server;
    bool          Telnet_Server::_setupdone    = false;
    uint16_t      Telnet_Server::_port         = 0;
    WiFiServer*   Telnet_Server::_telnetserver = NULL;
    WiFiClient    Telnet_Server::_telnetClients[MAX_TLNT_CLIENTS];

#    ifdef ENABLE_TELNET_WELCOME_MSG
    IPAddress Telnet_Server::_telnetClientsIP[MAX_TLNT_CLIENTS];
#    endif

    // hutuji §9-A2：Telnet client 跨任务互斥（只加锁，不改协议/不改换纸/不改运动）。
    // 竞态：loopTask（run_once -> grbl_sendf -> client_write -> Telnet_Server::write）
    // 与 clientCheckTask（wifi_config.handle -> WiFiServices::handle -> Telnet_Server::handle）
    // 同优先级、同核并发操作同一个 WiFiClient 与同一个 slot：
    //   1) write() 内 WiFiClient::write() 遇到非 EAGAIN 错误会调 stop()，stop() 把
    //      _rxBuffer 置空并释放（arduino-esp32 WiFiClient.cpp）；此刻读侧正在
    //      available()/read() 里走 WiFiClientRxBuffer::fillBuffer -> lwip_recv ->
    //      pbuf_free，命中 assert "pbuf_free: p->ref > 0"（上游 bdring/Grbl_Esp32#1364）。
    //   2) clearClients() 在两个任务里并发 available() 并对同一 slot 先 stop() 再赋值，
    //      等于在读侧脚下换掉 _rxBuffer 智能指针。
    // 递归锁而非普通锁：handle() 内 report_init_message(CLIENT_TELNET) 会回到 write()，
    // 同一任务必须能重入。
    // write() 不得在 TCP 背压下长占 loopTask/clientCheckTask。下方改用非阻塞 send +
    // 绝对 deadline：完整写出或关闭该 session，避免静默少 `ok`，也给实时输入留出上界。
    static constexpr TickType_t TELNET_WRITE_TIMEOUT_TICKS = pdMS_TO_TICKS(1000);
    static SemaphoreHandle_t telnetClientMutex = xSemaphoreCreateRecursiveMutex();
    class TelnetClientLock {
    public:
        TelnetClientLock() {
            if (telnetClientMutex != nullptr) {
                xSemaphoreTakeRecursive(telnetClientMutex, portMAX_DELAY);
            }
        }
        ~TelnetClientLock() {
            if (telnetClientMutex != nullptr) {
                xSemaphoreGiveRecursive(telnetClientMutex);
            }
        }
        TelnetClientLock(const TelnetClientLock&) = delete;
        TelnetClientLock& operator=(const TelnetClientLock&) = delete;
    };

    Telnet_Server::Telnet_Server() {
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
    }

    bool Telnet_Server::begin() {
        TelnetClientLock lock;
        bool no_error = true;
        end();
        _RXbufferSize = 0;
        _RXbufferpos  = 0;

        if (telnet_enable->get() == 0) {
            return false;
        }
        _port = telnet_port->get();

        //create instance
        _telnetserver = new WiFiServer(_port, MAX_TLNT_CLIENTS);
        _telnetserver->setNoDelay(true);
        String s = "[MSG:TELNET Started " + String(_port) + "]\r\n";
        grbl_send(CLIENT_ALL, (char*)s.c_str());
        //start telnet server
        _telnetserver->begin();
        _setupdone = true;
        return no_error;
    }

    void Telnet_Server::end() {
        TelnetClientLock lock;
        _setupdone    = false;
        _RXbufferSize = 0;
        _RXbufferpos  = 0;
        if (_telnetserver) {
            delete _telnetserver;
            _telnetserver = NULL;
        }
    }

    void Telnet_Server::clearClients() {
        //check if there are any new clients
        if (_telnetserver->hasClient()) {
            uint8_t i;
            for (i = 0; i < MAX_TLNT_CLIENTS; i++) {
                //find free/disconnected spot
                if (!_telnetClients[i] || !_telnetClients[i].connected()) {
#    ifdef ENABLE_TELNET_WELCOME_MSG
                    _telnetClientsIP[i] = IPAddress(0, 0, 0, 0);
#    endif
                    if (_telnetClients[i]) {
                        _telnetClients[i].stop();
                    }
                    _telnetClients[i] = _telnetserver->available();
                    // hutuji §9-A：半开死连接回收（只开 SO_KEEPALIVE 无效，须显式三参数）
                    // ~KEEPIDLE 10 + 3×KEEPINTVL ≈ 19s 发现对端掉电/NAT 超时
                    {
                        int keepalive = 1;
                        int keepidle  = 10;
                        int keepintvl = 3;
                        int keepcnt   = 3;
                        int s = _telnetClients[i].fd();
                        setsockopt(s, SOL_SOCKET,  SO_KEEPALIVE, &keepalive, sizeof(keepalive));
                        setsockopt(s, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle,  sizeof(keepidle));
                        setsockopt(s, IPPROTO_TCP, TCP_KEEPINTVL,&keepintvl, sizeof(keepintvl));
                        setsockopt(s, IPPROTO_TCP, TCP_KEEPCNT,  &keepcnt,   sizeof(keepcnt));
                    }
                    break;
                }
            }
            if (i >= MAX_TLNT_CLIENTS) {
                //no free/disconnected spot so reject
                _telnetserver->available().stop();
            }
        }
    }

    size_t Telnet_Server::write(const uint8_t* buffer, size_t size) {
        TelnetClientLock lock;
        size_t wsize = 0;
        if (!_setupdone || _telnetserver == NULL) {
            log_d("[TELNET out blocked]");
            return 0;
        }

        clearClients();

        // 可靠语义是「整段写出或明确断链」，不能在背压时静默截断 ok/状态行。
        for (uint8_t i = 0; i < MAX_TLNT_CLIENTS; i++) {
            if (!_telnetClients[i] || !_telnetClients[i].connected()) {
                continue;
            }
            const int socket = _telnetClients[i].fd();
            if (socket < 0) {
                continue;
            }
            // 每个有效客户端从 0 开始写整段（MAX_TLNT_CLIENTS>1 时不得沿用上一客户端的进度）。
            wsize = 0;
            const TickType_t began = xTaskGetTickCount();
            int send_errno = 0;
            while (wsize < size) {
                // 绝对 deadline 覆盖每次正 partial write；不能只在 EAGAIN 分支看时间，
                // 否则对端每轮接一小段就能无限占住 loopTask。
                if ((xTaskGetTickCount() - began) >= TELNET_WRITE_TIMEOUT_TICKS) {
                    log_w("[TELNET] write timeout sent=%u/%u", static_cast<unsigned>(wsize),
                          static_cast<unsigned>(size));
                    _telnetClients[i].stop();
                    return 0;
                }
                const int written = send(socket, buffer + wsize, size - wsize, MSG_DONTWAIT);
                send_errno = written < 0 ? errno : 0;
                if (written > 0) {
                    wsize += static_cast<size_t>(written);
                    continue;
                }
                if (written < 0 && (send_errno == EAGAIN || send_errno == EWOULDBLOCK)) {
                    COMMANDS::wait(0);
                    vTaskDelay(1);
                    continue;
                }
                log_w("[TELNET] write failed n=%d errno=%d sent=%u/%u", written, send_errno,
                      static_cast<unsigned>(wsize), static_cast<unsigned>(size));
                _telnetClients[i].stop();
                return 0;
            }
        }
        return wsize;
    }

    void Telnet_Server::handle() {
        TelnetClientLock lock;
        COMMANDS::wait(0);
        //check if can read
        if (!_setupdone || _telnetserver == NULL) {
            return;
        }
        clearClients();
        //check clients for data
        //uint8_t c;
        for (uint8_t i = 0; i < MAX_TLNT_CLIENTS; i++) {
            if (_telnetClients[i] && _telnetClients[i].connected()) {
#    ifdef ENABLE_TELNET_WELCOME_MSG
                if (_telnetClientsIP[i] != _telnetClients[i].remoteIP()) {
                    report_init_message(CLIENT_TELNET);
                    _telnetClientsIP[i] = _telnetClients[i].remoteIP();
                }
#    endif
                if (_telnetClients[i].available()) {
                    uint8_t buf[1024];
                    COMMANDS::wait(0);
                    int readlen  = _telnetClients[i].available();
                    int writelen = TELNETRXBUFFERSIZE - available();
                    if (readlen > 1024) {
                        readlen = 1024;
                    }
                    if (readlen > writelen) {
                        readlen = writelen;
                    }
                    if (readlen > 0) {
                        _telnetClients[i].read(buf, readlen);
                        push(buf, readlen);
                    }
                    return;
                }
            } else {
                if (_telnetClients[i]) {
#    ifdef ENABLE_TELNET_WELCOME_MSG
                    _telnetClientsIP[i] = IPAddress(0, 0, 0, 0);
#    endif
                    _telnetClients[i].stop();
                }
            }
            COMMANDS::wait(0);
        }
    }

    int Telnet_Server::peek(void) {
        if (_RXbufferSize > 0) {
            return _RXbuffer[_RXbufferpos];
        } else {
            return -1;
        }
    }

    int Telnet_Server::available() { return _RXbufferSize; }

    int Telnet_Server::get_rx_buffer_available() { return TELNETRXBUFFERSIZE - _RXbufferSize; }

    bool Telnet_Server::push(uint8_t data) {
        log_i("[TELNET]push %c", data);
        if ((1 + _RXbufferSize) <= TELNETRXBUFFERSIZE) {
            int current = _RXbufferpos + _RXbufferSize;
            if (current > TELNETRXBUFFERSIZE) {
                current = current - TELNETRXBUFFERSIZE;
            }
            if (current > (TELNETRXBUFFERSIZE - 1)) {
                current = 0;
            }
            _RXbuffer[current] = data;
            _RXbufferSize++;
            log_i("[TELNET]buffer size %d", _RXbufferSize);
            return true;
        }
        return false;
    }

    bool Telnet_Server::push(const uint8_t* data, int data_size) {
        if ((data_size + _RXbufferSize) <= TELNETRXBUFFERSIZE) {
            int data_processed = 0;
            int current        = _RXbufferpos + _RXbufferSize;
            if (current > TELNETRXBUFFERSIZE) {
                current = current - TELNETRXBUFFERSIZE;
            }
            for (int i = 0; i < data_size; i++) {
                if (current > (TELNETRXBUFFERSIZE - 1)) {
                    current = 0;
                }

                _RXbuffer[current] = data[i];
                current++;
                data_processed++;

                COMMANDS::wait(0);
                //vTaskDelay(1 / portTICK_RATE_MS);  // Yield to other tasks
            }
            _RXbufferSize += data_processed;
            return true;
        }
        return false;
    }

    int Telnet_Server::read(void) {
        if (_RXbufferSize > 0) {
            int v = _RXbuffer[_RXbufferpos];
            //log_d("[TELNET]read %c",char(v));
            _RXbufferpos++;
            if (_RXbufferpos > (TELNETRXBUFFERSIZE - 1)) {
                _RXbufferpos = 0;
            }
            _RXbufferSize--;
            return v;
        } else {
            return -1;
        }
    }

    Telnet_Server::~Telnet_Server() { end(); }
}
#endif  // Enable TELNET && ENABLE_WIFI
