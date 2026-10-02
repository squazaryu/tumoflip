"""Native regression tests for receive-only Renault V1 and saved frames."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class RenaultV1ReceiveTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='renault-rx-host-')
        cls.addClassCleanup(cls.temp.cleanup)
        header = (ROOT / 'lib/subghz/protocols/renault_v1.h').read_text()
        source = (ROOT / 'lib/subghz/protocols/renault_v1.c').read_text()
        header = re.sub(r'(?m)^#(?:include|pragma once).*$', '', header)
        source = re.sub(r'(?m)^#include.*$', '', source)
        fixture = (ROOT / 'tools/tumoflip/fixtures/renault_v1_rx.c').read_text()
        path = Path(cls.temp.name) / 'fixture.c'
        path.write_text(fixture.replace('/* PRODUCTION */', header + '\n' + source))
        cls.binary = Path(cls.temp.name) / 'fixture'
        built = subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                                '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                                '-I', str(ROOT), str(path), str(ROOT / 'lib/toolbox/manchester_decoder.c'),
                                '-o', str(cls.binary)], capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def run_case(self, case):
        result = subprocess.run([str(self.binary), case], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_valid_trace_and_repeated_frames(self): self.run_case('trace')
    def test_noise_truncation_checksum_and_header_rejected(self): self.run_case('invalid')
    def test_save_open_and_legacy_key2_formats(self): self.run_case('file')
    def test_invalid_load_and_partial_save_fail_closed(self): self.run_case('failure')
    def test_protocol_has_no_transmitter(self): self.run_case('rx-only')


if __name__ == '__main__':
    unittest.main()
