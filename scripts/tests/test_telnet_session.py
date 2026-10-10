"""真实Serial队列和Protocol解析泵：跨连接半行、超长行与在途字节隔离。"""
import os,subprocess,tempfile,unittest
from pathlib import Path
from test_protocol_overflow import program,function
ROOT=Path(__file__).resolve().parents[2]

class TelnetSessionTest(unittest.TestCase):
    def test_session_boundaries(self):
        source=program().split('int main(')[0]
        source=source.replace('int client_read(uint8_t client){return client==active_client&&cursor<input.size()?input[cursor++]:-1;}','int client_read(uint8_t);')
        serial=(ROOT/'Grbl_Esp32/src/Serial.cpp').read_text(encoding='utf-8')
        methods='\n'.join(function(serial,s) for s in ('uint32_t client_begin_session(', 'bool client_session_current(', 'void client_write_session(', 'int client_read('))
        prefix=r"""
#include <deque>
#include <mutex>
constexpr int CLIENT_TELNET=0;
struct Buffer {std::deque<int> q;void begin(){q.clear();}void write(int c){q.push_back(c);}int read(){if(q.empty())return -1;int c=q.front();q.pop_front();return c;}};
Buffer client_buffer[2];
uint32_t client_session_generation[2]={},client_session_seen[2]={};
std::mutex myMutex;
void vTaskEnterCritical(std::mutex* p){p->lock();}
void vTaskExitCritical(std::mutex* p){p->unlock();}
"""
        source=source.replace('@@STRUCT@@','')+prefix+methods+r"""
void enqueue(const std::string& data,uint32_t session,int client=0){for(char c:data)client_write_session(client,c,session);}
int main(){
 for(int scenario=0;scenario<5;++scenario){
  empty_lines();commands.clear();errors=0;
  auto old=client_begin_session(0);pump();
  if(scenario==0 || scenario==3){enqueue("G1 X10 F100;",old);pump();}
  if(scenario==1){enqueue(std::string(300,'A'),old);pump();}
  if(scenario==2)enqueue("G1 X20 F100\n",old);
  auto fresh=client_begin_session(0);
  if(scenario==3)enqueue("G1 X99 F100\n",old);
  enqueue("$I\n",fresh);pump();
  assert(commands.size()==1 && commands.front()=="$I");
  assert(!client_session_current(0,old) && client_session_current(0,fresh));
 }
 // 任意次数会话屏障都不能清掉独立串口的半行。
 commands.clear();enqueue("G1 X1",0,1);pump();
 client_begin_session(0);client_begin_session(0);enqueue(" F100\n",0,1);pump();
 assert(commands.size()==1 && commands[0]=="G1 X1 F100");
}
"""
        compiler=os.environ['CXX']
        with tempfile.TemporaryDirectory() as d:
            cpp=Path(d)/'case.cpp';cpp.write_text(source,encoding='utf-8');exe=cpp.with_suffix('.exe')
            r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,timeout=60)
            self.assertEqual(r.returncode,0,r.stderr.decode('utf-8','replace'))
            r=subprocess.run([str(exe)],capture_output=True,timeout=10)
            self.assertEqual(r.returncode,0,r.stderr.decode('utf-8','replace'))
