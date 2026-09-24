"""真实服务初始化+SDK启动清零语义：新连接与重开都必须立即发送应答。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from test_telnet_receive import ROOT, function_body


class TelnetNoDelayTest(unittest.TestCase):
    def test_begin_and_reopen_preserve_nodelay_for_accepted_clients(self):
        compiler = os.environ.get('CXX') or shutil.which('g++')
        if not compiler:
            self.skipTest('需要host C++编译器')
        source = (ROOT / 'Grbl_Esp32/src/WebUI/TelnetServer.cpp').read_text(encoding='utf-8')
        program = r'''
#include <cassert>
#include <cstdint>
#include <string>
struct TelnetClientLock {};
struct String : std::string {
    String(const char* value) : std::string(value) {}
    String(const std::string& value) : std::string(value) {}
    explicit String(unsigned value) : std::string(std::to_string(value)) {}
};
struct Setting { int value; int get() const { return value; } };
Setting enabled{1}, port{23};
Setting* telnet_enable = &enabled;
Setting* telnet_port = &port;
constexpr int CLIENT_ALL = 0;
void grbl_send(int, char*) {}
struct WiFiClient { bool no_delay; };
struct WiFiServer {
    bool no_delay = false;
    WiFiServer(uint16_t, int) {}
    // arduino-esp32 1.0.4 WiFiServer::begin 最后无条件重置；available 将其传给TCP_NODELAY。
    void begin() { no_delay = false; }
    void setNoDelay(bool value) { no_delay = value; }
    WiFiClient available() const { return {no_delay}; }
};
struct Telnet_Server {
    static constexpr int MAX_TLNT_CLIENTS = 1;
    WiFiServer* _telnetserver = nullptr;
    bool _setupdone = false;
    int _RXbufferSize = 0, _RXbufferpos = 0;
    uint16_t _port = 0;
    void end() { delete _telnetserver; _telnetserver = nullptr; _setupdone = false; }
    bool begin();
};
''' + function_body(source, 'bool Telnet_Server::begin()') + r'''
int main() {
    Telnet_Server server;
    for (int attempt = 0; attempt < 2; ++attempt) {
        assert(server.begin());
        assert(server._setupdone);
        assert(server._telnetserver->available().no_delay);
    }
    enabled.value = 0;
    assert(!server.begin());
    assert(!server._setupdone && server._telnetserver == nullptr);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'nodelay.cpp'
            exe = Path(directory) / 'nodelay.exe'
            cpp.write_text(program, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stderr)
            env = dict(os.environ)
            env['PATH'] = str(Path(compiler).parent) + os.pathsep + env.get('PATH', '')
            run = subprocess.run([str(exe)], env=env, capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
