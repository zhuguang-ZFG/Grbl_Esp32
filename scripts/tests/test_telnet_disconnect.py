"""真实Telnet断连与队列屏障，WiFiClient布尔与实际1.0.4 SDK相同。"""
import os,subprocess,shutil,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def function(text,signature):
 start=text.index(signature);left=text.index('{',start);end=left+1;depth=1
 while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]

def program():
    telnet=(ROOT/'Grbl_Esp32/src/WebUI/TelnetServer.cpp').read_text(encoding='utf-8');serial=(ROOT/'Grbl_Esp32/src/Serial.cpp').read_text(encoding='utf-8')
    source=r'''
    #include <cstdint>
    #include <cstdio>
    #include <deque>
    #include <string>
    #include <cassert>
    constexpr int CLIENT_COUNT=2,CLIENT_TELNET=0;
    constexpr int MAX_TLNT_CLIENTS=1,TELNETRXBUFFERSIZE=1200;
    struct Buffer{std::deque<int>q;void begin(){q.clear();}void write(int v){q.push_back(v);}int read(){if(q.empty())return -1;int v=q.front();q.pop_front();return v;}};
    Buffer client_buffer[CLIENT_COUNT];uint32_t client_session_generation[CLIENT_COUNT]={},client_session_seen[CLIENT_COUNT]={};
    int myMutex;void vTaskEnterCritical(int*){}void vTaskExitCritical(int*){}
    struct WiFiClient{bool live=false;explicit operator bool(){return connected();}bool connected(){return live;}
    int available(){return 0;}int read(uint8_t*,int){return 0;}void stop(){live=false;}};
    struct TelnetClientLock{};
    namespace COMMANDS{void wait(int){}}
    struct WiFiServer{};
    class Telnet_Server{public:bool _setupdone=true;WiFiServer* _telnetserver;
    WiFiClient _telnetClients[MAX_TLNT_CLIENTS];uint32_t _session=0;
    uint8_t _RXbuffer[1200]={};
    int _RXbufferSize=0,_RXbufferpos=0;void clearClients(){}int available(){return _RXbufferSize;}
    int read(uint32_t* session);bool executeRealtimeForSession(uint8_t,uint32_t,void(*)(uint8_t,uint8_t));
    bool _rx_active=true;void reset_rx();
    void push(const uint8_t*,int){}void handle();};
    ''' + 'int client_read(uint8_t,uint32_t* session=nullptr);\n' + function(serial,'uint32_t client_begin_session(')+function(serial,'void client_write_session(')+function(serial,'int client_read(')+function(telnet,'void Telnet_Server::reset_rx()')+function(telnet,'void Telnet_Server::handle()')+function(telnet,'int Telnet_Server::read(')+function(telnet,'bool Telnet_Server::executeRealtimeForSession(')+r'''
    int dispatches=0;
    int main(){
     WiFiServer listener;Telnet_Server server;server._telnetserver=&listener;
     auto session=client_begin_session(0);server._session=session;
     assert(client_read(0)==-2);
     for(char c:std::string("$I\n"))client_write_session(0,c,session);
     server._RXbufferSize=4;server._RXbufferpos=2;server._RXbuffer[2]='~';
     server._telnetClients[0].live=false;server.handle();
     const int first=client_read(0);const int queued=client_buffer[0].q.size();
     uint32_t read_session=0;const int realtime=server.read(&read_session);
     server.executeRealtimeForSession(realtime,read_session,[](uint8_t,uint8_t){++dispatches;});
     std::printf("{\"session_before\":%u,\"session_after\":%u,\"barrier\":%s,\"first_byte\":%d,\"remaining_queue\":%d,\"network_ring_bytes\":%d,\"stale_realtime_dispatches\":%d}\n",session,client_session_generation[0],first==-2?"true":"false",first,queued,server._RXbufferSize,dispatches);
     assert(session==client_session_generation[0]&&first=='$'&&queued==2&&server._RXbufferSize==3&&dispatches==1);
    }
    '''
    begin=source.index('int main(){')
    source=source[:begin]+r"""
int main(){
 WiFiServer listener;Telnet_Server server;server._telnetserver=&listener;
 auto session=client_begin_session(0);server._session=session;server._rx_active=true;
 assert(client_read(0)==-2);
 for(char c:std::string("$I\n"))client_write_session(0,c,session);
 server._RXbufferSize=4;server._RXbufferpos=2;server._RXbuffer[2]='~';
 server._telnetClients[0].live=false;server.handle();
 assert(client_session_generation[0]!=session&&client_read(0)==-2);
 assert(client_buffer[0].q.empty()&&server._RXbufferSize==0);
 uint32_t read_session=0;assert(server.read(&read_session)==-1);
 assert(!server.executeRealtimeForSession('~',session,[](uint8_t,uint8_t){++dispatches;}));
 assert(!server.executeRealtimeForSession('~',read_session,[](uint8_t,uint8_t){++dispatches;}));
 assert(dispatches==0);
 const auto settled=client_session_generation[0];server.handle();assert(client_session_generation[0]==settled);
}
"""
    return source

class TelnetDisconnectTest(unittest.TestCase):
    def test_actual_disconnected_bool_clears_all_receivers_once(self):
        compiler=os.environ.get('CXX') or shutil.which('g++')
        if not compiler:self.skipTest('需要host C++编译器')
        with tempfile.TemporaryDirectory() as folder:
            cpp=Path(folder)/'disconnect.cpp';cpp.write_text(program(),encoding='utf-8')
            exe=cpp.with_suffix('.exe' if os.name=='nt' else '')
            env=dict(os.environ,PATH=str(Path(compiler).parent)+os.pathsep+os.environ.get('PATH',''))
            build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],env=env,capture_output=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stderr.decode('utf-8','replace'))
            run=subprocess.run([str(exe)],env=env,capture_output=True,timeout=10)
            self.assertEqual(run.returncode,0,run.stderr.decode('utf-8','replace'))
