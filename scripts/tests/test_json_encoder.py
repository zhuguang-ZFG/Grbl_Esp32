"""WebUI JSON 字符串的引号、反斜杠及控制字符往返。"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get("GRBL_ROOT", Path(__file__).resolve().parents[2]))


class JSONEncoderTest(unittest.TestCase):
    def _build_probe(self, folder, main_body):
        compiler = os.environ.get("CXX") or shutil.which("g++")
        self.assertTrue(compiler)
        base = ROOT / "Grbl_Esp32/src/WebUI"
        header = (base / "JSONEncoder.h").read_text(encoding="utf-8")
        source = (base / "JSONEncoder.cpp").read_text(encoding="utf-8")

        def strip(text):
            return re.sub(r"(?m)^\s*#\s*(?:include|pragma)[^\n]*$", "", text)

        program = r'''
#include <string>
#include <iostream>
struct String:std::string{using std::string::string;String(int n):std::string(std::to_string(n)){}void concat(const char* s){append(s);}};
''' + strip(header) + strip(source) + main_body
        cpp = Path(folder) / "probe.cpp"
        exe = Path(folder) / ("probe.exe" if os.name == "nt" else "probe")
        cpp.write_text(program, encoding="utf-8")
        built = subprocess.run([compiler, "-std=c++17", str(cpp), "-o", str(exe)], capture_output=True, timeout=60)
        self.assertEqual(built.returncode, 0, built.stderr.decode("utf-8", "replace"))
        return exe

    def test_json_encoder_escapes_string_values(self):
        main_body = r'''
int main(int argc,char**argv){WebUI::JSONencoder j;j.begin();j.member("SSID",argv[1]);std::cout<<j.end();}
'''
        with tempfile.TemporaryDirectory() as folder:
            exe = self._build_probe(folder, main_body)
            for value in ('Home"Net', r"Home\room", "Home\nroom"):
                out = subprocess.check_output([str(exe), value], text=True)
                self.assertEqual(json.loads(out)["SSID"], value)

    def test_json_encoder_null_value_is_empty_string(self):
        # 回归：quoted() 对 null 需当空串（旧 str.concat 对 null 安全，新转义循环裸解 *p 会崩）。
        main_body = r'''
int main(){WebUI::JSONencoder j;j.begin();j.member("SSID",(const char*)nullptr);std::cout<<j.end();}
'''
        with tempfile.TemporaryDirectory() as folder:
            exe = self._build_probe(folder, main_body)
            out = subprocess.check_output([str(exe)], text=True)
            self.assertEqual(json.loads(out)["SSID"], "")


if __name__ == "__main__":
    unittest.main()
