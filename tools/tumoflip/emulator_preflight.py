"""Read-only readiness report for the pinned experimental Flipper/Renode source.

This is NOT a boot test. It does not clone, install, execute upstream scripts,
create disk images, open devices, change assertions or accept model licenses.
"""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import shutil
import subprocess

SOURCE = 'https://github.com/d4rks1d33/Flipper-Zero-Emulator'
PIN = '6b2443fc0065f102ea3045024b1d5e65f4d4c68c'


def git(root, *args):
    result = subprocess.run(['git', '-C', str(root), *args], capture_output=True, text=True, timeout=10)
    return result.stdout.strip() if result.returncode == 0 else None


def inspect(root, host=None):
    root = Path(root).resolve()
    host = host or platform.system()
    blockers = []
    present = root.is_dir()
    commit = git(root, 'rev-parse', 'HEAD') if present else None
    dirty = git(root, 'status', '--porcelain') if present else None
    if not present: blockers.append('sourceUnavailable')
    if commit != PIN: blockers.append('sourcePinMismatch')
    if dirty: blockers.append('sourceModified')
    # Renode's MIT license does not implicitly license newly written peripheral models.
    model_license = next((name for name in ('LICENSE', 'LICENSE.md', 'COPYING')
                          if (root / name).is_file()), None)
    if not model_license: blockers.append('modelLicenseMissing')
    if host != 'Linux': blockers.append('linuxRuntimeRequired')
    if not shutil.which('renode'): blockers.append('renodeRuntimeUnavailable')
    models = {}
    for name in ('ST7567.cs', 'FlipperSdCard.cs', 'IPCC_WB55.cs'):
        path = root / 'peripherals' / name
        if path.is_file() and path.stat().st_size <= 256 * 1024:
            models[name] = hashlib.sha256(path.read_bytes()).hexdigest()
        else: blockers.append('missingOrOversizedModel:' + name)
    image = root / 'sdcard/sdcard.img'
    existing_size = image.stat().st_size if image.is_file() else None
    if existing_size and existing_size > 64 * 1024 * 1024: blockers.append('existingDiskExceedsBudget')
    return {
        'schema': 1, 'source': SOURCE, 'expectedCommit': PIN, 'observedCommit': commit,
        'host': host, 'modelLicenseFile': model_license, 'modelSHA256': models,
        'blockers': blockers, 'readyToRun': False, 'status': 'experimental-preflight-only',
        'bootTested': False, 'hardwareAccepted': False, 'proposedDiskBudgetMiB': 64,
        'existingDiskBytes': existing_size,
        'limitations': ['No firmware execution in this command', 'No RF/BLE/SD electrical acceptance',
                        'No upstream installer or 32GB image creation',
                        'Model license and actual bounded boot harness require separate resolution'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('checkout', type=Path)
    report = inspect(parser.parse_args().checkout)
    print(json.dumps(report, indent=2))
    return 2 if report['blockers'] else 0


if __name__ == '__main__': raise SystemExit(main())
