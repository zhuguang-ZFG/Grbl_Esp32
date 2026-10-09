"""编译当前真实函数，注入断流/时间交错；不连接或驱动设备。"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from test_web_command_context import function

ROOT = Path(__file__).resolve().parents[2]
PROGRAM = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <atomic>
#define grbl_send(...) ((void)0)
#define grbl_sendf(...) ((void)0)
using String=std::string;
using wifi_mode_t=int;
using WiFiEvent_t=int;
constexpr int WIFI_MODE_STA=1,WIFI_MODE_AP=2,ESP_WIFI_STA=1,WL_CONNECTED=3,DHCP_MODE=0;
constexpr int SYSTEM_EVENT_STA_GOT_IP=1,SYSTEM_EVENT_STA_DISCONNECTED=2,SYSTEM_EVENT_STA_LOST_IP=3;
uint32_t clock_ms=10000;
uint32_t millis(){return clock_ms;}
int restarts=0, retries=0,scenario=0,radio_mode=WIFI_MODE_STA,link_status=0,fallbacks=0;
void (*hook)()=nullptr;
struct ESPStub { void restart(){++restarts;} } ESP;
struct IPAddress { explicit IPAddress(int=0){} operator uint32_t(){return 0;} String toString(){return "0.0.0.0";} };
struct WiFiStub {
 int getMode() { if(hook){auto h=hook;hook=nullptr;h();} return radio_mode; }
 int status(){return link_status;}
 IPAddress localIP(){return IPAddress();}
 void disconnect(bool){}
 void config(IPAddress,IPAddress,IPAddress){}
 void begin(const char*,const char*){++retries;}
} WiFi;
struct Setting { int get(){return ESP_WIFI_STA;} } setting;
struct TextSetting { String get(){return "test";} } text_setting;
auto* wifi_radio_mode=&setting;
auto* wifi_sta_mode=&setting;
auto* wifi_sta_ip=&setting;
auto* wifi_sta_gateway=&setting;
auto* wifi_sta_netmask=&setting;
auto* wifi_sta_ssid=&text_setting;
auto* wifi_sta_password=&text_setting;
struct COMMANDS {static void wait(int){}};
struct Services {void handle(){}} wifi_services;
@@CONSTANTS@@
struct WiFiConfig {
 static uint32_t _sta_link_down_since_ms,_sta_last_retry_ms,_ap_fallback_last_retry_ms;
 static std::atomic<bool> _sta_link_recovered;
 static void handle(); static void WiFiEvent(WiFiEvent_t); static void begin(){++fallbacks;}
};
std::atomic<bool> WiFiConfig::_sta_link_recovered{false};
uint32_t WiFiConfig::_sta_link_down_since_ms=0;
uint32_t WiFiConfig::_sta_last_retry_ms=0;
uint32_t WiFiConfig::_ap_fallback_last_retry_ms=0;
@@METHODS@@
void disconnect_after_clock(){clock_ms+=1;WiFiConfig::WiFiEvent(SYSTEM_EVENT_STA_DISCONNECTED);}

int main(int argc,char**argv){
 scenario=atoi(argv[1]);
 switch(scenario){
 case 1: hook=disconnect_after_clock; break;
 case 2: WiFiConfig::_sta_link_down_since_ms=10000;clock_ms=610000;break;
 case 3: clock_ms=100000;WiFiConfig::handle();clock_ms+=14999;WiFiConfig::handle();break;
 case 4: clock_ms=100000;WiFiConfig::handle();clock_ms+=15000;break;
 case 5: clock_ms=100000;WiFiConfig::handle();clock_ms+=15000;WiFiConfig::handle();clock_ms+=29999;break;
 case 6: clock_ms=100000;WiFiConfig::handle();clock_ms+=15000;WiFiConfig::handle();clock_ms+=30000;break;
 case 7: WiFiConfig::_sta_link_down_since_ms=UINT32_MAX-999; WiFiConfig::_sta_last_retry_ms=UINT32_MAX-30999;clock_ms=14000;break;
 case 8: WiFiConfig::_sta_link_down_since_ms=10000;clock_ms=610000;WiFiConfig::WiFiEvent(SYSTEM_EVENT_STA_GOT_IP);break;
 case 9: wifi_radio_mode=nullptr;break;
 case 10: radio_mode=0;clock_ms=1000000;break;
 case 11: radio_mode=WIFI_MODE_AP;clock_ms=120000;break;
 }
 WiFiConfig::handle();
 printf("{\"restarts\":%d,\"retries\":%d,\"fallbacks\":%d}\n",restarts,retries,fallbacks);
}
'''
CASES = {'test_first_disconnect': (0, {'restarts': 0, 'retries': 0, 'fallbacks': 0}), 'test_event_after_clock_sample': (1, {'restarts': 0, 'retries': 0, 'fallbacks': 0}), 'test_ten_minute_restart': (2, {'restarts': 1, 'retries': 1, 'fallbacks': 0}), 'test_no_retry_before_15s': (3, {'restarts': 0, 'retries': 0, 'fallbacks': 0}), 'test_first_retry_at_15s': (4, {'restarts': 0, 'retries': 1, 'fallbacks': 0}), 'test_no_duplicate_before_30s': (5, {'restarts': 0, 'retries': 1, 'fallbacks': 0}), 'test_retry_after_30s': (6, {'restarts': 0, 'retries': 2, 'fallbacks': 0}), 'test_millis_wrap': (7, {'restarts': 0, 'retries': 1, 'fallbacks': 0}), 'test_recovered_then_disconnected': (8, {'restarts': 0, 'retries': 0, 'fallbacks': 0}), 'test_settings_not_ready': (9, {'restarts': 0, 'retries': 0, 'fallbacks': 0}), 'test_explicit_radio_off': (10, {'restarts': 0, 'retries': 0, 'fallbacks': 0}), 'test_ap_fallback_retry': (11, {'restarts': 0, 'retries': 0, 'fallbacks': 1})}

class RegressionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CXX") or shutil.which("g++")
        if not compiler:
            raise RuntimeError("需要host C++编译器")
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cpp = Path(cls.temp.name) / "probe.cpp"
        cls.exe = cpp.with_suffix(".exe" if os.name == "nt" else "")
        source = (ROOT / "Grbl_Esp32/src/WebUI/WifiConfig.cpp").read_text(encoding="utf-8")
        methods = function(source, "void WiFiConfig::WiFiEvent(") + "\n" + function(source, "void WiFiConfig::handle(")
        constants = "\n".join(re.findall(r"static const uint32_t STA_\w+\s*=\s*\d+;", source))
        program = PROGRAM.replace("@@METHODS@@", methods).replace("@@CONSTANTS@@", constants)
        cpp.write_text(program, encoding="utf-8")
        cls.env = dict(os.environ)
        cls.env["PATH"] = str(Path(compiler).parent) + os.pathsep + cls.env.get("PATH", "")
        built = subprocess.run([compiler, "-std=c++17", "-O0", str(cpp), "-o", str(cls.exe)],
                               capture_output=True, env=cls.env, timeout=60)
        if built.returncode:
            raise AssertionError(built.stderr.decode("utf-8", "replace"))

def case_test(case, expected):
    def run(self):
        proc = subprocess.run([str(self.exe), str(case)], capture_output=True,
                              env=self.env, timeout=10, check=True)
        result = json.loads(proc.stdout)
        for key, value in expected.items():
            self.assertEqual(result[key], value, (key, result))
    return run

for name, (case, expected) in CASES.items():
    setattr(RegressionTest, name, case_test(case, expected))

if __name__ == "__main__":
    unittest.main()
