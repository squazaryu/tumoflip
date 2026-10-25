"""Portable ASan/UBSan runner for heap and Desktop fixtures; optional LLVM coverage."""
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def native(body, *, storage=False):
    coverage = os.environ.get('TUMO_OPTIONAL_MEMORY_COVERAGE_DIR')
    with tempfile.TemporaryDirectory(prefix='optional-memory-') as directory:
        work = Path(directory)
        source, binary = work / 'test.c', work / 'test'
        source.write_text(body)
        flags = ['-fprofile-instr-generate', '-fcoverage-mapping'] if coverage else []
        if storage:
            (work / 'sd').mkdir()
            flags += ['-D_DEFAULT_SOURCE', '-Wextra', '-Wno-unused-function',
                      '-DMBEDTLS_CONFIG_FILE="library_crypto_config.h"',
                      '-I', str(ROOT / 'tools/tumoflip/fixtures'),
                      '-I', str(ROOT / 'lib/mbedtls/include')]
        result = subprocess.run(['cc', '-std=c11', '-O1', '-g', '-Wall', '-Werror',
                                 '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                                 *flags, str(source), '-o', str(binary)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        command = [str(binary), *([str(work / 'sd')] if storage else [])]
        result = subprocess.run(command, capture_output=True, text=True,
                                env=dict(os.environ, LLVM_PROFILE_FILE=str(work / 'test.profraw')))
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        if coverage:
            target = Path(coverage)
            target.mkdir(parents=True, exist_ok=True)
            number = len(list(target.glob('case-*.json')))
            profile = work / 'test.profdata'
            subprocess.run(['xcrun', 'llvm-profdata', 'merge', '-sparse', str(work / 'test.profraw'),
                            '-o', str(profile)], check=True, capture_output=True)
            args = ['xcrun', 'llvm-cov', 'export', str(binary), '-instr-profile=' + str(profile)]
            report = subprocess.check_output(args, text=True)
            json.loads(report)  # Refuse incomplete diagnostic output.
            (target / f'case-{number}.c').write_text(body)
            (target / f'case-{number}.json').write_text(report)
            (target / f'case-{number}.lcov').write_text(subprocess.check_output([*args, '-format=lcov'], text=True))
