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

def compile_run(program, timeout=10):
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
                               capture_output=True, text=True, env=env, cwd=directory, timeout=60)
        assert built.returncode == 0, built.stderr
        try:
            result = subprocess.run([str(exe)], capture_output=True, text=True,
                                    env=env, cwd=directory, timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            raise AssertionError(f"host probe timed out after {timeout}s") from exc
        assert result.returncode == 0, (
            f"host probe exited {result.returncode} (0x{result.returncode & 0xffffffff:08x})\n"
            f"{result.stdout}{result.stderr}"
        )

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


RESPONSE_PROGRAM = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
using String = std::string;
using TaskHandle_t = std::thread::id;
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(p) (p)->lock()
#define portEXIT_CRITICAL(p) (p)->unlock()
#define configASSERT(x) assert(x)
TaskHandle_t xTaskGetCurrentTaskHandle() { return std::this_thread::get_id(); }
enum class Error { Ok, AnotherInterfaceBusy, InvalidValue };
@@CLIENTS@@
#define ENABLE_HTTP
#define ENABLE_WIFI
#define CONTENT_LENGTH_UNKNOWN (-1)
std::vector<std::pair<uint8_t, std::string>> sent;
void grbl_send(uint8_t client, const char* data) { sent.emplace_back(client, data); }
int http_calls = 0;
class WebServer {
public:
    std::string output;
    void setContentLength(int) { ++http_calls; }
    void sendHeader(const char*, const char*) { ++http_calls; }
    void send(int status) { assert(status == 200); ++http_calls; }
    void sendContent(const String& data) { ++http_calls; output += data; }
};
#include "Grbl_Esp32/src/WebUI/ESPResponse.h"
namespace WebUI {
enum class AuthenticationLevel { User };
@@RESPONSE@@
@@CONTEXT@@
@@HELPERS@@

std::vector<std::string> encoded;
bool json_pretty = false;
int json_completed = 0;
class JSONencoder {
    String text;
public:
    explicit JSONencoder(bool pretty) { json_pretty = pretty; }
    void begin() { text = "{"; }
    void begin_array(const char* name) { text += "\"" + String(name) + "\":["; }
    void entry(const char* name) {
        if (!encoded.empty()) text += ",";
        text += "\"" + String(name) + "\"";
        encoded.emplace_back(name);
    }
    void end_array() { text += "]"; }
    String end() { ++json_completed; return text + "}"; }
};
enum { WEBSET, WEBCMD, OTHER };
int help_names = 0;
struct Setting {
    inline static Setting* List = nullptr;
    int type;
    const char* alias;
    const char* name;
    const char* description;
    Setting* link;
    Setting* next() { return link; }
    int getType() { return type; }
    const char* getGrblName() { return alias; }
    const char* getName() { ++help_names; return name; }
    const char* getDescription() { return description; }
    void addWebui(JSONencoder* j) { j->entry(name); }
};
struct Command {
    inline static Command* List = nullptr;
    int type;
    const char* alias;
    const char* name;
    const char* description;
    Command* link;
    Command* next() { return link; }
    int getType() { return type; }
    const char* getGrblName() { return alias; }
    const char* getName() { ++help_names; return name; }
    const char* getDescription() { return description; }
};
@@QUERIES@@
}
#undef espresponse
#undef params
#undef webColumn
struct WebCommand {
    std::function<bool()> _cmdChecker;
    std::function<Error(char*, WebUI::AuthenticationLevel)> _action;
    Error action(char*, WebUI::AuthenticationLevel, WebUI::ESPResponseStream*);
};
@@ACTION@@

void assert_silent() { assert(sent.empty()); assert(http_calls == 0); }
std::string client_output(uint8_t client) {
    std::string output;
    for (const auto& part : sent) {
        assert(part.first == client);
        output += part.second;
    }
    return output;
}
const std::string expected_help =
    "Persistent web settings - $name to show, $name=value to set\r\n"
    "ESPname FullName         Description\r\n"
    "------- --------         -----------\r\n"
    " ESP100 WiFi/Mode        Radio mode\r\n"
    "        Host/Name        Network name\r\n"
    "\r\n"
    "Other web commands: $name to show, $name=value to set\r\n"
    "ESPname FullName         Values\r\n"
    "------- --------         ------\r\n"
    " ESP0   Web/Help         Show commands\r\n"
    "        No/Args\r\n";

int main() {
#ifdef _WIN32
    // 崩溃必须直接结束子进程，不能弹窗阻塞回归。
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    using namespace WebUI;
    constexpr auto user = AuthenticationLevel::User;
    Setting hidden_setting{OTHER, "0", "Hidden/Setting", "Hidden", nullptr};
    Setting hostname{WEBSET, nullptr, "Host/Name", "Network name", &hidden_setting};
    Setting mode{WEBSET, "ESP100", "WiFi/Mode", "Radio mode", &hostname};
    Setting::List = &mode;
    Command hidden_command{OTHER, "H", "Hidden/Command", "Hidden", nullptr};
    Command bare{WEBCMD, nullptr, "No/Args", nullptr, &hidden_command};
    Command named{WEBCMD, "ESP0", "Web/Help", "Show commands", &bare};
    Command::List = &named;
    WebCommand help{{}, showWebHelp};
    WebCommand settings{{}, listSettings};
    @@BODY@@
    assert(command_contexts == nullptr);
}
'''


def response_program(body):
    source = (ROOT / "Grbl_Esp32/src/WebUI/WebSettings.cpp").read_text(encoding="utf-8")
    response = (ROOT / "Grbl_Esp32/src/WebUI/ESPResponse.cpp").read_text(encoding="utf-8")
    report = (ROOT / "Grbl_Esp32/src/Report.h").read_text(encoding="utf-8")
    start = source.index("    typedef struct {")
    end = source.index("    bool            split_params(", start)
    replacements = {
        "CLIENTS": "\n".join(line for line in report.splitlines() if line.startswith("#define CLIENT_")),
        "RESPONSE": "\n".join(function(response, signature) for signature in (
            "ESPResponseStream::ESPResponseStream()",
            "ESPResponseStream::ESPResponseStream(uint8_t client, bool byid)",
            "ESPResponseStream::ESPResponseStream(WebServer* webserver)",
            "void ESPResponseStream::print(const char* data)",
            "void ESPResponseStream::println(const char* data)",
            "void ESPResponseStream::flush()",
        )),
        "CONTEXT": source[start:end],
        "HELPERS": "\n".join(function(source, signature) for signature in (
            "static void webPrint(const char* s)",
            "static void webPrintSetColumn(int column)",
            "static void webPrint(String s)",
            "static void webPrint(const char* s1, const char* s2)",
            "static void webPrintln(const char* s)",
        )),
        "QUERIES": "\n".join(function(source, signature) for signature in (
            "static Error showWebHelp(", "static Error listSettings(",
        )),
        "ACTION": function(source, "Error WebCommand::action("),
        "BODY": body,
    }
    program = RESPONSE_PROGRAM
    for name, code in replacements.items():
        program = program.replace(f"@@{name}@@", code)
    return program


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

    def test_default_response_discards_output_but_advances_columns(self):
        compile_run(response_program(r'''
    ESPResponseStream discarded;
    assert(discarded.client() == CLIENT_INPUT);
    discarded.print(std::string(2000, 'x').c_str());
    discarded.println("discarded");
    {
        WebCommandContext context(&discarded);
        webPrint("abc");
        webPrintSetColumn(25);
        assert(current_command_context().column == 25);
        webPrintln("end");
        assert(current_command_context().column == 0);
    }
    discarded.flush();
    assert(!discarded.anyOutput());
    assert_silent();
'''), timeout=3)

    def test_silent_help_returns_without_output(self):
        compile_run(response_program(r'''
    assert(help.action(nullptr, user, nullptr) == Error::Ok);
    assert(help_names == 4);
    assert_silent();
'''), timeout=3)

    def test_silent_settings_returns_without_output(self):
        compile_run(response_program(r'''
    assert(settings.action(nullptr, user, nullptr) == Error::Ok);
    assert((encoded == std::vector<std::string>{"WiFi/Mode", "Host/Name"}));
    assert(json_completed == 1 && json_pretty);
    assert_silent();
'''), timeout=3)

    def test_normal_help_remains_formatted(self):
        compile_run(response_program(r'''
    WebServer server;
    ESPResponseStream http(&server), telnet(CLIENT_TELNET);
    for (auto* out : {&http, &telnet}) {
        help_names = 0;
        http_calls = 0;
        sent.clear();
        assert(help.action(nullptr, user, out) == Error::Ok);
        assert(help_names == 4);
        out->flush();
        if (out == &http) {
            assert(server.output == expected_help);
            assert(http_calls > 0 && sent.empty());
        } else {
            assert(client_output(CLIENT_TELNET) == expected_help);
            assert(http_calls == 0);
        }
    }
'''), timeout=3)

    def test_normal_settings_keeps_response_and_filters_registry(self):
        compile_run(response_program(r'''
    WebServer server;
    ESPResponseStream http(&server), telnet(CLIENT_TELNET);
    for (auto* out : {&http, &telnet}) {
        encoded.clear();
        json_completed = 0;
        http_calls = 0;
        sent.clear();
        assert(settings.action(nullptr, user, out) == Error::Ok);
        assert((encoded == std::vector<std::string>{"WiFi/Mode", "Host/Name"}));
        assert(json_completed == 1);
        const std::string expected = "{\"EEPROM\":[\"WiFi/Mode\",\"Host/Name\"]}";
        out->flush();
        if (out == &http) {
            assert(!json_pretty && server.output == expected);
            assert(http_calls > 0 && sent.empty());
        } else {
            assert(json_pretty && client_output(CLIENT_TELNET) == expected);
            assert(http_calls == 0);
        }
    }
'''), timeout=3)

    def test_nested_silent_restores_outer_output_and_state(self):
        compile_run(response_program(r'''
    ESPResponseStream out(CLIENT_TELNET);
    WebCommand inner{{}, [&](char* value, auto auth) {
        assert(value && *value == '\0' && auth == user);
        auto& nested = current_command_context();
        assert(nested.column == 0 && nested.parameters[0].key == nullptr);
        nested.parameters[0].key = const_cast<char*>("inner");
        assert(showWebHelp(value, auth) == Error::Ok);
        assert(listSettings(value, auth) == Error::Ok);
        return Error::InvalidValue;
    }};
    WebCommand outer{{}, [&](char*, auto) {
        auto& context = current_command_context();
        assert(context.response == &out);
        context.parameters[0].key = const_cast<char*>("outer");
        context.parameters[0].value = const_cast<char*>("preserved");
        webPrint("OUTER");
        const auto before = sent;
        assert(inner.action(nullptr, user, nullptr) == Error::InvalidValue);
        assert(help_names == 4 && json_completed == 1);
        assert((encoded == std::vector<std::string>{"WiFi/Mode", "Host/Name"}));
        assert(sent == before && http_calls == 0);
        assert(&current_command_context() == &context);
        assert(context.response == &out && context.column == 5);
        assert(std::strcmp(context.parameters[0].key, "outer") == 0);
        assert(std::strcmp(context.parameters[0].value, "preserved") == 0);
        webPrintSetColumn(13);
        webPrintln("RESTORED");
        return Error::Ok;
    }};
    assert(outer.action(nullptr, user, &out) == Error::Ok);
    assert(client_output(CLIENT_TELNET) == "OUTER        RESTORED\r\n");
    assert(http_calls == 0);
'''), timeout=3)

    def test_checker_early_return_preserves_outer_context(self):
        compile_run(response_program(r'''
    WebServer server;
    ESPResponseStream http(&server), out(CLIENT_TELNET);
    int checked = 0, called = 0;
    WebCommand guarded{{}, [&](char*, auto) { ++called; return Error::Ok; }};
    WebCommand outer{{}, [&](char* value, auto auth) {
        assert(std::strcmp(value, "request") == 0 && auth == user);
        auto& context = current_command_context();
        context.column = 7;
        context.parameters[0].key = const_cast<char*>("outer");
        guarded._cmdChecker = [&] {
            ++checked;
            assert(&current_command_context() == &context);
            assert(command_contexts == &context);
            return true;
        };
        assert(guarded.action(nullptr, user, nullptr) == Error::AnotherInterfaceBusy);
        assert(guarded.action(nullptr, user, &http) == Error::AnotherInterfaceBusy);
        assert(called == 0 && checked == 2);
        assert(&current_command_context() == &context);
        assert(context.response == &out && context.column == 7);
        assert(std::strcmp(context.parameters[0].key, "outer") == 0);
        assert_silent();
        webPrint("AFTER_CHECK");
        return Error::Ok;
    }};
    char request[] = "request";
    assert(outer.action(request, user, &out) == Error::Ok);
    assert(client_output(CLIENT_TELNET) == "AFTER_CHECK");
    assert(http_calls == 0 && server.output.empty());
    guarded._cmdChecker = [&] {
        ++checked;
        assert(command_contexts == nullptr);
        return true;
    };
    assert(guarded.action(nullptr, user, nullptr) == Error::AnotherInterfaceBusy);
    assert(checked == 3 && called == 0);
'''), timeout=3)

if __name__ == "__main__":
    unittest.main()
