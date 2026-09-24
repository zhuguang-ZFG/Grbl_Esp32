"""编译真实 WebSettings 解析函数，验证容量、畸形输入与重复调用。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(os.environ.get("GRBL_ROOT", Path(__file__).resolve().parents[2]))


class WebSettingsParamsTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CXX") or shutil.which("g++")
        if not compiler:
            raise RuntimeError("需要 host C++ 编译器，不能跳过验证")
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        stem = Path(cls.directory.name) / "web_params"
        source = (ROOT / "Grbl_Esp32/src/WebUI/WebSettings.cpp").read_text(encoding="utf-8")
        start = source.index("bool            split_params(")
        body = source[start:source.index("    char  nullstr", start)]
        program = r'''
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
struct keyval_t { char* key; char* value; };
struct Storage {
    size_t before = 0x13579;
    keyval_t slots[10] = {};
    size_t after = 0x24680;
} storage;
#define params storage.slots
''' + body + r'''
bool parse(const std::string& input) {
    static std::vector<char> buffer;
    buffer.assign(input.begin(), input.end());
    buffer.push_back(0);
    bool ok = split_params(buffer.data());
    assert(storage.before == 0x13579 && storage.after == 0x24680);
    return ok;
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const int scenario = std::atoi(argv[1]);
    if (scenario == 0) {
        assert(parse("T=F P=110 V=4500"));
        assert(std::strcmp(params[0].key, "T") == 0);
        assert(std::strcmp(params[2].value, "4500") == 0);
        assert(params[3].key == nullptr);
    } else if (scenario <= 3) {
        const int counts[] = {0, 9, 10, 100};
        std::string input;
        for (int i = 0; i < counts[scenario]; ++i)
            input += (i ? " " : "") + std::string("K") + std::to_string(i) + "=1";
        assert(parse(input) == (scenario == 1));
        if (scenario == 1) assert(params[9].key == nullptr);
        assert(parse("K=2"));
        assert(params[1].key == nullptr);
    } else if (scenario == 4) {
        assert(!parse("=bad"));
        assert(!parse("K=1=2"));
        assert(parse("K=ok"));
    } else {
        assert(parse("K=before"));
        assert(parse(""));
        assert(params[0].key == nullptr);
    }
}
'''
        stem.with_suffix(".cpp").write_text(program, encoding="utf-8")
        cls.exe = stem.with_suffix(".exe" if os.name == "nt" else "")
        result = subprocess.run([compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror",
                                 str(stem.with_suffix(".cpp")), "-o", str(cls.exe)],
                                capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stderr or result.stdout)
        cls.env = dict(os.environ)
        cls.env["PATH"] = str(Path(compiler).parent) + os.pathsep + cls.env.get("PATH", "")

    def run_case(self, scenario):
        result = subprocess.run([str(self.exe), str(scenario)], env=self.env,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr or result.stdout)

    def test_normal_settings(self): self.run_case(0)
    def test_nine_parameters_and_terminator(self): self.run_case(1)
    def test_tenth_parameter_rejected(self): self.run_case(2)
    def test_many_parameters_rejected(self): self.run_case(3)
    def test_malformed_then_valid(self): self.run_case(4)
    def test_empty_clears_previous_values(self): self.run_case(5)


if __name__ == "__main__":
    unittest.main()
