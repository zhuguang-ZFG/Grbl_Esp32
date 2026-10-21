"""编译真实查询守卫和 action，忙态拒绝不得调用查询。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get("GRBL_ROOT", Path(__file__).resolve().parents[2]))


def function(source, signature):
    start = source.index(signature)
    left = source.index("{", start)
    depth, end = 1, left + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class QueryGuardTest(unittest.TestCase):
    def test_actual_action_rejects_cycle_hold_and_reads_idle(self):
        raw = (ROOT / "Grbl_Esp32/src/Settings.cpp").read_text(encoding="utf-8")
        source = r'''
#include <cassert>
enum class State{Idle,Cycle,Hold,Jog,Alarm};
enum class Error{Ok,IdleError};
namespace WebUI{enum class AuthenticationLevel{Admin};struct ESPResponseStream{};}
struct{State state;}sys;
int calls=0;
Error query(const char*,WebUI::AuthenticationLevel,WebUI::ESPResponseStream*){++calls;return Error::Ok;}
struct GrblCommand{
    bool (*_cmdChecker)();
    Error (*_action)(const char*,WebUI::AuthenticationLevel,WebUI::ESPResponseStream*);
    Error action(char*,WebUI::AuthenticationLevel,WebUI::ESPResponseStream*);
};
''' + function(raw, "bool notCycleOrHold()") + function(raw, "Error GrblCommand::action(") + r'''
int main(){
    GrblCommand command{notCycleOrHold,query};
    WebUI::ESPResponseStream stream;
    sys.state=State::Cycle;assert(command.action(nullptr,WebUI::AuthenticationLevel::Admin,&stream)==Error::IdleError);
    sys.state=State::Hold;assert(command.action(nullptr,WebUI::AuthenticationLevel::Admin,&stream)==Error::IdleError);
    assert(calls==0);
    sys.state=State::Idle;assert(command.action(nullptr,WebUI::AuthenticationLevel::Admin,&stream)==Error::Ok);
    assert(calls==1);
}
'''
        compiler = os.environ.get("CXX") or shutil.which("g++")
        if not compiler:
            self.skipTest("需要宿主 C++ 编译器")
        with tempfile.TemporaryDirectory() as folder:
            cpp = Path(folder) / "probe.cpp"
            cpp.write_text(source, encoding="utf-8")
            exe = cpp.with_suffix(".exe" if os.name == "nt" else "")
            env = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ.get("PATH", ""))
            built = subprocess.run([compiler,"-std=c++17",str(cpp),"-o",str(exe)], env=env, capture_output=True, timeout=60)
            self.assertEqual(built.returncode,0,built.stderr.decode("utf-8","replace"))
            ran = subprocess.run([str(exe)], env=env, capture_output=True, timeout=10)
            self.assertEqual(ran.returncode,0,ran.stderr.decode("utf-8","replace"))


if __name__ == "__main__":
    unittest.main()
