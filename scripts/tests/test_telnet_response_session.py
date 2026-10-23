"""F49-03 已领取命令响应的会话归属：旧 ok/error/多行结果换连后不得流入新连接。

真实 Serial 领取/标签与 Telnet reset_rx/clearClients/write/write_for_session 编译进宿主；
emit() 复刻 Serial.cpp client_write 的 TELNET 分发（有标签走 write_for_session，无标签走 write）。
"""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('GRBL_ROOT', Path(__file__).resolve().parents[2]))


def function(raw, signature):
    start = raw.index(signature)
    left = raw.index('{', start)
    depth, end = 1, left + 1
    while depth:
        depth += (raw[end] == '{') - (raw[end] == '}')
        end += 1
    return raw[start:end]


STUB = r'''
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
constexpr int CLIENT_TELNET = 0, MAX_TLNT_CLIENTS = 1;
constexpr int SOL_SOCKET = 1, SO_KEEPALIVE = 2, IPPROTO_TCP = 3, TCP_KEEPIDLE = 4, TCP_KEEPINTVL = 5,
              TCP_KEEPCNT = 6, MSG_DONTWAIT = 7;
using TickType_t = unsigned;
constexpr TickType_t TELNET_WRITE_TIMEOUT_TICKS = 1000;
int setsockopt(int, int, int, const void*, size_t) { return 0; }
TickType_t xTaskGetTickCount() { return 0; }
void vTaskDelay(int) {}
#define log_w(...) do {} while (0)
#define log_d(...) do {} while (0)
namespace COMMANDS { void wait(int) {} }
struct TelnetClientLock {};
struct Buffer { void begin() {} };
Buffer client_buffer[1];
uint32_t client_session_generation[1] = {}, client_command_owners[1] = {}, client_response_session[1] = {};
int myMutex;
void vTaskEnterCritical(int*) {}
void vTaskExitCritical(int*) {}
std::map<int, std::string> writes;
int send(int fd, const uint8_t* b, size_t n, int) {
    writes[fd].append(reinterpret_cast<const char*>(b), n);
    return static_cast<int>(n);
}
struct WiFiClient {
    int socket = -1;
    bool live = false;
    explicit operator bool() const { return live; }
    bool connected() const { return live; }
    int fd() { return socket; }
    void stop() { live = false; }
};
struct WiFiServer {
    bool pending = false;
    bool hasClient() { return pending; }
    WiFiClient available() {
        pending = false;
        return WiFiClient{42, true};
    }
};
struct Telnet_Server {
    bool _setupdone = true, _rx_active = true;
    WiFiServer* _telnetserver;
    WiFiClient _telnetClients[1];
    int _RXbufferSize = 0, _RXbufferpos = 0;
    uint32_t _session = 0;
    void reset_rx();
    void clearClients();
    size_t write(const uint8_t*, size_t);
    size_t write_for_session(const uint8_t*, size_t, uint32_t);
    size_t write_locked(const uint8_t*, size_t);
};
'''


def program():
    serial = (ROOT / 'Grbl_Esp32/src/Serial.cpp').read_text(encoding='utf-8')
    telnet = (ROOT / 'Grbl_Esp32/src/WebUI/TelnetServer.cpp').read_text(encoding='utf-8')
    source = STUB
    source += '\n'.join(function(serial, sig) for sig in (
        'uint32_t client_begin_session(',
        'bool client_claim_command(',
        'void client_release_command(',
        'uint32_t client_response_tag(',
    ))
    source += '\n'.join(function(telnet, sig) for sig in (
        'void Telnet_Server::reset_rx()',
        'void Telnet_Server::clearClients()',
        'size_t Telnet_Server::write(',
        'size_t Telnet_Server::write_for_session(',
        'size_t Telnet_Server::write_locked(',
    ))
    source += r'''
int main(int argc, char** argv) {
    const int mode = argc > 1 ? std::atoi(argv[1]) : 0;
    WiFiServer listener;
    Telnet_Server server;
    server._telnetserver = &listener;
    server._telnetClients[0] = WiFiClient{41, true};
    server.reset_rx();
    const uint32_t old = server._session;
    assert(client_claim_command(0, old));
    // 复刻 Serial.cpp client_write 的 TELNET 分发：在途已领取命令的写出带会话标签。
    auto emit = [&server](const char* text) {
        const uint32_t tag = client_response_tag(0);
        if (tag) {
            server.write_for_session(reinterpret_cast<const uint8_t*>(text), std::strlen(text), tag);
        } else {
            server.write(reinterpret_cast<const uint8_t*>(text), std::strlen(text));
        }
    };
    if (mode == 0) {
        // 未换连：已领取命令的 ok 送认领时的连接。
        emit("ok\r\n");
        assert(writes[41] == "ok\r\n");
        assert(writes[42].empty());
        client_release_command(0);
    } else {
        // 旧连接掉线、新连接在等待收养；旧命令仍在领取中（跨会话执行窗口）。
        server._telnetClients[0].live = false;
        listener.pending = true;
        // 旧会话 $$ 多行结果与收尾 ok：全部不得流入新连接，也不应触发收养。
        emit("$130=210.000\r\n");
        emit("$131=297.000\r\n");
        emit("ok\r\n");
        assert(writes[41].empty());
        assert(writes[42].empty());
        client_release_command(0);
        // 无标签广播（横幅/状态行）经 clearClients 收养新连接并 bump 会话；
        // 收养前旧会话仍是惰性当前态，不在这里断言失效。
        const char* banner = "banner\r\n";
        server.write(reinterpret_cast<const uint8_t*>(banner), std::strlen(banner));
        assert(writes[42] == "banner\r\n");
        // 收养后会话已换：旧会话不可再领取，新会话可领取且其 ok 必须送达。
        assert(!client_claim_command(0, old));
        const uint32_t now = server._session;
        assert(client_claim_command(0, now));
        emit("ok\r\n");
        client_release_command(0);
        assert(writes[42] == "banner\r\nok\r\n");
        assert(client_response_tag(0) == 0);
    }
    printf("{\"mode\":%d,\"socket41\":%zu,\"socket42\":%zu}\n", mode, writes[41].size(), writes[42].size());
    return 0;
}
'''
    return source


class TelnetResponseSessionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('CXX') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('需要 host C++ 编译器')
        cls.folder = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.folder.cleanup)
        cpp = Path(cls.folder.name) / 'response_session.cpp'
        cpp.write_text(program(), encoding='utf-8')
        cls.exe = cpp.with_suffix('.exe' if os.name == 'nt' else '')
        cls.env = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ.get('PATH', ''))
        built = subprocess.run([compiler, '-std=c++17', '-O0', str(cpp), '-o', str(cls.exe)],
                               env=cls.env, capture_output=True, timeout=60)
        if built.returncode:
            raise AssertionError(built.stderr.decode('utf-8', 'replace'))

    def check(self, mode):
        ran = subprocess.run([str(self.exe), str(mode)], env=self.env, capture_output=True, timeout=10)
        self.assertEqual(ran.returncode, 0, ran.stderr.decode('utf-8', 'replace'))

    def test_claimed_response_reaches_own_session(self):
        self.check(0)

    def test_stale_claimed_response_dropped_after_reconnect(self):
        self.check(1)


if __name__ == '__main__':
    unittest.main()
