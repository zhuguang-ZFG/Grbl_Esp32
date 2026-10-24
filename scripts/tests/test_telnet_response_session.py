"""2026-10-23 续修：已领取命令的响应标签必须限定在领取任务内。

真实 Serial 领取/释放/标签/client_write 与 Telnet reset_rx/clearClients/write/
write_for_session 都编译进宿主；测试只切换当前任务句柄模拟同核任务，不复制
client_write 的 TELNET 分发逻辑。编号每次从 Report.h 解析（CLIENT_TELNET=3），
固件改枚举时 fixture 跟随，另有一条用例显式钉住上板契约值。
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(os.environ.get('GRBL_ROOT', Path(__file__).resolve().parents[2]))

CLIENT_IDS = ('CLIENT_SERIAL', 'CLIENT_BT', 'CLIENT_WEBUI', 'CLIENT_TELNET', 'CLIENT_INPUT')


def report_client_ids():
    text = (ROOT / 'Grbl_Esp32/src/Report.h').read_text(encoding='utf-8')
    found = {}
    for name in CLIENT_IDS:
        match = re.search(r'^#define\s+' + name + r'\s+(\d+)\s*(?://.*)?$', text, re.MULTILINE)
        if not match:
            raise AssertionError('Report.h 缺少 ' + name)
        found[name] = int(match.group(1))
    for name, pattern in (('CLIENT_ALL', r'0x[0-9A-Fa-f]+|\d+'), ('CLIENT_COUNT', r'\d+')):
        match = re.search(
            r'^#define\s+' + name + r'\s+(' + pattern + r')\s*(?://.*)?$', text, re.MULTILINE)
        if not match:
            raise AssertionError('Report.h 缺少 ' + name)
        found[name] = int(match.group(1), 0)
    return found


def function(raw, signature):
    start = raw.index(signature)
    left = raw.index('{', start)
    depth, end = 1, left + 1
    while depth:
        depth += (raw[end] == '{') - (raw[end] == '}')
        end += 1
    return raw[start:end]


STUB = r'''
#define ENABLE_WIFI 1
#define ENABLE_TELNET 1
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
__CLIENT_CONSTANTS__
constexpr int MAX_TLNT_CLIENTS = 1;
constexpr int SOL_SOCKET = 1, SO_KEEPALIVE = 2, IPPROTO_TCP = 3, TCP_KEEPIDLE = 4, TCP_KEEPINTVL = 5,
              TCP_KEEPCNT = 6, MSG_DONTWAIT = 7;
using TickType_t = unsigned;
using TaskHandle_t = int;
constexpr TickType_t TELNET_WRITE_TIMEOUT_TICKS = 1000;
int host_current_task = 0;
TaskHandle_t xTaskGetCurrentTaskHandle() { return host_current_task; }
int setsockopt(int, int, int, const void*, size_t) { return 0; }
TickType_t xTaskGetTickCount() { return 0; }
void vTaskDelay(int) {}
#define log_w(...) do {} while (0)
#define log_d(...) do {} while (0)
namespace COMMANDS { void wait(int) {} }
struct TelnetClientLock {};
struct Buffer { void begin() {} };
Buffer client_buffer[CLIENT_COUNT];
uint32_t client_session_generation[CLIENT_COUNT] = {}, client_command_owners[CLIENT_COUNT] = {},
         client_response_session[CLIENT_COUNT] = {};
TaskHandle_t client_response_owner_task[CLIENT_COUNT] = {};
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
    size_t write_for_session(const uint8_t*, size_t, uint32_t, bool broadcast_on_stale = false);
    size_t write_locked(const uint8_t*, size_t);
};
namespace WebUI { Telnet_Server telnet_server; }
struct Uart { std::string out; void write(const char* text) { out += text; } };
Uart Uart0;
'''


def program():
    serial = (ROOT / 'Grbl_Esp32/src/Serial.cpp').read_text(encoding='utf-8')
    telnet = (ROOT / 'Grbl_Esp32/src/WebUI/TelnetServer.cpp').read_text(encoding='utf-8')
    ids = report_client_ids()
    constants = (
        'constexpr int CLIENT_SERIAL = {CLIENT_SERIAL}, CLIENT_BT = {CLIENT_BT}, '
        'CLIENT_WEBUI = {CLIENT_WEBUI}, CLIENT_TELNET = {CLIENT_TELNET},\n'
        '              CLIENT_INPUT = {CLIENT_INPUT}, CLIENT_ALL = {CLIENT_ALL}, '
        'CLIENT_COUNT = {CLIENT_COUNT};'
    ).format(**ids)
    source = STUB.replace('__CLIENT_CONSTANTS__', constants)
    source += '\n'.join(function(serial, sig) for sig in (
        'uint32_t client_begin_session(',
        'bool client_claim_command(',
        'void client_release_command(',
        'uint32_t client_response_tag(',
        'void client_write(',
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
    Telnet_Server& server = WebUI::telnet_server;
    server._telnetserver = &listener;
    server._telnetClients[0] = WiFiClient{41, true};
    host_current_task = 1;
    server.reset_rx();
    const uint32_t old = server._session;

    if (mode == 0) {
        // 未换连：领取任务自己的 ok 送认领会话；普通串口分派不碰 Telnet。
        assert(client_claim_command(CLIENT_TELNET, old));
        client_write(CLIENT_SERIAL, "serial-only\r\n");
        assert(writes[41].empty());
        assert(writes[42].empty());
        client_write(CLIENT_TELNET, "ok\r\n");
        assert(writes[41] == "ok\r\n");
        assert(writes[42].empty());
        client_release_command(CLIENT_TELNET);
        assert(client_response_tag(CLIENT_TELNET) == 0);
    } else if (mode == 1) {
        // 旧命令在其他任务执行中：旧连接掉线、新连接等待收养。
        assert(client_claim_command(CLIENT_TELNET, old));
        server._telnetClients[0].live = false;
        listener.pending = true;
        // 旧任务的旧结果先到（会话尚未 bump）：死槽不写出，更不能落到新连接。
        client_write(CLIENT_TELNET, "$130=210.000\r\n");
        client_write(CLIENT_TELNET, "error:2\r\n");
        assert(writes[41].empty());
        assert(writes[42].empty());
        // 另一任务（clientCheckTask）的欢迎消息与 `?` 应答属当前连接：不能继承
        // 别的任务的领取标签，必须走正常收养路径，否则新连接永远得不到应答。
        host_current_task = 2;
        client_write(CLIENT_TELNET, "welcome\r\n");
        const uint32_t now = server._session;
        assert(now != old);
        client_write(CLIENT_TELNET, "<Idle|MPos:0.000,0.000,0.000>\r\n");
        assert(writes[42] == "welcome\r\n<Idle|MPos:0.000,0.000,0.000>\r\n");
        assert(writes[41].empty());
        // 收养已 bump 会话：旧任务的多行结果与 ok 被 session 核验拦住。
        host_current_task = 1;
        client_write(CLIENT_TELNET, "$131=297.000\r\n");
        client_write(CLIENT_TELNET, "ok\r\n");
        assert(writes[42] == "welcome\r\n<Idle|MPos:0.000,0.000,0.000>\r\n");
        // 旧会话不可再领取；新会话领取后的 ok 必须送达新连接。
        assert(!client_claim_command(CLIENT_TELNET, old));
        host_current_task = 2;
        assert(client_claim_command(CLIENT_TELNET, now));
        client_write(CLIENT_TELNET, "ok-new\r\n");
        assert(writes[42] == "welcome\r\n<Idle|MPos:0.000,0.000,0.000>\r\nok-new\r\n");
        client_release_command(CLIENT_TELNET);
        host_current_task = 1;
        assert(client_response_tag(CLIENT_TELNET) == 0);
    } else {
        // 领取前后标签作用域；跨任务 release 不得清掉真正领取者的标签。
        host_current_task = 1;
        assert(client_response_tag(CLIENT_TELNET) == 0);
        assert(client_claim_command(CLIENT_TELNET, old));
        assert(client_response_tag(CLIENT_TELNET) == old);
        host_current_task = 2;
        assert(client_response_tag(CLIENT_TELNET) == 0);
        client_release_command(CLIENT_TELNET);
        host_current_task = 1;
        assert(client_response_tag(CLIENT_TELNET) == old);
        assert(client_command_owners[CLIENT_TELNET] == 1);
        // 领取任务内 CLIENT_ALL 仍按旧会话核验；普通串口照常收到。
        client_write(CLIENT_ALL, "all-owned\r\n");
        assert(writes[41] == "all-owned\r\n");
        assert(Uart0.out == "all-owned\r\n");
        client_release_command(CLIENT_TELNET);
        assert(client_response_tag(CLIENT_TELNET) == 0);
        assert(client_command_owners[CLIENT_TELNET] == 0);
        // 无标签后其他任务的 CLIENT_ALL 走正常广播。
        host_current_task = 2;
        client_write(CLIENT_ALL, "all-free\r\n");
        assert(writes[41] == "all-owned\r\nall-free\r\n");
        assert(Uart0.out == "all-owned\r\nall-free\r\n");
    }
    printf("{\"mode\":%d,\"socket41\":%zu,\"socket42\":%zu}\n", mode,
           writes[41].size(), writes[42].size());
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

    def test_other_task_welcome_and_status_reach_new_session(self):
        self.check(1)

    def test_tag_scoped_to_claiming_task_and_release(self):
        self.check(2)

    def test_client_ids_match_report_header(self):
        ids = report_client_ids()
        # 上板契约：Telnet 是第 4 个客户端，广播是 0xFF，计数覆盖全部编号。
        self.assertEqual(ids['CLIENT_TELNET'], 3)
        self.assertEqual(ids['CLIENT_ALL'], 0xFF)
        self.assertEqual(ids['CLIENT_COUNT'], 5)
        self.assertLess(max(ids[name] for name in CLIENT_IDS), ids['CLIENT_COUNT'])


if __name__ == '__main__':
    unittest.main()
