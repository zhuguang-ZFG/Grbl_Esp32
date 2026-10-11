"""编译真实Serial2Socket及Web服务收尾：解绑、剩余日志与并发析构。"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(os.environ.get("GRBL_ROOT",Path(__file__).resolve().parents[2]))

def function(text,signature):
    start=text.index(signature);left=text.index("{",start);end=left+1;depth=1
    while depth:
        depth+=(text[end]=="{")-(text[end]=="}");end+=1
    return text[start:end]

STUB=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <future>
#include <chrono>
#include <atomic>
#define ENABLE_WIFI
#define ENABLE_HTTP
#define ENABLE_SERIAL2SOCKET_OUT
#define ENABLE_SERIAL2SOCKET_IN
#define log_i(...) ((void)0)
#define configASSERT(x) assert(x)
using SemaphoreHandle_t=std::recursive_mutex*;
constexpr int portMAX_DELAY=0;
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(){return new std::recursive_mutex;}
void xSemaphoreTakeRecursive(SemaphoreHandle_t p,int){p->lock();}
void xSemaphoreGiveRecursive(SemaphoreHandle_t p){p->unlock();}
class Print {};
uint32_t clock_ms=0;
uint32_t millis(){return clock_ms;}
void delay(uint32_t ms){clock_ms+=ms;}
struct String:std::string{using std::string::string; explicit String(long n):std::string(std::to_string(n)){}; String(const std::string& s):std::string(s){}};
std::atomic<bool> bad{false},destroyed{false};
std::string received;
std::mutex mx;std::condition_variable cv;bool entered=false,released=false;int block_kind=0,total=0;
void block(int kind){if(block_kind!=kind)return;std::unique_lock<std::mutex> l(mx);entered=true;cv.notify_all();cv.wait(l,[]{return released;});}
struct WebSocketsServer {
 bool alive=true;
 ~WebSocketsServer(){alive=false;destroyed=true;}
 static void operator delete(void*) noexcept {} // 替身保留存储以观测析构后的非法使用。
 void broadcastBIN(uint8_t* data,size_t n){block(1);if(!this||!alive)bad=true;else{assert(n<=1200);total+=n;received.append((const char*)data,n);}}
 void broadcastTXT(const String&){}
 void sendTXT(long,const String&){block(3);if(!alive)bad=true;}
 void loop(){block(2);if(!alive)bad=true;}
};
struct FakeClient{int available(){return 0;}};
struct WebServer{void handleClient(){} FakeClient client(){return {};} void send(int,const char*,const char*){}};
namespace WebUI {
struct COMMANDS{static void wait(int){}};
class Web_Server {
public:
 static bool _setupdone;static long _id_connection;
 static WebSocketsServer* _socket_server;static WebServer* _webserver;
 void end();void handle();
 static void pushError(int code,const char* st,bool web_error,uint16_t timeout);
};
bool Web_Server::_setupdone=true;long Web_Server::_id_connection=0;
WebSocketsServer* Web_Server::_socket_server=nullptr;WebServer* Web_Server::_webserver=nullptr;
}
'''

MAIN=r'''
int main(int argc,char**argv){
 using namespace WebUI;
 int scenario=atoi(argv[1]);Web_Server web;
 auto* old=new WebSocketsServer;Web_Server::_socket_server=old;Serial2Socket.attachWS(old);
 if(scenario==0){Serial2Socket.write((const uint8_t*)"abc",3);web.end();Serial2Socket.write((const uint8_t*)"later",5);Serial2Socket.flush();assert(!bad&&total==0);}
 if(scenario==1){Serial2Socket.write((const uint8_t*)"old",3);Serial2Socket.detachWS();Serial2Socket.flush();assert(!bad);auto* next=new WebSocketsServer;Serial2Socket.attachWS(next);Serial2Socket.write((const uint8_t*)"new",3);Serial2Socket.flush();assert(total==3);}
 if(scenario==2||scenario==3){
  block_kind=scenario==2?1:2;
  if(scenario==2)Serial2Socket.write((const uint8_t*)"ok",2);
  std::thread writer([&]{if(scenario==2)Serial2Socket.flush();else web.handle();});
  {std::unique_lock<std::mutex> l(mx);assert(cv.wait_for(l,std::chrono::seconds(2),[]{return entered;}));}
  std::promise<void> started;
  auto closer=std::async(std::launch::async,[&]{started.set_value();web.end();});
  started.get_future().wait();
  bool blocked=closer.wait_for(std::chrono::milliseconds(80))==std::future_status::timeout;
  {std::lock_guard<std::mutex> l(mx);released=true;}cv.notify_all();writer.join();closer.get();
  assert(blocked&&!bad&&destroyed);
 }
 if(scenario==4){for(int i=0;i<20;++i){web.end();auto* next=new WebSocketsServer;Web_Server::_socket_server=next;Serial2Socket.attachWS(next);Serial2Socket.write((uint8_t)'x');Serial2Socket.flush();}assert(!bad&&total==20);}
 if(scenario==5){for(size_t n:{size_t(0),size_t(1),size_t(1199),size_t(1200),size_t(1201),size_t(1399),size_t(4803)}){
   std::string payload;for(size_t i=0;i<n;++i)payload.push_back(char(i%251));
   Serial2Socket.begin(0);received.clear();total=0;
   Serial2Socket.write((const uint8_t*)"prefix",6);
   assert(Serial2Socket.write((const uint8_t*)payload.data(),payload.size())==payload.size());
   Serial2Socket.flush();assert(!bad&&received=="prefix"+payload&&total==6+n);
 }}
 if(scenario==6){
  // 回归：pushError 必须在 WebSocketAccessGuard 下解 _socket_server->loop()，
  // 否则并发 end() 会 delete 掉对象导致 loop() 命中 !alive（UAF）。
  block_kind=2;
  std::thread writer([&]{Web_Server::pushError(1,"boom",false,5);});
  {std::unique_lock<std::mutex> l(mx);assert(cv.wait_for(l,std::chrono::seconds(2),[]{return entered;}));}
  std::promise<void> started;
  auto closer=std::async(std::launch::async,[&]{started.set_value();web.end();});
  started.get_future().wait();
  bool blocked=closer.wait_for(std::chrono::milliseconds(80))==std::future_status::timeout;
  {std::lock_guard<std::mutex> l(mx);released=true;}cv.notify_all();writer.join();closer.get();
  assert(blocked&&!bad&&destroyed);
 }
 return 0;
}
'''

class WebSocketLifecycleTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        base=ROOT/"Grbl_Esp32/src/WebUI"
        header=(base/"Serial2Socket.h").read_text(encoding="utf-8")
        cpp=(base/"Serial2Socket.cpp").read_text(encoding="utf-8")
        web=(base/"WebServer.cpp").read_text(encoding="utf-8")
        strip=lambda text:re.sub(r"(?m)^\s*#\s*(?:include|pragma)[^\n]*$","",text)
        program=STUB+strip(header)+strip(cpp)+"\nnamespace WebUI {\n"+function(web,"void Web_Server::end()")+function(web,"void Web_Server::handle()")+function(web,"void Web_Server::pushError(")+"\n}\n"+MAIN
        path=Path(cls.temp.name)/"test.cpp";path.write_text(program,encoding="utf-8")
        cls.exe=path.with_suffix(".exe" if os.name=="nt" else "")
        compiler=os.environ.get("CXX") or shutil.which("g++")
        built=subprocess.run([compiler,"-std=c++17","-O0","-pthread",str(path),"-o",str(cls.exe)],capture_output=True)
        assert built.returncode==0,built.stderr.decode("utf-8","replace")
    def check(self,scenario):
        result=subprocess.run([str(self.exe),str(scenario)],capture_output=True,timeout=8)
        self.assertEqual(result.returncode,0,result.stderr.decode("utf-8","replace"))
    def test_log_after_service_end(self):self.check(0)
    def test_detach_drops_pending_output(self):self.check(1)
    def test_end_waits_for_broadcast(self):self.check(2)
    def test_end_waits_for_loop(self):self.check(3)
    def test_repeated_service_recreation(self):self.check(4)
    def test_oversized_output_is_fully_sent(self):self.check(5)
    def test_end_waits_for_pusherror(self):self.check(6)

if __name__=="__main__":unittest.main()
