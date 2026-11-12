"""Package only an ESP32 RX Link build, never a Marauder/automatic-flash recipe."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import zipfile


def package(root, source_commit, sdk_root=None):
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
    # ESP-IDF app descriptor starts after the image and first segment headers.
    if files["tumo_rx_link_module_one.bin"][48:80].split(b"\0", 1)[0] != b"0.1.0":
        raise ValueError("ESP application version differs from package version")
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
    files["licenses/tumoflip-LICENSE"] = (Path(__file__).resolve().parents[3] / "LICENSE").read_bytes()
    sdk = Path(sdk_root or os.environ.get("IDF_PATH", "/nonexistent")).resolve()
    if not (sdk / "LICENSE").is_file():
        raise ValueError("ESP-IDF license tree is required")
    suffixes = {"", ".txt", ".md", ".rst", ".lib", ".lesser", ".gpl", ".bsd", ".mit"}
    for notice in sorted(sdk.rglob("*")):
        if ".git" in notice.parts or not notice.is_file():
            continue
        if not notice.name.upper().startswith(("LICENSE", "LICENCE", "COPYING", "NOTICE")):
            continue
        if notice.suffix.lower() not in suffixes or not notice.resolve().is_relative_to(sdk):
            continue
        if notice.stat().st_size > 1024 * 1024:
            raise ValueError("unexpected oversized SDK notice")
        files["licenses/esp-idf/" + notice.relative_to(sdk).as_posix()] = notice.read_bytes()
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
