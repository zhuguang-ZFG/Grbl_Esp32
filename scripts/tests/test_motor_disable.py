"""编译真实 $MD 参数处理与 trim，验证正常/错误返回均回收原始分配。"""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function_body(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth = 0
    for end in range(brace, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if depth == 0:
            return source[start:end + 1]
    raise AssertionError('函数体未闭合')


class MotorDisableMemoryTest(unittest.TestCase):
    def test_parameters_do_not_leak_or_change_axis_semantics(self):
        compiler = os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
        if not compiler:
            self.skipTest('需要 host C++ 编译器')
        process = (ROOT / 'Grbl_Esp32/src/ProcessSettings.cpp').read_text(encoding='utf-8')
        nuts = (ROOT / 'Grbl_Esp32/src/NutsBolts.cpp').read_text(encoding='utf-8')
        source = r'''
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include "Grbl_Esp32/src/Error.h"
std::set<void*> allocated;
int allocations = 0;
int frees = 0;
char* counted_strdup(const char* text) {
    auto* copy = static_cast<char*>(std::malloc(std::strlen(text) + 1));
    assert(copy);
    std::strcpy(copy, text);
    assert(allocated.insert(copy).second);
    ++allocations;
    return copy;
}
void counted_free(void* original) {
    assert(allocated.erase(original) == 1); // trim/逐字解析偏移后的指针不能交回 free。
    ++frees;
    std::free(original);
}
#define strdup counted_strdup
#define free counted_free
#define bit(index) (1UL << (index))
namespace WebUI {
enum class AuthenticationLevel { LEVEL_ADMIN };
struct ESPResponseStream {};
}
struct String {
    std::string value;
    explicit String(const char* text): value(text) {}
    int indexOf(int c) const {
        const auto found = value.find(static_cast<char>(c));
        return found == std::string::npos ? -1 : static_cast<int>(found);
    }
};
int disable_calls = 0;
int32_t last_mask = 0;
void motors_set_disable(bool disabled, int32_t mask) {
    assert(disabled);
    ++disable_calls;
    last_mask = mask;
}
''' + function_body(nuts, 'char* trim(char* str)') + '\n' + function_body(process, 'Error motor_disable(') + r'''
int main() {
    struct Case { const char* value; Error error; int32_t mask; };
    const Case cases[] = {
        {nullptr, Error::Ok, 255}, {"", Error::Ok, 255}, {" \t ", Error::Ok, 255},
        {" 5 ", Error::Ok, 5}, {"XY", Error::Ok, 3}, {" xZ ", Error::Ok, 5},
        {"256", Error::Ok, 256}, {"-1", Error::Ok, -1},
        {" XQ ", Error::BadNumberFormat, 0}, {"1x", Error::BadNumberFormat, 0}
    };
    for (int round = 0; round < 1000; ++round) {
        for (const auto& item : cases) {
            const int before = disable_calls;
            const Error result = motor_disable(item.value, WebUI::AuthenticationLevel::LEVEL_ADMIN, nullptr);
            assert(result == item.error);
            assert(disable_calls == before + (result == Error::Ok));
            if (result == Error::Ok) assert(last_mask == item.mask);
            assert(allocated.empty());
            assert(allocations == frees);
        }
    }
    assert(allocations == 10000 && frees == 10000);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            stem = Path(directory) / 'motor_disable'
            stem.with_suffix('.cpp').write_text(source, encoding='utf-8')
            exe = stem.with_suffix('.exe' if os.name == 'nt' else '')
            built = subprocess.run([compiler, '-std=c++11', '-Wall', '-Wextra', '-Werror',
                                    '-Wno-unused-parameter', '-I', str(ROOT),
                                    str(stem.with_suffix('.cpp')), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stderr or built.stdout)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stderr or ran.stdout)


if __name__ == '__main__':
    unittest.main()
