"""Instrument the real new model/store/controller bodies, excluding GUI/I/O mocks.

Only bodies whose entire current production translation unit is present in a
fixture are eligible. A mocked function with the same name cannot count as a
production implementation. Per-function relative regions/lines are unioned
across the independently executed native cases. All sanitizers stay enabled.
"""
import argparse
import importlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]
FILES = [ROOT / 'applications_user/servo_tester_plus' / name for name in ('servo_model.c', 'servo_hardware.c')]
FILES += [ROOT / 'applications/system/device_library' / name for name in
          ('power_history.c', 'power_store.c', 'field_report.c', 'uhf_inventory.c', 'uhf_runtime.c',
           'sensor_cards.c', 'sensor_runtime.c', 'varta_inspect.c')]
MODULES = ('test_servo_plus', 'test_power_history', 'test_field_report', 'test_uhf_inventory',
           'test_sensor_cards', 'test_varta_inspect')


def compact(source):
    return re.sub(r'\s+', '', re.sub(r'^#(?:include|pragma).*\n', '', source, flags=re.M))


def measure(output):
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    run = subprocess.run
    cases = []
    binaries = {}

    def instrument(command, *args, **kwargs):
        command = list(command)
        sources = [Path(p) for p in command if isinstance(p, str) and p.endswith('/test.c')]
        if command[0] == 'cc' and sources and '-o' in command:
            work = output / f'case-{len(cases):02d}'
            work.mkdir()
            source = sources[0].read_text()
            (work / 'source.c').write_text(source)
            command[1:1] = ['-fprofile-instr-generate', '-fcoverage-mapping']
            binary = command[command.index('-o') + 1]
            result = run(command, *args, **kwargs)
            if result.returncode == 0:
                shutil.copy2(binary, work / 'test')
                case = {'work': work, 'source': source}
                cases.append(case)
                binaries[binary] = case
            return result
        if command[0] in binaries:
            kwargs['env'] = {**os.environ, **(kwargs.get('env') or {}),
                             'LLVM_PROFILE_FILE': str(binaries[command[0]]['work'] / 'run.profraw')}
        return run(command, *args, **kwargs)

    suite = unittest.TestSuite(unittest.defaultTestLoader.loadTestsFromModule(
        importlib.import_module('tools.tumoflip.' + name)) for name in MODULES)
    try:
        subprocess.run = instrument
        result = unittest.TextTestRunner(verbosity=0).run(suite)
        if not result.wasSuccessful(): raise RuntimeError('Instrumented native regression failed')
    finally:
        subprocess.run = run
    coverage = {}
    for file in FILES:
        production = file.read_text()
        names = set(re.findall(r'(?m)^[A-Za-z_][\w \t*]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*\{', production))
        coverage[file.name] = {'names': names, 'functions': {}, 'lines': {}, 'regions': {}}
    for case in cases:
        work = case['work']
        profile = work / 'run.profdata'
        run(['xcrun', 'llvm-profdata', 'merge', '-sparse', str(work / 'run.profraw'), '-o', str(profile)], check=True, capture_output=True)
        report = json.loads(subprocess.check_output(['xcrun', 'llvm-cov', 'export', str(work / 'test'),
            '-instr-profile=' + str(profile)], text=True))['data'][0]
        lcov = subprocess.check_output(['xcrun', 'llvm-cov', 'export', str(work / 'test'),
            '-instr-profile=' + str(profile), '-format=lcov'], text=True)
        lines, current = {}, None
        for line in lcov.splitlines():
            if line.startswith('SF:'): current = lines.setdefault(line[3:], {})
            elif line.startswith('DA:') and current is not None:
                number, count = map(int, line[3:].split(',')[:2])
                current[number] = count
        source = compact(case['source'])
        for file in FILES:
            if compact(file.read_text()) not in source: continue
            item = coverage[file.name]
            for f in report['functions']:
                name = f['name'].rsplit(':', 1)[-1]
                if name not in item['names']: continue
                item['functions'][name] = item['functions'].get(name, False) or f['count'] > 0
                first = f['regions'][0]
                base = first[0]
                mapping = lines.get(f['filenames'][first[5]], {})
                for number in range(first[0], first[2] + 1):
                    if number in mapping:
                        key = (name, number - base)
                        item['lines'][key] = item['lines'].get(key, False) or mapping[number] > 0
                for region in f['regions']:
                    if region[7] != 0 or region[5] != first[5]: continue
                    key = (name, region[0] - base, region[1], region[2] - base, region[3])
                    item['regions'][key] = item['regions'].get(key, False) or region[4] > 0
    summary = {}
    for name, item in coverage.items():
        missing = sorted(item['names'] - item['functions'].keys())
        if missing or not item['lines'] or not item['regions']:
            raise RuntimeError(f'Incomplete production coverage mapping: {name}, missing {missing}')
        summary[name] = {
            'functions': len(item['names']), 'functions_hit': sum(item['functions'].values()),
            'lines_percent': round(100 * sum(item['lines'].values()) / len(item['lines']), 2),
            'regions_percent': round(100 * sum(item['regions'].values()) / len(item['regions']), 2),
            'unhit': sorted(k for k, hit in item['functions'].items() if not hit),
        }
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))
    return all(min(item['lines_percent'], item['regions_percent']) >= 80 for item in summary.values())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    raise SystemExit(0 if measure(parser.parse_args().output) else 1)
