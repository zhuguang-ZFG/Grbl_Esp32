"""编译真实 Telnet 收包/关闭入口，注入短读与服务重开边界。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get('GRBL_ROOT', Path(__file__).resolve().parents[2]))


def function_body(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError('函数体未闭合')


class TelnetReceiveTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler:
            raise unittest.SkipTest('需要 host C++ 编译器')
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        stem = Path(cls.directory.name) / 'telnet_receive'
        source = (ROOT / 'Grbl_Esp32/src/WebUI/TelnetServer.cpp').read_text(encoding='utf-8')
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
class Telnet_Server {
public:
    static const int MAX_TLNT_CLIENTS = 1;
    static const int TELNETRXBUFFERSIZE = 1200;
    bool _setupdone = true;
    WiFiServer* _telnetserver = new WiFiServer;
    WiFiClient _telnetClients[MAX_TLNT_CLIENTS];
    int _RXbufferSize = 0, _RXbufferpos = 0;
    std::vector<uint8_t> pushed;
    void clearClients() {}
    int available() const { return _RXbufferSize; }
    bool push(const uint8_t* buffer, int length) {
        assert(length > 0 && length <= TELNETRXBUFFERSIZE - available());
        pushed.insert(pushed.end(), buffer, buffer + length);
        _RXbufferSize += length;
        return true;
    }
    void handle();
    void end();
};
''' + function_body(source, 'void Telnet_Server::handle()') + '\n' + function_body(source, 'void Telnet_Server::end()') + r'''
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
        stem.with_suffix('.cpp').write_text(program, encoding='utf-8')
        cls.exe = stem.with_suffix('.exe' if os.name == 'nt' else '')
        result = subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                                 str(stem.with_suffix('.cpp')), '-o', str(cls.exe)],
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stderr or result.stdout)
        cls.env = dict(os.environ)
        cls.env['PATH'] = str(Path(compiler).parent) + os.pathsep + cls.env.get('PATH', '')

    def run_case(self, scenario):
        result = subprocess.run([str(self.exe), str(scenario)], env=self.env,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)

    def test_full_read(self): self.run_case(0)
    def test_short_read_keeps_only_received_bytes(self): self.run_case(1)
    def test_zero_read_does_not_enqueue(self): self.run_case(2)
    def test_read_error_does_not_enqueue(self): self.run_case(3)
    def test_stack_buffer_limit(self): self.run_case(4)
    def test_ring_capacity_limit(self): self.run_case(5)
    def test_end_closes_existing_client_before_reopen(self): self.run_case(6)


if __name__ == '__main__':
    unittest.main()
