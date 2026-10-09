"""编译真实产品函数，检查输入/会话边界；仅外部I/O由宿主替身代替。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

def function(source, signature):
    start=source.index(signature); left=source.index('{',start); right=left+1; depth=1
    while depth:
        depth+=(source[right]=='{')-(source[right]=='}'); right+=1
    return source[start:right]
STUB=r'''
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
constexpr int CLIENT_COUNT=2,LINE_BUFFER_SIZE=256;
enum class Error {Ok,Eol,Overflow,SystemGcLock};
enum class State {Idle,Alarm,Jog};
struct {State state=State::Idle;bool abort=false;}sys;
namespace WebUI {enum class AuthenticationLevel{LEVEL_GUEST};}
@@STRUCT@@
std::vector<std::string> commands;
int errors=0;
std::string input;size_t cursor=0;
int active_client=0;
int client_read(uint8_t client){return client==active_client&&cursor<input.size()?input[cursor++]:-1;}
void protocol_execute_realtime(){}
void report_status_message(Error error,uint8_t){if(error==Error::Overflow)++errors;}
Error system_execute_line(char* line,uint8_t,WebUI::AuthenticationLevel){commands.emplace_back(line);return Error::Ok;}
Error gc_execute_line(char* line,uint8_t){commands.emplace_back(line);return Error::Ok;}
'''
MAIN=r'''
int main(int argc,char**argv){
    const int scenario=std::stoi(argv[1]);
    if(scenario==0)input="G1 X1 F100\n";
    if(scenario==1)input=std::string(LINE_BUFFER_SIZE,'A')+"G1 X10 F100\n";
    if(scenario==2)input=std::string(LINE_BUFFER_SIZE,'A')+"G1 X10 F100\nG1 X1 F100\n";
    if(scenario==3)input=std::string(LINE_BUFFER_SIZE-1,'A')+"\nG1 X1 F100\n";
    if(scenario==4)input=std::string(LINE_BUFFER_SIZE,'A')+"\bG1 X10 F100\r\nG1 X1 F100\n";
    if(scenario==5)input=std::string(LINE_BUFFER_SIZE*3,'A')+"\rG1 X1 F100\n";
    if(scenario==6)input=std::string(LINE_BUFFER_SIZE-2,'A')+"\n";
    if(scenario==7){
        for(int i=0;i<LINE_BUFFER_SIZE;++i){if(add_char_to_line('A',0)==Error::Overflow)empty_line(0);}
        input="G1 X1 F100\n";active_client=1;
    }
    if(scenario==8){
        for(int i=0;i<LINE_BUFFER_SIZE;++i){if(add_char_to_line('A',0)==Error::Overflow)empty_line(0);}
        empty_lines();input="G1 X1 F100\n";
    }
    pump();
    if(scenario==1)assert(commands.empty());
    else if(scenario==6){assert(errors==0&&commands.size()==1&&commands[0].size()==LINE_BUFFER_SIZE-2);}
    else {assert(commands.size()==1&&commands[0]=="G1 X1 F100");}
    if(scenario>=1&&scenario<=5)assert(errors==1);
}
'''
def program():
    root=Path(os.environ.get('GRBL_ROOT',Path(__file__).resolve().parents[2]))
    source=(root/'Grbl_Esp32/src/Protocol.cpp').read_text(encoding='utf-8')
    a=source.index('typedef struct {');b=source.index('static void empty_line',a)
    stub=STUB.replace('@@STRUCT@@',source[a:b])
    methods='\n'.join(function(source,n) for n in ('static void empty_line(', 'static void empty_lines(', 'Error add_char_to_line(', 'Error execute_line('))
    a=source.index('        for (client = 0; client < CLIENT_COUNT; client++) {',source.index('void protocol_main_loop()'))
    b=source.index('        // If there are no more characters',a)
    return stub+methods+'\nvoid pump(){int c;uint8_t client;char* line;\n'+source[a:b]+'}\n'+MAIN

class RegressionTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=os.environ.get('CXX') or shutil.which('g++')
        if not compiler:raise RuntimeError('需要host C++编译器')
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        path=Path(cls.temp.name)/'case.cpp';path.write_text(program(),encoding='utf-8')
        cls.exe=path.with_suffix('.exe' if os.name=='nt' else '')
        cls.env=dict(os.environ,PATH=str(Path(compiler).parent)+os.pathsep+os.environ.get('PATH',''))
        built=subprocess.run([compiler,'-std=c++17','-O0',str(path),'-o',str(cls.exe)],env=cls.env,capture_output=True,timeout=60)
        if built.returncode:raise AssertionError(built.stderr.decode('utf-8','replace'))
    def check(self,*args):
        result=subprocess.run([str(self.exe),*map(str,args)],env=self.env,capture_output=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr.decode('utf-8','replace'))

def case(scenario):
    def run(self):self.check(scenario)
    return run
for n,label in enumerate(('short','discard_tail','next_line','eol_overflow','backspace_crlf','long_cr','max_valid','client_isolation','reset')):
    setattr(RegressionTest,'test_'+label,case(n))
if __name__=='__main__':unittest.main()
