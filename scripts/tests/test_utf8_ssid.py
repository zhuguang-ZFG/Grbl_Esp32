"""真实UTF-8校验/工厂身份宿主回归。"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
CXX=os.environ.get('CXX','D:/zhugu-home/mingw64/mingw64/bin/g++.exe')
class FactoryTest(unittest.TestCase):
    def compile_run(self,source,headers=None):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary);cpp=path/'test.cpp';cpp.write_text(source,encoding='utf-8')
            for name,text in (headers or {}).items():(path/name).write_text(text,encoding='utf-8')
            exe=path/'test.exe'
            build=subprocess.run([CXX,'-std=c++17','-Wall','-Wextra','-Werror','-I',str(path),'-I',str(ROOT),str(cpp),'-o',str(exe)],capture_output=True,timeout=60)
            self.assertEqual(build.returncode,0,build.stderr.decode('utf-8','replace'))
            env=dict(os.environ,PATH=str(Path(CXX).parent)+os.pathsep+os.environ.get('PATH',''))
            run=subprocess.run([str(exe)],capture_output=True,env=env,timeout=15)
            self.assertEqual(run.returncode,0,run.stderr.decode('utf-8','replace'))

    def test_utf8_matrix(self):
        self.compile_run('\n#include <cassert>\n#include <string>\n#include "Grbl_Esp32/src/WebUI/Utf8Ssid.h"\nint main(){\n    auto check=[](const std::string& s){return ValidUtf8Ssid(s.data(),s.size());};\n    assert(check("Home Wifi"));assert(check("家里网络"));assert(check("家庭😀"));\n    assert(check(std::string(32,\'a\')));assert(!check(std::string(33,\'a\')));\n    assert(!check(""));assert(!check("a\\nb"));assert(!check(std::string("a\\0b",3)));\n    assert(!check("\\xc0\\xaf"));assert(!check("\\xe5\\xae"));assert(!check("\\xed\\xa0\\x80"));\n    assert(!check("\\xf4\\x90\\x80\\x80"));assert(!check("\\xc2\\x85"));assert(!check("\\xe2\\x80\\xae"));\n}\n')

if __name__=='__main__':unittest.main()
