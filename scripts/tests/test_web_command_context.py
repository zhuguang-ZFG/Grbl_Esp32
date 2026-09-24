"""编译当前真实函数，硬件接口替身仅用于确定性交错回归。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

def function(source, signature):
    start = source.index(signature)
    left = source.index("{", start)
    depth, right = 1, left + 1
    while depth:
        depth += (source[right] == "{") - (source[right] == "}")
        right += 1
    return source[start:right]

def compile_run(program):
    compiler = os.environ.get("CXX") or shutil.which("g++")
    if not compiler:
        raise RuntimeError("需要 host C++ 编译器，不跳过关键并发回归")
    env = dict(os.environ)
    env["PATH"] = str(Path(compiler).parent) + os.pathsep + env.get("PATH", "")
    with tempfile.TemporaryDirectory() as directory:
        cpp = Path(directory) / "probe.cpp"
        exe = Path(directory) / ("probe.exe" if os.name == "nt" else "probe")
        cpp.write_text(program, encoding="utf-8")
        built = subprocess.run([compiler, "-std=c++17", "-pthread", "-I", str(ROOT), str(cpp), "-o", str(exe)],
                               capture_output=True, text=True, env=env, timeout=60)
        assert built.returncode == 0, built.stderr
        result = subprocess.run([str(exe)], capture_output=True, text=True, env=env, timeout=10)
        assert result.returncode == 0, result.stderr

PROGRAM = r'''
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <condition_variable>
#include <string>
#include <thread>
enum class Error { Ok, AnotherInterfaceBusy };
using TaskHandle_t = std::thread::id;
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) (p)->lock()
#define portEXIT_CRITICAL(p) (p)->unlock()
#define configASSERT(x) assert(x)
TaskHandle_t xTaskGetCurrentTaskHandle() { return std::this_thread::get_id(); }
namespace WebUI {
enum class AuthenticationLevel { User };
struct ESPResponseStream {
    std::string output;
    void print(const char* s) { output += s; }
};
@@CONTEXT@@
@@PRINTER@@
}
struct WebCommand {
    std::function<bool()> _cmdChecker;
    std::function<Error(char*, WebUI::AuthenticationLevel)> _action;
    Error action(char*, WebUI::AuthenticationLevel, WebUI::ESPResponseStream*);
};
@@ACTION@@

int main() {
    std::mutex mutex;
    std::condition_variable condition;
    bool a_started = false, b_finished = false;
    WebUI::ESPResponseStream a, b;
    WebCommand first, second;
    first._action = [&](char*, auto) {
        std::unique_lock<std::mutex> lock(mutex);
        a_started = true;
        condition.notify_all();
        condition.wait(lock, [&] { return b_finished; });
        WebUI::webPrint("A_ONLY");
        return Error::Ok;
    };
    second._action = [&](char*, auto) {
        WebUI::webPrint("B_ONLY");
        return Error::Ok;
    };
    std::thread task([&] { first.action(nullptr, WebUI::AuthenticationLevel::User, &a); });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return a_started; });
        second.action(nullptr, WebUI::AuthenticationLevel::User, &b);
        b_finished = true;
    }
    condition.notify_all();
    task.join();
    assert(a.output == "A_ONLY" && b.output == "B_ONLY");
    {
        WebUI::WebCommandContext outer(&a);
        auto& ctx = WebUI::current_command_context();
        ctx.column = 12;
        ctx.parameters[0].key = const_cast<char*>("outer");
        {
            WebUI::WebCommandContext inner(&b);
            WebUI::current_command_context().column = 30;
            WebUI::current_command_context().parameters[0].key = const_cast<char*>("inner");
        }
        assert(&WebUI::current_command_context() == &ctx);
        assert(ctx.column == 12 && std::strcmp(ctx.parameters[0].key, "outer") == 0);
    }
    assert(WebUI::command_contexts == nullptr);
    
}
'''


class WebCommandContextTest(unittest.TestCase):
    def test_concurrent_response_and_nested_context(self):
        source = (ROOT / "Grbl_Esp32/src/WebUI/WebSettings.cpp").read_text(encoding="utf-8")
        start = source.index("    typedef struct {")
        end = source.index("    bool            split_params(", start)
        context = source[start:end]
        program = PROGRAM.replace("@@CONTEXT@@", context)
        program = program.replace("@@PRINTER@@", function(source, "static void webPrint(const char* s)"))
        program = program.replace("@@ACTION@@", function(source, "Error WebCommand::action("))
        compile_run(program)

if __name__ == "__main__":
    unittest.main()
