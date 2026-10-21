"""Compile actual notice arithmetic for host/target font-height type variants."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which('clang++') or shutil.which('g++')


class NoticeFontTests(unittest.TestCase):
    @unittest.skipUnless(COMPILER, 'C++ compiler unavailable')
    def test_font_macro_and_integral_height_types(self):
        source = (ROOT / 'main/scanner/codex_scanner_display.cc').read_text()
        lines = [line.strip() for line in source.splitlines()
                 if line.strip().startswith(('const lv_font_t* notice_font =',
                                              'const int notice_height =',
                                              'const int notice_scale ='))]
        self.assertEqual(len(lines), 3)
        body = '\n'.join(lines)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            for kind in ('int', 'long', 'unsigned long'):
                code = ('#include <algorithm>\n#include <cassert>\n'
                        f'struct lv_font_t {{ {kind} line_height; }};\n'
                        'const lv_font_t font{30};\n#define LV_FONT_DEFAULT &font\n'
                        'int main() {\n' + body + '\n'
                        'assert(notice_scale > 0);\n'
                        'assert(notice_height * notice_scale / 256 <= 14);\n'
                        'assert((208 * 256 / notice_scale) * notice_scale / 256 <= 208);\n}\n')
                (path / 'check.cc').write_text(code)
                subprocess.run([COMPILER, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                str(path / 'check.cc'), '-o', str(path / 'check')],
                               check=True, capture_output=True)
                subprocess.run([str(path / 'check')], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
