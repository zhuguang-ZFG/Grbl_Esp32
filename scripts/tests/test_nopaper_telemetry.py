"""量产ESP901只提供纸路占位，电机与压纸必须明确未知。"""
import os,subprocess,tempfile,unittest
from pathlib import Path
from test_protocol_overflow import function
ROOT=Path(__file__).resolve().parents[2]
class TelemetryTest(unittest.TestCase):
 def test_unknown_motor_and_panel_cross_repo(self):
  source=(ROOT/'Grbl_Esp32/src/WebUI/WebSettings.cpp').read_text(encoding='utf-8')
  code=r"""
#include <string>
#include <cassert>
#include "D:/Users/xiaozhi-esp32/main/boards/lichuang-dev/hutuji_recovery_core.h"
enum class Error{Ok}; enum class AuthenticationLevel{Admin};
std::string reply;
void webPrintln(const char* s){reply=s;}
"""+function(source,'static Error nopaperStatusHandler(')+r"""
int main(){
 nopaperStatusHandler(nullptr,AuthenticationLevel::Admin);
 using namespace hutuji;
 auto motor=MotorEnState::On;auto panel=PanelHoldState::On;auto paper=PaperPresentState::Yes;
 assert(ParsePaperStatusFields(reply,paper,motor,panel));
 assert(motor==MotorEnState::Unknown && panel==PanelHoldState::Unknown && paper==PaperPresentState::No);
}
"""
  with tempfile.TemporaryDirectory() as d:
   cpp=Path(d)/'case.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code,encoding='utf-8')
   r=subprocess.run([os.environ['CXX'],'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,timeout=60)
   self.assertEqual(r.returncode,0,r.stderr.decode('utf-8','replace'))
   r=subprocess.run([str(exe)],capture_output=True,timeout=10)
   self.assertEqual(r.returncode,0,r.stderr.decode('utf-8','replace'))
