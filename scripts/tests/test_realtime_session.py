"""真实 Serial 分流与 Telnet 实时提交锁，覆盖检查后失效和提交锁内失效。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
ROOT=Path(os.environ.get('GRBL_ROOT',Path(__file__).resolve().parents[2]))
def function(raw,signature):
    start=raw.index(signature); left=raw.index('{',start); depth,end=1,left+1
    while depth:
        depth+=(raw[end]=='{')-(raw[end]=='}');end+=1
    return raw[start:end]
class RealtimeSessionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler=os.environ.get('CXX') or shutil.which('g++')
        if not cls.compiler: raise unittest.SkipTest('需要宿主 C++ 编译器')
        serial=(ROOT/'Grbl_Esp32/src/Serial.cpp').read_text(encoding='utf-8')
        telnet=(ROOT/'Grbl_Esp32/src/WebUI/TelnetServer.cpp').read_text(encoding='utf-8')
        mass='client_session_current(' in serial
        cls.folder=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.folder.cleanup)
        pre=r"""
#include <cassert>
#include <cstdint>
#include <cstdlib>
using TaskHandle_t=int;using UBaseType_t=unsigned;
enum class State{Idle,Cycle};enum class Cmd:uint8_t{CycleStart='~'};
struct{State state=State::Idle;}sys;
struct Buffer{void begin(){}bool write(uint8_t){return true;}};
Buffer client_buffer[5];uint32_t client_session_generation[5]={};int myMutex;
constexpr uint8_t CLIENT_TELNET=3,CLIENT_ALL=5;
bool locked=false;int scenario=0,dispatches=0;bool read_once=false;
void vTaskEnterCritical(int*){}void vTaskExitCritical(int*){}
uint32_t client_begin_session(uint8_t);
bool client_session_current(uint8_t,uint32_t);
struct TelnetClientLock{TelnetClientLock(){assert(!locked);locked=true;}
~TelnetClientLock(){locked=false;}};
namespace WebUI{
class Telnet_Server{public:bool _rx_active=true,_setupdone=true;uint32_t _rx_epoch=1,_session=1;
bool executeRealtimeForSession(uint8_t,uint32_t,void(*)(uint8_t,uint8_t));};
Telnet_Server telnet_server;
struct{void handle(){}}wifi_config;
namespace COMMANDS{void handle(){throw 1;}}}
void invalidate(){assert(!locked);client_begin_session(CLIENT_TELNET);
WebUI::telnet_server._rx_epoch=2;WebUI::telnet_server._session=2;}
bool is_realtime_command(uint8_t){if(scenario==0)invalidate();return true;}
void execute_realtime_command(Cmd,uint8_t){assert(locked || scenario==3);++dispatches;
if(scenario==2)assert(locked);}
int plan_get_block_buffer_available(){return 0;}void taskYIELD(){}void vTaskDelay(int){}
#define portTICK_RATE_MS 1
#define ENABLE_WIFI 1
#define ENABLE_TELNET 1
"""
        if mass:
            pre+=r"""
uint8_t getClientChar(uint8_t* data,uint32_t* epoch){if(read_once)return CLIENT_ALL;
read_once=true;*data='~';*epoch=1;return scenario==3?0:CLIENT_TELNET;}
"""
            methods=['uint32_t client_begin_session(', 'bool client_session_current(', 'void client_write_session(']
        else:
            pre+=r"""
uint8_t getClientChar(uint8_t* data,uint32_t& epoch){if(read_once)return CLIENT_ALL;
read_once=true;*data='~';epoch=1;return scenario==3?0:CLIENT_TELNET;}
"""
            methods=['uint32_t client_begin_session(', 'static bool client_byte_is_current(', 'static bool client_accept_byte(']
        program=pre+'\n'.join(function(serial,s) for s in methods)
        program+='\nnamespace WebUI{\n'+function(telnet,'bool Telnet_Server::executeRealtimeForSession(')+'\n}\n'
        program+='\n'+function(serial,'void clientCheckTask(')+r"""
int main(int argc,char**argv){assert(argc==2);scenario=std::atoi(argv[1]);
client_session_generation[CLIENT_TELNET]=1;
if(scenario==1){invalidate();assert(!WebUI::telnet_server.executeRealtimeForSession('~',1,[](uint8_t value,uint8_t client){execute_realtime_command(static_cast<Cmd>(value),client);}));}
else{try{clientCheckTask(nullptr);}catch(int){}
assert(dispatches==(scenario==0?0:1));}
assert(!locked);}
"""
        cpp=Path(cls.folder.name)/'probe.cpp';cpp.write_text(program,encoding='utf-8')
        cls.exe=cpp.with_suffix('.exe' if os.name=='nt' else '')
        cls.env=dict(os.environ,PATH=str(Path(cls.compiler).parent)+os.pathsep+os.environ.get('PATH',''))
        built=subprocess.run([cls.compiler,'-std=c++17',str(cpp),'-o',str(cls.exe)],env=cls.env,capture_output=True,timeout=60)
        if built.returncode:raise AssertionError(built.stderr.decode('utf-8','replace'))
    def check(self,scenario):
        run=subprocess.run([str(self.exe),str(scenario)],env=self.env,capture_output=True,timeout=10)
        self.assertEqual(run.returncode,0,run.stderr.decode('utf-8','replace'))
    def test_invalidation_after_serial_check_rejects_realtime(self):self.check(0)
    def test_already_stale_session_rejects_realtime(self):self.check(1)
    def test_current_session_holds_lock_through_submission(self):self.check(2)
    def test_non_telnet_realtime_remains_available(self):self.check(3)
if __name__=='__main__':unittest.main()
