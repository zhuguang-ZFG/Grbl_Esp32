"""真实HTTP handler、ESP解析器、StringSetting和S3凭据校验的配网链回归。"""
import os
import shutil
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

ROOT=Path(os.environ.get('GRBL_ROOT',Path(__file__).resolve().parents[2]))
S3=Path(os.environ.get('XIAOZHI_ROOT',ROOT.parent/'xiaozhi-esp32'))
sys.path.insert(0,str(ROOT/'scripts/tests'))
from test_web_command_bounds import PROGRAM, function

def program():
    web=(ROOT/'Grbl_Esp32/src/WebUI/WebServer.cpp').read_text(encoding='utf-8')
    process=(ROOT/'Grbl_Esp32/src/ProcessSettings.cpp').read_text(encoding='utf-8')
    settings=(ROOT/'Grbl_Esp32/src/Settings.cpp').read_text(encoding='utf-8')
    source=PROGRAM[:PROGRAM.index('int main(')]
    source=source.replace('Error{Ok,Invalid}', 'Error{Ok,Invalid,InvalidStatement,BadNumberFormat,NvsSetFailed}')
    a=source.index('Error system_execute_line(');b=source.index('struct Web_Server',a)
    source=source[:a]+'Error system_execute_line(char*,ESPResponseStream*,AuthenticationLevel);\n'+source[b:]
    source=source.replace('@@METHODS@@',function(web,'void Web_Server::_handle_web_command(')+'\n'+function(web,'String Web_Server::get_Splited_Value('))
    source+='\n#include "'+(S3/'main/boards/lichuang-dev/plotter_provision_core.h').as_posix()+'"\n'
    source+=r"""
std::string stored;
int nvs_erase_key(int,const char*){stored.clear();return 0;}
int nvs_set_str(int,const char*,const char* value){stored=value;return 0;}
struct StringSetting {
 size_t _minLength=1,_maxLength=64;int _handle=0;const char* _keyName="wifi";
 std::string _storedValue,_currentValue,_defaultValue;
 Error check(char*){return Error::Ok;}
 Error setStringValue(char*);
};
"""+function(settings,'Error StringSetting::setStringValue(')
    source+=r"""
void remove_password(char*,WebUI::AuthenticationLevel&){}
"""+function(process,'char* normalize_key(')+r"""
StringSetting setting;
Error do_command_or_setting(const char* key,char* value,WebUI::AuthenticationLevel,WebUI::ESPResponseStream*){
 if((std::string(key)!="ESP100"&&std::string(key)!="ESP101")||!value)return Error::Invalid;
 return setting.setStringValue(value);
}
"""+function(process,'Error system_execute_line(char* line, WebUI::ESPResponseStream* out, WebUI::AuthenticationLevel auth_level)').replace('Error system_execute_line(', 'Error WebUI::system_execute_line(')
    source+=r"""
std::string decode(const std::string& text){
 std::string result;
 for(size_t i=0;i<text.size();++i){if(text[i]=='%'){result+=static_cast<char>(std::stoi(text.substr(i+1,2),nullptr,16));i+=2;}else result+=text[i];}
 return result;
}
int main(int argc,char** argv){
 const int scenario=std::stoi(argv[1]);
 std::string ssid="Home Wifi",password="password123";
 if(scenario==1)ssid+=" ";
 if(scenario==2)password+=" ";
 if(scenario==3){ssid=" 家里网络 ";password=" password123 ";}
 if(scenario==4){ssid=std::string(31,'a')+" ";password=std::string(63,'p')+" ";}
 if(scenario==6)ssid="Home]Wifi";
 using namespace hutuji::provision;
 assert(ValidateHomeCredentials(ssid,password)==CredentialError::None);
 auto commands=BuildCommandSequence(ssid,password);
 HTTP http;WebUI::Web_Server::_webserver=&http;WebUI::Web_Server web;
 for(int i=0;i<2;++i){
  auto url=BuildCommandUrl(commands[i]);
  http.command=decode(url.substr(url.find("plain=")+6));
  if(scenario==5)http.command=" \t"+http.command;
  web._handle_web_command(true);
  assert(http.code==200);
  assert(stored==(i?password:ssid));
 }
}
"""
    return source

class WebCredentialsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory();cls.addClassCleanup(cls.temp.cleanup)
        cpp=Path(cls.temp.name)/'credentials.cpp';cpp.write_text(program(),encoding='utf-8')
        cls.exe=cpp.with_suffix('.exe');compiler=os.environ.get('CXX') or shutil.which('g++') or 'D:/zhugu-home/mingw64/mingw64/bin/g++.exe'
        cls.env=dict(os.environ,PATH=str(Path(compiler).parent)+os.pathsep+os.environ.get('PATH',''))
        result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(cls.exe)],capture_output=True,env=cls.env,timeout=60)
        assert result.returncode==0,result.stderr.decode('utf-8','replace')
    def check(self,n):
        result=subprocess.run([str(self.exe),str(n)],capture_output=True,env=self.env,timeout=10)
        self.assertEqual(result.returncode,0,result.stderr.decode('utf-8','replace'))

def case_method(n):
    def run(self):self.check(n)
    return run
for n,label in enumerate(('normal','ssid_tail','password_tail','utf8_spaces','max_length','command_prefix','ssid_right_bracket')):
    setattr(WebCredentialsTest,'test_'+label,case_method(n))
