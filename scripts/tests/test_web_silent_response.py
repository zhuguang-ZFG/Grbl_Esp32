"""静默查询仍须有有效应答对象，且使用现有 CLIENT_INPUT 丢弃输出。"""
import os
from pathlib import Path
import unittest
import test_web_command_bounds as bounds

PROGRAM = bounds.PROGRAM

ROOT = Path(os.environ.get("GRBL_ROOT", Path(__file__).resolve().parents[2]))

class SilentResponseTest(unittest.TestCase):
    check = bounds.WebCommandBoundsTest.check
    @classmethod
    def setUpClass(cls):
        # 复用真实 HTTP 处理函数的宿主编译器，替换执行端以观察输出去向。
        import test_web_command_bounds as bounds
        original = bounds.PROGRAM
        source = PROGRAM[:PROGRAM.index("int main(")]
        source = source.replace("explicit ESPResponseStream(HTTP*){}", "int destination=7; explicit ESPResponseStream(HTTP*){destination=2;}")
        a = source.index("Error system_execute_line(")
        b = source.index("struct Web_Server", a)
        source = source[:a] + '''
Error system_execute_line(char* line,ESPResponseStream* out,AuthenticationLevel){
    assert(out != nullptr);
    assert(out->destination == (line[4]=='4' ? 7 : 2));
    ++parsed;
    return Error::Ok;
}
''' + source[b:]
        source += r'''
int main(int argc,char**argv){
    HTTP http;WebUI::Web_Server::_webserver=&http;WebUI::Web_Server web;
    for(const char* cmd:{"[ESP400]","[ESP410]","[ESP420]"}){
        http.command=cmd; web._handle_web_command(true);
        assert(http.code==200 && http.body=="ok");
    }
    http.command="[ESP100]"; web._handle_web_command(false);
    assert(http.code==200 && parsed==4);
}
'''
        try:
            bounds.PROGRAM = source
            bounds.WebCommandBoundsTest.setUpClass.__func__(cls)
        finally:
            bounds.PROGRAM = original

    def test_silent_queries_have_sink_and_normal_query_has_response(self):
        self.check(0, 0)

if __name__ == "__main__":
    unittest.main()
