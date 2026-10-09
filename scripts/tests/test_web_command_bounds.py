"""真实HTTP命令处理：长请求有界、ESP字符串完整、满队列不再追加。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

def function(source, signature):
    start=source.index(signature);left=source.index("{",start);right=left+1;depth=1
    while depth:
        depth+=(source[right]=="{")-(source[right]=="}");right+=1
    return source[start:right]

ROOT=Path(os.environ.get('GRBL_ROOT',Path(__file__).resolve().parents[2]))
PROGRAM=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
struct String:std::string {
    using std::string::string;
    String(const std::string& x):std::string(x){}
    void trim(){auto a=find_first_not_of(" \r\n\t");if(a==npos){clear();return;}auto b=find_last_not_of(" \r\n\t");*this=substr(a,b-a+1);}
    int indexOf(const char* s){auto p=find(s);return p==npos?-1:static_cast<int>(p);}
    char charAt(int i){return at(i);}
    String substring(int a,int b){return substr(a,b-a);}
    void remove(int a,int n){erase(a,n);}
    String& operator+=(int x){append(std::to_string(x));return *this;}
    using std::string::operator+=;
};
enum class Error{Ok,Invalid};
const char* errorString(Error){return "invalid";}
bool is_realtime_command(char c){return c=='?';}
struct HTTP {
    String command;int replies=0,code=0;String body;
    bool hasArg(const char* name){return std::string(name)=="commandText";}
    String arg(const char*){return command;}
    void send(int c,const char*,const String& text){++replies;code=c;body=text;}
};
bool has_terminator=true;int copied_length=0,parsed=0;
namespace WebUI {
enum class AuthenticationLevel{LEVEL_GUEST,LEVEL_USER};
struct ESPResponseStream {
    explicit ESPResponseStream(HTTP*){}
    bool anyOutput(){return false;}void flush(){}
};
struct SerialStub {
    int calls=0,capacity=1000;
    bool push(const char*){if(++calls>512)throw std::runtime_error("bounded probe stopped repeated input");return calls<=capacity;}
} Serial2Socket;
Error system_execute_line(char* line,ESPResponseStream*,AuthenticationLevel){
    // 真实解析器会strrchr/strlen；在同一边界先量256B内是否存在终止符，避免在探针里也越界。
    ++parsed;has_terminator=std::memchr(line,0,256)!=nullptr;
    copied_length=has_terminator?std::strlen(line):256;
    return Error::Ok;
}
struct Web_Server {
    static HTTP* _webserver;
    AuthenticationLevel is_authenticated(){return AuthenticationLevel::LEVEL_USER;}
    void _handle_web_command(bool);
    String get_Splited_Value(String,char,int);
};
HTTP* Web_Server::_webserver=nullptr;
@@METHODS@@
}

int main(int argc,char**argv){
    const int mode=std::stoi(argv[1]),n=std::stoi(argv[2]);HTTP http;WebUI::Web_Server::_webserver=&http;WebUI::Web_Server web;
    if(mode==0||mode==2){for(int i=0;i<n;++i)http.command+="?\n";}
    if(mode==1){http.command="[ESP420]";http.command+=std::string(n-8,'x');}
    if(mode==2)WebUI::Serial2Socket.capacity=2;
    if(mode==3)http.command=std::string(n,'G');
    if(mode==4)http.command="G1 X1\n\nG1 X2";
    web._handle_web_command(true);
    assert(http.replies==1);
    if(mode==0){assert(WebUI::Serial2Socket.calls==(n<=256?n:0));assert(http.code==(n<=256?200:413));}
    if(mode==1){assert(has_terminator);assert(parsed==(n<=255?1:0));assert(http.code==(n<=255?200:413));}
    if(mode==2){assert(WebUI::Serial2Socket.calls==3);assert(http.body=="Error");}
    if(mode==3){assert(http.code==413&&WebUI::Serial2Socket.calls==0);}
    if(mode==4){assert(WebUI::Serial2Socket.calls==1);}
}
'''

class WebCommandBoundsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=os.environ.get("CXX") or shutil.which("g++")
        if not compiler:raise unittest.SkipTest("需要host C++编译器")
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        source=(ROOT/"Grbl_Esp32/src/WebUI/WebServer.cpp").read_text(encoding="utf-8")
        methods=function(source,"void Web_Server::_handle_web_command(")+"\n"+function(source,"String Web_Server::get_Splited_Value(")
        path=Path(cls.temp.name)/"web.cpp";path.write_text(PROGRAM.replace("@@METHODS@@",methods),encoding="utf-8")
        cls.exe=path.with_suffix(".exe" if os.name=="nt" else "")
        cls.env=dict(os.environ,PATH=str(Path(compiler).parent)+os.pathsep+os.environ.get("PATH",""))
        built=subprocess.run([compiler,"-std=c++17","-O0","-ftrivial-auto-var-init=pattern",str(path),"-o",str(cls.exe)],capture_output=True,env=cls.env,timeout=60)
        if built.returncode:raise AssertionError(built.stderr.decode("utf-8","replace"))
    def check(self,mode,n):
        result=subprocess.run([str(self.exe),str(mode),str(n)],capture_output=True,env=self.env,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr.decode("utf-8","replace"))

def case_method(mode,n):
    def run(self):self.check(mode,n)
    return run

for count in (1,255,256,257):setattr(WebCommandBoundsTest,"test_lines_"+str(count),case_method(0,count))
for size in (20,254,255,256,300):setattr(WebCommandBoundsTest,"test_esp_bytes_"+str(size),case_method(1,size))
for mode,n,label in ((2,20,"full_queue"),(3,4097,"byte_budget"),(4,0,"blank_line_compatibility")):
    setattr(WebCommandBoundsTest,"test_"+label,case_method(mode,n))
if __name__=="__main__":unittest.main()
