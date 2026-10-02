"""Execute production Fiat V2 serialization with fault-injected format writes."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'applications_user/protopirate/protocols/fiat_v2.c'


def function(source, name):
    match = re.search(r'(?m)^(?:static\s+)?[\w*]+\s+' + re.escape(name) + r'\s*\([^;]*?\)\s*\{', source)
    if not match:
        raise AssertionError(f'function missing: {name}')
    depth = 1
    for i in range(match.end(), len(source)):
        depth += (source[i] == '{') - (source[i] == '}')
        if depth == 0:
            return source[match.start():i + 1]
    raise AssertionError(f'unterminated: {name}')


class FiatV2SerializationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='fiat-v2-host-')
        cls.addClassCleanup(cls.temp.cleanup)
        source = SOURCE.read_text()
        defines = '\n'.join(re.findall(r'^#define FIAT_V2_.*$', source, re.M))
        struct = re.search(r'struct SubGhzProtocolDecoderFiatV2 \{.*?\n\};', source, re.S).group()
        constants = re.search(r'static const SubGhzBlockConst subghz_protocol_fiat_v2_const = \{.*?\n\};', source, re.S).group()
        names = ['fiat_v2_button_valid', 'fiat_v2_uid', 'fiat_v2_is_fca',
                 'fiat_v2_hop', 'fiat_v2_counter', 'fiat_v2_frame_valid', 'fiat_v2_decode_fields']
        if 'static bool fiat_v2_write_u32(' in source:
            names.append('fiat_v2_write_u32')
        names += ['subghz_protocol_decoder_fiat_v2_serialize', 'subghz_protocol_decoder_fiat_v2_deserialize']
        fixture = (ROOT / 'tools/tumoflip/fixtures/fiat_v2_format.c').read_text()
        content = fixture.replace('/* PRODUCTION */', defines + '\n' + struct + '\n' + constants + '\n' + '\n'.join(function(source, name) for name in names))
        path = Path(cls.temp.name) / 'fixture.c'
        path.write_text(content)
        cls.binary = Path(cls.temp.name) / 'fixture'
        result = subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                                 '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                                 str(path), '-o', str(cls.binary)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_case(self, mode):
        result = subprocess.run([str(self.binary), mode], capture_output=True, text=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_save_open_preserves_raw_and_fields_without_duplicate_keys(self):
        self.run_case('roundtrip')

    def test_every_write_failure_is_reported(self):
        for key in ['Raw', 'Hop', 'Btn', 'Serial', 'Cnt']:
            with self.subTest(key=key):
                self.run_case(key)

    def test_malformed_load_preserves_previously_loaded_frame(self):
        self.run_case('invalid')


if __name__ == '__main__':
    unittest.main()
