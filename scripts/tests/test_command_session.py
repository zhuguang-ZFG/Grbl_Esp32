"""真实Serial/Protocol提交点回归，执行时不持网络或自旋锁。"""
import importlib.util,json,os,subprocess,shutil,tempfile,unittest
from pathlib import Path
ROOT=Path(os.environ.get('GRBL_ROOT',Path(__file__).resolve().parents[2]))
def module(name,file):
 spec=importlib.util.spec_from_file_location(name,file);result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result);return result
def function(raw,signature):
 start=raw.index(signature);left=raw.index('{',start);depth,end=1,left+1
 while depth:depth+=(raw[end]=='{')-(raw[end]=='}');end+=1
 return raw[start:end]
def run(name,source,scenarios):return source
def program():
    repo = ROOT
    original = os.environ.get('GRBL_ROOT')
    os.environ['GRBL_ROOT'] = str(repo)
    fixture = module('protocol', repo / 'scripts/tests/test_protocol_overflow.py')
    source = fixture.program().split('int main(')[0]
    protocol=(repo/'Grbl_Esp32/src/Protocol.cpp').read_text(encoding='utf-8')
    guard=protocol[protocol.index('class ClientCommandOwner {'):protocol.index('Error execute_line(')]
    source=source.replace('bool client_claim_command(uint8_t,uint32_t){return true;}void client_release_command(uint8_t){}', 'bool client_claim_command(uint8_t,uint32_t);void client_release_command(uint8_t);')
    if original is None:
        del os.environ['GRBL_ROOT']
    else:
        os.environ['GRBL_ROOT'] = original
    serial = (repo / 'Grbl_Esp32/src/Serial.cpp').read_text(encoding='utf-8')
    source = source.replace('int client_read(uint8_t client,uint32_t* session=nullptr){if(session)*session=0;return client==active_client&&cursor<input.size()?input[cursor++]:-1;}', 'int client_read(uint8_t,uint32_t* session=nullptr);\nvoid protocol_execute_realtime();')
    source = source.replace('void protocol_execute_realtime(){}', 'void protocol_execute_realtime();')
    source=source.replace('Error gc_execute_line(char* line,uint8_t){commands.emplace_back(line);return Error::Ok;}', 'Error gc_execute_line(char*,uint8_t);')
    source += r'''
#include <deque>
constexpr int CLIENT_TELNET=0;
struct Buffer {std::deque<int> q;void begin(){q.clear();}void write(int c){q.push_back(c);}int read(){if(q.empty())return -1;int c=q.front();q.pop_front();return c;}};
Buffer client_buffer[CLIENT_COUNT];
uint32_t client_session_generation[CLIENT_COUNT]={},client_session_seen[CLIENT_COUNT]={};
uint32_t client_command_owners[CLIENT_COUNT]={};int critical_depth=0;
int myMutex;void vTaskEnterCritical(int*){++critical_depth;}void vTaskExitCritical(int*){--critical_depth;}
'''
    source += '\n'+function(serial,'bool client_claim_command(')+function(serial,'void client_release_command(')
    source += '\n'.join(function(serial, s) for s in ('uint32_t client_begin_session(', 'void client_write_session(', 'int client_read('))
    source += r'''
int mode=0;bool invalidate=false;uint32_t before=0;
void protocol_execute_realtime(){
 if(invalidate){invalidate=false;client_begin_session(0);}
}
Error gc_execute_line(char* line,uint8_t){assert(critical_depth==0);if(mode==3)client_begin_session(0);commands.emplace_back(line);return Error::Ok;}
int main(int argc,char**argv){
 mode=argc>1?std::atoi(argv[1]):0;
 before=client_begin_session(0);pump();
 for(char c:std::string("G1 X10 F100\n"))client_write_session(mode==4?1:0,c,before);
 if(mode==1)invalidate=true;
 if(mode==2)client_begin_session(0);
 pump();
 std::cout<<"{\"scenario\":"<<mode<<",\"before\":"<<before<<",\"after\":"<<client_session_generation[0]
 <<",\"executed\":"<<commands.size()<<",\"line\":\""<<(commands.empty()?"":commands[0])<<"\"}";
 assert(commands.size()==((mode==1||mode==2)?0u:1u));assert(client_command_owners[0]==0);
}
'''
    return run('grbl-eol-session', source, [0, 1, 2])


class CommandSessionTest(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  compiler=os.environ.get('CXX') or shutil.which('g++')
  if not compiler:raise unittest.SkipTest('需要host C++编译器')
  cls.folder=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.folder.cleanup)
  cpp=Path(cls.folder.name)/'command.cpp';cpp.write_text(program(),encoding='utf-8');cls.exe=cpp.with_suffix('.exe' if os.name=='nt' else '')
  cls.env=dict(os.environ,PATH=str(Path(compiler).parent)+os.pathsep+os.environ.get('PATH',''))
  built=subprocess.run([compiler,'-std=c++17','-O0',str(cpp),'-o',str(cls.exe)],env=cls.env,capture_output=True,timeout=60)
  if built.returncode:raise AssertionError(built.stderr.decode('utf-8','replace'))
 def check(self,mode):
  ran=subprocess.run([str(self.exe),str(mode)],env=self.env,capture_output=True,timeout=10)
  self.assertEqual(ran.returncode,0,ran.stderr.decode('utf-8','replace'))
 def test_current_line_executes(self):self.check(0)
 def test_invalidation_after_eol_before_claim_rejects_line(self):self.check(1)
 def test_invalidation_before_eol_rejects_line(self):self.check(2)
 def test_invalidation_after_claim_finishes_owned_line(self):self.check(3)
 def test_serial_line_is_independent(self):self.check(4)
