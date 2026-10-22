"""编译真实 Telnet 收包/会话边界入口与协议行状态，注入换连、短读和超长行交错。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get('GRBL_ROOT', Path(__file__).resolve().parents[2]))


def function_body(source, signature, required=True):
    if signature not in source:
        if required:
            raise AssertionError('缺少函数: ' + signature)
        return ''
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError('函数体未闭合')


class HostBuild(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler:
            raise unittest.SkipTest('需要 host C++ 编译器')
        cls.compiler = compiler
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        cls.env = dict(os.environ)
        cls.env['PATH'] = str(Path(compiler).parent) + os.pathsep + cls.env.get('PATH', '')

    @classmethod
    def build(cls, name, program):
        stem = Path(cls.directory.name) / name
        stem.with_suffix('.cpp').write_text(program, encoding='utf-8')
        exe = stem.with_suffix('.exe' if os.name == 'nt' else '')
        result = subprocess.run([cls.compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                                 str(stem.with_suffix('.cpp')), '-o', str(exe)],
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stderr or result.stdout)
        return exe

    def run_case(self, exe, scenario):
        result = subprocess.run([str(exe), str(scenario)], env=self.env,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)


class TelnetReceiveTest(HostBuild):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        source = (ROOT / 'Grbl_Esp32/src/WebUI/TelnetServer.cpp').read_text(encoding='utf-8')
        reset = function_body(source, 'void Telnet_Server::reset_rx()', required=False)
        program = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <vector>
namespace COMMANDS { void wait(int) {} }
struct TelnetClientLock { TelnetClientLock() {} };
struct WiFiClient {
    bool live = true;
    int announced = 0, received = 0, requested = -1, stops = 0;
    explicit operator bool() const { return live; }
    bool connected() const { return live; }
    int available() const { return announced; }
    int read(uint8_t* buffer, int length) {
        requested = length;
        const int actual = std::min(length, received);
        for (int i = 0; i < actual; ++i) buffer[i] = static_cast<uint8_t>('A' + i);
        return actual;
    }
    void stop() { live = false; ++stops; }
};
struct WiFiServer {};
#define CLIENT_TELNET 3
uint32_t client_begin_session(uint8_t) { return 0; }
class Telnet_Server {
public:
    static const int MAX_TLNT_CLIENTS = 1;
    static const int TELNETRXBUFFERSIZE = 1200;
    bool _setupdone = true;
    WiFiServer* _telnetserver = new WiFiServer;
    WiFiClient _telnetClients[MAX_TLNT_CLIENTS];
    int _RXbufferSize = 0, _RXbufferpos = 0;
    bool _rx_active = false;
    uint32_t _rx_epoch = 0;
    std::vector<uint8_t> pushed;
    void clearClients() {}
    int available() const { return _RXbufferSize; }
    bool push(const uint8_t* buffer, int length) {
        assert(length > 0 && length <= TELNETRXBUFFERSIZE - available());
        pushed.insert(pushed.end(), buffer, buffer + length);
        _RXbufferSize += length;
        return true;
    }
    void reset_rx();
    void handle();
    void end();
};
''' + (reset or 'void Telnet_Server::reset_rx() {}') + '\n' + \
            function_body(source, 'void Telnet_Server::handle()') + '\n' + \
            function_body(source, 'void Telnet_Server::end()') + r'''
int main(int argc, char** argv) {
    assert(argc == 2);
    const int scenario = std::atoi(argv[1]);
    Telnet_Server server;
    auto& client = server._telnetClients[0];
    if (scenario == 6) {
        server._RXbufferSize = 12;
        server._RXbufferpos = 7;
        server.end();
        assert(!server._setupdone && server._telnetserver == nullptr);
        assert(!client.connected());
        assert(server._RXbufferSize == 0 && server._RXbufferpos == 0);
        server.end();
        assert(!client.connected());
        return 0;
    }
    const int actual[] = {7, 2, 0, -1, 2000, 7};
    client.announced = scenario == 4 ? 2000 : 7;
    client.received = actual[scenario];
    if (scenario == 5) server._RXbufferSize = 1198;
    server.handle();
    const int expected[] = {7, 2, 0, 0, 1024, 2};
    assert(server.pushed.size() == static_cast<size_t>(expected[scenario]));
    for (int i = 0; i < expected[scenario]; ++i)
        assert(server.pushed[i] == static_cast<uint8_t>('A' + i));
    return 0;
}
'''
        cls.exe = cls.build('telnet_receive', program)

    def test_full_read(self): self.run_case(self.exe, 0)
    def test_short_read_keeps_only_received_bytes(self): self.run_case(self.exe, 1)
    def test_zero_read_does_not_enqueue(self): self.run_case(self.exe, 2)
    def test_read_error_does_not_enqueue(self): self.run_case(self.exe, 3)
    def test_stack_buffer_limit(self): self.run_case(self.exe, 4)
    def test_ring_capacity_limit(self): self.run_case(self.exe, 5)
    def test_end_closes_existing_client_before_reopen(self): self.run_case(self.exe, 6)


class TelnetLifecycleTest(HostBuild):
    """换连/断开/写失败必须把下游 Telnet 队列与协议半行一起作废。"""
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        source = (ROOT / 'Grbl_Esp32/src/WebUI/TelnetServer.cpp').read_text(encoding='utf-8')
        reset = function_body(source, 'void Telnet_Server::reset_rx()', required=False)
        program = r'''
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <vector>
#define ENABLE_TELNET 1
#define MSG_DONTWAIT 0
#ifndef EAGAIN
#define EAGAIN 11
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK EAGAIN
#endif
#ifndef EPIPE
#define EPIPE 32
#endif
#ifndef SOL_SOCKET
#define SOL_SOCKET 1
#endif
#ifndef SO_KEEPALIVE
#define SO_KEEPALIVE 2
#endif
#ifndef IPPROTO_TCP
#define IPPROTO_TCP 6
#endif
#ifndef TCP_KEEPIDLE
#define TCP_KEEPIDLE 4
#endif
#ifndef TCP_KEEPINTVL
#define TCP_KEEPINTVL 5
#endif
#ifndef TCP_KEEPCNT
#define TCP_KEEPCNT 6
#endif
namespace COMMANDS { void wait(int) {} }
struct TelnetClientLock { TelnetClientLock() {} };
using TickType_t = uint32_t;
static const TickType_t TELNET_WRITE_TIMEOUT_TICKS = 1000;
static TickType_t now_ticks = 0;
static int send_mode = 0;  // 0 ok, 1 error, 2 EAGAIN until deadline
static bool offered_stopped = false;
static uint32_t next_epoch = 1;
static int invalidations = 0;
static std::vector<uint8_t> serial_telnet_queue;
static TickType_t xTaskGetTickCount() { return now_ticks; }
static void vTaskDelay(int) {}
static int send(int, const void*, size_t size, int) {
    if (send_mode == 0) return static_cast<int>(size);
    if (send_mode == 2) {
        now_ticks += 1000;
        errno = EAGAIN;
        return -1;
    }
    errno = EPIPE;
    return -1;
}
static int setsockopt(int, int, int, const void*, unsigned int) { return 0; }
static void log_w(const char*, ...) {}
static void log_d(const char*, ...) {}
struct WiFiClient {
    bool live = false;
    int announced = 0, received = 0, requested = -1, stops = 0;
    bool* offered = nullptr;
    explicit operator bool() const { return live; }
    bool connected() const { return live; }
    int available() const { return announced; }
    int read(uint8_t* buffer, int length) {
        requested = length;
        const int actual = std::min(length, received);
        for (int i = 0; i < actual; ++i) buffer[i] = static_cast<uint8_t>('A' + i);
        return actual;
    }
    int fd() const { return live ? 7 : -1; }
    void stop() {
        live = false;
        ++stops;
        if (offered) *offered = true;
    }
};
struct WiFiServer {
    std::vector<WiFiClient> pending;
    bool hasClient() { return !pending.empty(); }
    WiFiClient available() {
        WiFiClient client = pending.front();
        pending.erase(pending.begin());
        client.offered = &offered_stopped;
        return client;
    }
};
#define CLIENT_TELNET 3
uint32_t client_begin_session(uint8_t) {
    ++invalidations;
    serial_telnet_queue.clear();
    return next_epoch++;
}
class Telnet_Server {
public:
    static const int MAX_TLNT_CLIENTS = 1;
    static const int TELNETRXBUFFERSIZE = 1200;
    bool _setupdone = true;
    WiFiServer* _telnetserver = nullptr;
    WiFiClient _telnetClients[MAX_TLNT_CLIENTS];
    uint8_t _RXbuffer[TELNETRXBUFFERSIZE];
    int _RXbufferSize = 0, _RXbufferpos = 0;
    bool _rx_active = false;
    uint32_t _rx_epoch = 0;
    void clearClients();
    void reset_rx();
    void handle();
    void end();
    size_t write(const uint8_t* buffer, size_t size);
    int available() const { return _RXbufferSize; }
    bool push(const uint8_t* buffer, int length) {
        assert(length > 0 && length <= TELNETRXBUFFERSIZE - available());
        for (int i = 0; i < length; ++i) {
            _RXbuffer[(_RXbufferpos + _RXbufferSize) % TELNETRXBUFFERSIZE] = buffer[i];
            ++_RXbufferSize;
        }
        return true;
    }
    void fill_ring(int length) {
        for (int i = 0; i < length; ++i) _RXbuffer[i] = static_cast<uint8_t>('a' + i);
        _RXbufferSize = length;
        _RXbufferpos = 0;
    }
};
''' + (reset or 'void Telnet_Server::reset_rx() {}') + '\n' + \
            function_body(source, 'void Telnet_Server::clearClients()') + '\n' + \
            function_body(source, 'size_t Telnet_Server::write(const uint8_t* buffer, size_t size)') + '\n' + \
            function_body(source, 'void Telnet_Server::handle()') + '\n' + \
            function_body(source, 'void Telnet_Server::end()') + r'''
static void reset_case(Telnet_Server& server, bool active) {
    server._telnetserver = new WiFiServer;
    server._telnetClients[0].live = active;
    server._telnetClients[0].stops = 0;
    server._rx_active = active;
    server._rx_epoch = 4;
    server.fill_ring(9);
    serial_telnet_queue = {'$', 'S', 'y', 's', 't', 'e', 'm', '/'};
    invalidations = 0;
    offered_stopped = false;
    send_mode = 0;
    now_ticks = 0;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const int scenario = std::atoi(argv[1]);
    Telnet_Server server;
    if (scenario == 0) {  // 断开的旧连接被新连接替换
        reset_case(server, false);
        server._rx_active = true;
        WiFiClient next;
        next.live = true;
        server._telnetserver->pending.push_back(next);
        server.handle();
        assert(invalidations >= 1);
        assert(serial_telnet_queue.empty());
        assert(server._RXbufferSize == 0 && server._RXbufferpos == 0);
        assert(server._rx_active);
        assert(server._telnetClients[0].live);
        assert(server._rx_epoch != 4);
        return 0;
    }
    if (scenario == 1) {  // end 必须同时作废旧会话
        reset_case(server, true);
        server.end();
        assert(invalidations >= 1);
        assert(serial_telnet_queue.empty());
        assert(server._RXbufferSize == 0 && server._RXbufferpos == 0);
        assert(!server._rx_active);
        assert(!server._telnetClients[0].live);
        return 0;
    }
    if (scenario == 2) {  // 对端掉线且尚无新连接
        reset_case(server, false);
        server._rx_active = true;
        server.handle();
        assert(invalidations >= 1);
        assert(serial_telnet_queue.empty());
        assert(server._RXbufferSize == 0);
        assert(!server._rx_active);
        return 0;
    }
    if (scenario == 3) {  // 唯一槽位被占用时拒绝新连接，不得动现有会话
        reset_case(server, true);
        WiFiClient next;
        next.live = true;
        server._telnetserver->pending.push_back(next);
        server.handle();
        assert(offered_stopped);
        assert(invalidations == 0);
        assert(serial_telnet_queue.size() == 8);
        assert(server._RXbufferSize == 9);
        assert(server._rx_active);
        assert(server._telnetClients[0].live);
        assert(server._rx_epoch == 4);
        return 0;
    }
    if (scenario == 4) {  // 首次接入也要清掉复用槽位里的旧字节
        reset_case(server, false);
        WiFiClient next;
        next.live = true;
        server._telnetserver->pending.push_back(next);
        server.handle();
        assert(invalidations >= 1);
        assert(serial_telnet_queue.empty());
        assert(server._RXbufferSize == 0);
        assert(server._rx_active);
        assert(server._telnetClients[0].live);
        return 0;
    }
    if (scenario == 5 || scenario == 6) {  // 写错误/写超时必须作废该会话
        reset_case(server, true);
        send_mode = scenario == 5 ? 1 : 2;
        const uint8_t payload[] = {'o', 'k', '\r', '\n'};
        const size_t written = server.write(payload, sizeof(payload));
        assert(written == 0);
        assert(invalidations >= 1);
        assert(serial_telnet_queue.empty());
        assert(server._RXbufferSize == 0);
        assert(!server._rx_active);
        assert(!server._telnetClients[0].live);
        return 0;
    }
    return 2;
}
'''
        cls.exe = cls.build('telnet_lifecycle', program)

    def test_replacement_invalidates_old_session(self): self.run_case(self.exe, 0)
    def test_end_invalidates_old_session(self): self.run_case(self.exe, 1)
    def test_disconnect_invalidates_old_session(self): self.run_case(self.exe, 2)
    def test_rejected_client_keeps_incumbent(self): self.run_case(self.exe, 3)
    def test_first_accept_clears_stale_ring(self): self.run_case(self.exe, 4)
    def test_write_error_invalidates_session(self): self.run_case(self.exe, 5)
    def test_write_timeout_invalidates_session(self): self.run_case(self.exe, 6)


class ProtocolBoundaryTest(HostBuild):
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        source = (ROOT / 'Grbl_Esp32/src/Protocol.cpp').read_text(encoding='utf-8')
        program = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
#define CLIENT_SERIAL 0
#define CLIENT_BT 1
#define CLIENT_TELNET 3
#define CLIENT_COUNT 5
#define LINE_BUFFER_SIZE 256
enum class Error { Ok, Eol, Overflow, SystemGcLock, IdleError, AuthenticationFailed };
namespace WebUI { enum class AuthenticationLevel { LEVEL_GUEST }; }
struct SystemState { bool abort = false; } sys;
typedef struct {
    char buffer[LINE_BUFFER_SIZE];
    int len;
    int line_number;
    uint32_t rx_epoch;
    bool discard_until_eol;
} client_line_t;
client_line_t client_lines[CLIENT_COUNT];
struct Byte { uint8_t data; uint32_t epoch; };
static std::deque<Byte> queues[CLIENT_COUNT];
static std::vector<std::string> executed;
static int overflow_reports = 0;
static int client_read(uint8_t client) __attribute__((unused));
static int client_read(uint8_t client) {
    if (queues[client].empty()) return -1;
    const int data = queues[client].front().data;
    queues[client].pop_front();
    return data;
}
static int client_read(uint8_t client, uint32_t& epoch) __attribute__((unused));
static int client_read(uint8_t client, uint32_t& epoch) {
    if (queues[client].empty()) {
        epoch = 0;
        return -1;
    }
    const Byte byte = queues[client].front();
    queues[client].pop_front();
    epoch = byte.epoch;
    return byte.data;
}
static void report_status_message(Error status, uint8_t) {
    if (status == Error::Overflow) ++overflow_reports;
}
static Error execute_line(char* line, uint8_t, WebUI::AuthenticationLevel) {
    executed.push_back(line);
    return Error::Ok;
}
static bool check_license() { return true; }
static void license_notify_motion_blocked() {}
static void paper_bt_on_first_host_ack() {}
static void protocol_execute_realtime() {}
''' + function_body(source, 'static void empty_line(uint8_t client)') + '\n' + \
            function_body(source, 'static void empty_lines()') + '\n' + \
            function_body(source, 'Error add_char_to_line(char c, uint8_t client)') + '\n' + \
            function_body(source, 'static bool protocol_line_is_motion_gcode_g0_g3(const char* line)') + '\n' + \
            'bool client_claim_command(uint8_t,uint32_t){return true;}void client_release_command(uint8_t){}\n' + source[source.index('class ClientCommandOwner {'):source.index('Error execute_line(')] + '\n' + function_body(source, 'static bool protocol_poll_client(uint8_t client)') + r'''
static void enqueue(uint8_t client, const char* text, uint32_t epoch) {
    for (const char* p = text; *p; ++p) queues[client].push_back({static_cast<uint8_t>(*p), epoch});
}
static void pump() { protocol_poll_client(CLIENT_TELNET); }
static bool executed_nonempty_other_than(const char* expected) {
    for (size_t i = 0; i < executed.size(); ++i) {
        if (!executed[i].empty() && executed[i] != expected) return true;
    }
    return false;
}
static void many(uint8_t client, int count, char c, uint32_t epoch) {
    for (int i = 0; i < count; ++i) queues[client].push_back({static_cast<uint8_t>(c), epoch});
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const int scenario = std::atoi(argv[1]);
    if (scenario == 0) {  // 溢出后同一物理行后缀不得执行
        many(CLIENT_TELNET, 256, 'A', 1);
        enqueue(CLIENT_TELNET, "$I\n", 1);
        pump();
        assert(overflow_reports == 1);
        assert(!executed_nonempty_other_than(""));
        return 0;
    }
    if (scenario == 1) {  // 恰好由换行触发的溢出不得吞掉下一行
        many(CLIENT_TELNET, 255, 'A', 1);
        enqueue(CLIENT_TELNET, "\n$I\n", 1);
        pump();
        assert(overflow_reports == 1);
        assert(!executed.empty() && executed.back() == "$I");
        return 0;
    }
    if (scenario == 2) {  // 254 字节合法行保持可执行
        many(CLIENT_TELNET, 254, 'A', 1);
        enqueue(CLIENT_TELNET, "\n", 1);
        pump();
        assert(overflow_reports == 0);
        assert(executed.size() == 1 && executed[0].size() == 254);
        return 0;
    }
    if (scenario == 3) {  // 255 字节 + 换行仍按原契约溢出
        many(CLIENT_TELNET, 255, 'A', 1);
        enqueue(CLIENT_TELNET, "\n", 1);
        pump();
        assert(overflow_reports == 1);
        assert(!executed_nonempty_other_than(""));
        return 0;
    }
    if (scenario == 4) {  // 丢弃期含退格
        many(CLIENT_TELNET, 256, 'A', 1);
        enqueue(CLIENT_TELNET, "\b$I\n", 1);
        pump();
        assert(overflow_reports == 1);
        assert(!executed_nonempty_other_than(""));
        return 0;
    }
    if (scenario == 5) {  // 超长行只报一次，丢弃到 EOL 后恢复
        many(CLIENT_TELNET, 256, 'A', 1);
        many(CLIENT_TELNET, 300, 'B', 1);
        enqueue(CLIENT_TELNET, "\n$I\n", 1);
        pump();
        assert(overflow_reports == 1);
        assert(!executed.empty() && executed.back() == "$I");
        assert(!executed_nonempty_other_than("$I"));
        return 0;
    }
    if (scenario == 6) {  // 新 Telnet 会话不得拼上旧半行
        enqueue(CLIENT_TELNET, "$System/", 1);
        pump();
        queues[CLIENT_TELNET].clear();  // 网络层失效时已排空旧队列
        enqueue(CLIENT_TELNET, "IP\n", 2);
        pump();
        assert(executed.size() == 1 && executed[0] == "IP");
        return 0;
    }
    if (scenario == 7) {  // 新会话必须清掉旧会话的超长行丢弃状态
        many(CLIENT_TELNET, 256, 'A', 1);
        pump();
        queues[CLIENT_TELNET].clear();
        enqueue(CLIENT_TELNET, "$I\n", 2);
        pump();
        assert(executed.size() == 1 && executed[0] == "$I");
        return 0;
    }
    if (scenario == 8) {  // 软重置重新初始化行状态
        many(CLIENT_TELNET, 256, 'A', 1);
        pump();
        empty_lines();
        enqueue(CLIENT_TELNET, "$I\n", 1);
        pump();
        assert(executed.size() == 1 && executed[0] == "$I");
        return 0;
    }
    if (scenario == 9) {  // 丢弃期被 CR 结束后恢复，CRLF 原语义保留
        many(CLIENT_TELNET, 256, 'A', 1);
        enqueue(CLIENT_TELNET, "\r\n$I\n", 1);
        pump();
        assert(overflow_reports == 1);
        assert(!executed.empty() && executed.back() == "$I");
        assert(!executed_nonempty_other_than("$I"));
        return 0;
    }
    return 2;
}
'''
        cls.exe = cls.build('protocol_boundary', program)

    def test_overflow_suffix_is_not_executed(self): self.run_case(self.exe, 0)
    def test_overflow_at_newline_keeps_next_line(self): self.run_case(self.exe, 1)
    def test_254_byte_line_still_executes(self): self.run_case(self.exe, 2)
    def test_255_byte_line_with_eol_still_overflows(self): self.run_case(self.exe, 3)
    def test_backspace_during_discard_is_ignored(self): self.run_case(self.exe, 4)
    def test_one_overflow_report_until_eol(self): self.run_case(self.exe, 5)
    def test_new_session_drops_old_partial_line(self): self.run_case(self.exe, 6)
    def test_new_session_clears_discard_state(self): self.run_case(self.exe, 7)
    def test_soft_reset_clears_discard_state(self): self.run_case(self.exe, 8)
    def test_cr_terminates_discard_without_swallowing_next_line(self): self.run_case(self.exe, 9)


class SerialSessionQueueTest(HostBuild):
    """跨任务窗口：旧代次字节即使已读出网络环，也不得进入新队列。"""
    @classmethod
    def setUpClass(cls):
        super().setUpClass()
        source = (ROOT / 'Grbl_Esp32/src/Serial.cpp').read_text(encoding='utf-8')
        accept = function_body(source, 'bool client_accept_byte(', required=False)
        legacy = r'''
static bool client_accept_byte(uint8_t client, uint8_t data, uint32_t) {
    return client_buffer[client].write(data) != 0;
}
'''
        program = r'''
#include <cassert>
#include <cstdint>
#include <vector>
#define CLIENT_SERIAL 0
#define CLIENT_TELNET 3
struct InputBuffer {
    std::vector<uint8_t> bytes;
    size_t write(uint8_t c) { bytes.push_back(c); return 1; }
    void begin() { bytes.clear(); }
};
static InputBuffer client_buffer[5];
static uint32_t client_session_generation[5] __attribute__((unused)) = {};
static int myMutex __attribute__((unused)) = 0;
#define vTaskEnterCritical(x) ((void)(x))
#define vTaskExitCritical(x) ((void)(x))
''' + (accept or legacy) + r'''
int main(int argc, char** argv) {
    assert(argc == 2);
    const int scenario = std::atoi(argv[1]);
    if (scenario == 0) {
        client_session_generation[CLIENT_TELNET] = 5;
        assert(client_accept_byte(CLIENT_TELNET, 'A', 5));
        assert(client_buffer[CLIENT_TELNET].bytes.size() == 1);
        return 0;
    }
    if (scenario == 1) {
        client_session_generation[CLIENT_TELNET] = 5;
        assert(!client_accept_byte(CLIENT_TELNET, 'A', 4));
        assert(client_buffer[CLIENT_TELNET].bytes.empty());
        return 0;
    }
    if (scenario == 2) {
        client_session_generation[CLIENT_TELNET] = 6;
        assert(client_accept_byte(CLIENT_SERIAL, 'A', 5));
        assert(client_buffer[CLIENT_SERIAL].bytes.size() == 1);
        return 0;
    }
    if (scenario == 3) {
        client_session_generation[CLIENT_TELNET] = 7;
        assert(client_accept_byte(CLIENT_TELNET, 'B', 7));
        assert(!client_accept_byte(CLIENT_TELNET, 'A', 6));
        assert(client_buffer[CLIENT_TELNET].bytes.size() == 1);
        assert(client_buffer[CLIENT_TELNET].bytes[0] == 'B');
        return 0;
    }
    return 2;
}
'''
        cls.exe = cls.build('serial_session_queue', program)

    def test_current_epoch_enqueues(self): self.run_case(self.exe, 0)
    def test_stale_epoch_is_dropped(self): self.run_case(self.exe, 1)
    def test_non_telnet_ignores_epoch(self): self.run_case(self.exe, 2)
    def test_stale_byte_after_replacement_is_dropped(self): self.run_case(self.exe, 3)


if __name__ == '__main__':
    unittest.main()
