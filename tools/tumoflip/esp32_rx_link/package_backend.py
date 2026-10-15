"""Package only an ESP32 RX Link build, never a Marauder/automatic-flash recipe."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile


def package(root, source_commit):
    if not re.fullmatch(r"[0-9a-f]{40}", source_commit):
        raise ValueError("full source commit required")
    build = root / "build"
    args = json.loads((build / "flasher_args.json").read_text())
    expected = {
        "0x1000": "bootloader/bootloader.bin",
        "0x8000": "partition_table/partition-table.bin",
        "0x10000": "tumo_rx_link_module_one.bin",
    }
    if args.get("flash_files") != expected or args.get("extra_esptool_args", {}).get("chip") != "esp32":
        raise ValueError("unexpected chip or flash layout")
    files = {name: (build / name).read_bytes() for name in expected.values()}
    for name in ("bootloader/bootloader.bin", "tumo_rx_link_module_one.bin"):
        image = files[name]
        if len(image) < 24 or image[0] != 0xE9 or image[12:14] != b"\x00\x00":
            raise ValueError("image is not for classic ESP32")
    metadata = {
        "kind": "experimental-tumo-rx-link", "version": "0.1.0",
        "board": "module-one-v6.1", "chip": "esp32", "source_commit": source_commit,
        "idf": "5.3.2", "wire_version": 1, "baud": 230400,
        "hardware_accepted": False, "replaces_marauder": True,
        "files": [{"offset": offset, "path": path, "bytes": len(files[path]),
                   "sha256": hashlib.sha256(files[path]).hexdigest()}
                  for offset, path in expected.items()],
    }
    files["rx-link-build.json"] = (json.dumps(metadata, indent=2) + "\n").encode()
    files["README.md"] = (root / "README.md").read_bytes()
    target = build / "tumo-rx-link-module-one-0.1.0.zip"
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, payload in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o100644 << 16
            archive.writestr(info, payload)
    with zipfile.ZipFile(target) as archive:
        assert archive.testzip() is None
        for name, payload in files.items():
            assert archive.read(name) == payload
    print(target)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-commit", required=True)
    arguments = parser.parse_args()
    package(Path(__file__).resolve().parent, arguments.source_commit)
