import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / 'Software' if (ROOT / 'Software/src').exists() else ROOT
spec = importlib.util.spec_from_file_location('identity_build', PROJECT/'serial_identity_build.py')
build = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build)

class ProfileTests(unittest.TestCase):
    def test_target_variants(self):
        for product, flash, board, model, ram in (
            ('LKBX','16MB','esp32-s3-devkitc-1-n16r2v','LKBX-N16R2',2),
            ('LKBX','16MB','esp32-s3-devkitc-1-n16r8v','LKBX-N16R8',8),
            ('RADR','16MB','esp32-s3-devkitc-1-n16r8v','RADR-N16R8',8),
            ('DTT','4MB','esp32dev','DTT-N4',0),
            ('DTT','16MB','esp32dev','DTT-N16',0),
            ('OSSM','4MB','esp32dev','OSSM-N4',0),
            ('OSSM','16MB','esp32dev','OSSM-N16',0)):
            with self.subTest(model=model):
                self.assertEqual(build.profile_identity(product,flash,board),
                    (model,int(flash[:-2])*1048576,ram*1048576))

    def test_reject_unknown_or_incompatible_profiles(self):
        for args in [('FAKE','4MB','esp32dev'),('LKBX','4MB','esp32-s3-devkitc-1-n16r2v'),
                     ('RADR','16MB','esp32dev'),('OSSM','auto','esp32dev')]:
            with self.assertRaises(ValueError): build.profile_identity(*args)

class WireTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        directory = Path(cls.temp.name)
        cpp = directory/'probe.cpp'
        cpp.write_text(r'''
#include "serialIdentityProtocol.h"
#include <iostream>
#include <string>
int main(int argc, char** argv) {
  if (std::string(argv[1]) == "parse") {
    serialIdentity::CommandBuffer parser;
    int c;
    while ((c = std::cin.get()) != EOF) {
      if (const char* command = parser.push(c)) std::cout << command << "\n";
    }
    return 0;
  }
  serialIdentity::Info info{"DTT","DTT-N4","2.0.94","staging","unknown","123456789ABC",
      4194304,0,16777216,0};
  if (std::string(argv[1]) == "injection") info.version = "2.0.4\"\nBAD";
  char line[640];
  size_t count = serialIdentity::format(line, std::string(argv[1]) == "short" ? 16 : sizeof(line), info);
  if (count) std::cout.write(line, count);
}
''')
        fallback = Path.home()/'.platformio/packages/toolchain-gccmingw32/bin/g++.exe'
        compiler = os.getenv('CXX') or shutil.which('g++') or (str(fallback) if fallback.exists() else None)
        if not compiler:
            raise RuntimeError('A native C++ compiler is required for serial identity tests')
        cls.program = directory/('probe.exe' if os.name == 'nt' else 'probe')
        cls.env = dict(os.environ, PATH=str(Path(compiler).parent)+os.pathsep+os.environ['PATH'])
        subprocess.run([compiler,'-std=c++11','-Wall','-Wextra','-Werror','-Wno-unused-parameter',
            '-I'+str(PROJECT/'src/services'),str(cpp),'-o',str(cls.program)],check=True,env=cls.env,
            stdout=subprocess.PIPE,stderr=subprocess.PIPE)

    @classmethod
    def tearDownClass(cls): cls.temp.cleanup()

    def run_probe(self, mode, data=b''):
        return subprocess.run([str(self.program),mode],input=data,stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE,check=True,env=self.env).stdout.decode().replace('\r\n','\n')

    def test_fragmented_and_crlf_commands(self):
        self.assertEqual(self.run_probe('parse',b'\ninfo\r\nscreen retry\nidentity\n'),
                         'info\nscreen retry\nidentity\n')

    def test_binary_and_overlong_lines_are_not_suffix_commands(self):
        self.assertEqual(self.run_probe('parse',b'\x00info\n'+b'x'*200+b'info\ninfo\n'),'info\n')

    def test_partial_command_needs_delimiter(self):
        self.assertEqual(self.run_probe('parse',b'info'),'')

    def test_report_separates_target_from_physical_flash(self):
        lines = self.run_probe('format').splitlines()
        self.assertEqual(lines[1],'DTT-N4 2.0.94')
        data = json.loads(lines[2].removeprefix('RAD_ID '))
        self.assertEqual(data['target_flash_bytes'],4194304)
        self.assertEqual(data['flash_bytes'],16777216)
        self.assertEqual(data['device_id'],'123456789ABC')

    def test_json_injection_and_truncated_frames_are_never_sent(self):
        self.assertEqual(self.run_probe('injection'),'')
        self.assertEqual(self.run_probe('short'),'')

if __name__ == '__main__': unittest.main()
