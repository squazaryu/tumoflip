#!/usr/bin/env python3
"""Compile a bounded TumoCard JSON definition into an SD applet directory."""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
from pathlib import Path
from typing import Any


APPLET_ID_RE = re.compile(r"^[a-z0-9_-]{1,23}$")
ALLOWED_KEYS = {
    "id",
    "name",
    "aid",
    "initial_state",
    "profile",
    "writable",
    "enabled",
    "capabilities",
    "crypto",
}
REQUIRED_CAPABILITIES = {"nfc.type4", "usb.ccid"}

SELECT_CODE = [0x02, 0x04, 0x03, 0x04, 0x05, 0x90, 0x00]
READ_CODE = [0x01, 0x06, 0x05, 0x90, 0x00]
WRITE_CODE = [0x01, 0x07, 0x05, 0x90, 0x00]


class CompileError(ValueError):
    pass


def parse_hex(value: Any, field: str, minimum: int, maximum: int) -> bytes:
    if not isinstance(value, str):
        raise CompileError(f"{field} must be a hex string")
    compact = "".join(value.split())
    if len(compact) % 2:
        raise CompileError(f"{field} must contain complete bytes")
    try:
        data = bytes.fromhex(compact)
    except ValueError as error:
        raise CompileError(f"{field} contains invalid hex") from error
    if not minimum <= len(data) <= maximum:
        raise CompileError(f"{field} must contain {minimum}..{maximum} bytes")
    return data


def hex_field(data: bytes) -> str:
    return " ".join(f"{byte:02X}" for byte in data)


def validate_spec(spec: Any) -> dict[str, Any]:
    if not isinstance(spec, dict):
        raise CompileError("definition must be a JSON object")
    unknown = set(spec) - ALLOWED_KEYS
    if unknown:
        raise CompileError(f"unsupported fields: {', '.join(sorted(unknown))}")

    applet_id = spec.get("id")
    if not isinstance(applet_id, str) or not APPLET_ID_RE.fullmatch(applet_id):
        raise CompileError("id must match [a-z0-9_-]{1,23}")
    name = spec.get("name")
    if not isinstance(name, str) or not name or len(name.encode("ascii", "ignore")) != len(name):
        raise CompileError("name must be non-empty ASCII")
    if len(name) >= 24:
        raise CompileError("name must be shorter than 24 characters")

    aid = parse_hex(spec.get("aid"), "aid", 5, 16)
    initial_state = parse_hex(spec.get("initial_state"), "initial_state", 1, 64)
    if spec.get("profile", "state-v1") != "state-v1":
        raise CompileError("only the bounded state-v1 profile is supported")

    capabilities = spec.get("capabilities", sorted(REQUIRED_CAPABILITIES))
    if not isinstance(capabilities, list) or set(capabilities) != REQUIRED_CAPABILITIES:
        raise CompileError("capabilities must be exactly nfc.type4 and usb.ccid")
    crypto = spec.get("crypto", [])
    if crypto != []:
        raise CompileError("crypto capabilities are not supported by TumoCard OS 0.1")
    writable = spec.get("writable", False)
    enabled = spec.get("enabled", True)
    if not isinstance(writable, bool) or not isinstance(enabled, bool):
        raise CompileError("writable and enabled must be booleans")

    return {
        "id": applet_id,
        "name": name,
        "aid": aid,
        "initial_state": initial_state,
        "writable": writable,
        "enabled": enabled,
    }


def render_files(spec: dict[str, Any]) -> dict[str, str]:
    aid: bytes = spec["aid"]
    state: bytes = spec["initial_state"]
    writable: bool = spec["writable"]
    bytecode = SELECT_CODE + READ_CODE + (WRITE_CODE if writable else [])
    routes = [0x00, 0xA4, 0x00, 0x00, 0xB0, len(SELECT_CODE)]
    if writable:
        routes.extend([0x00, 0xD6, len(SELECT_CODE) + len(READ_CODE)])

    manifest = "\n".join(
        [
            "Filetype: TumoCard Applet",
            "Version: 1",
            f"Name: {spec['name']}",
            f"AID size: {len(aid)}",
            f"AID: {hex_field(aid)}",
            f"State size: {len(state)}",
            f"Initial state: {hex_field(state)}",
            "Capability NFC Type4: true",
            "Capability USB CCID: true",
            "Crypto mask: 0",
            "",
        ]
    )
    program = "\n".join(
        [
            "Filetype: TumoVM Program",
            "Version: 1",
            f"Route count: {len(routes) // 3}",
            f"Routes: {hex_field(bytes(routes))}",
            f"Bytecode size: {len(bytecode)}",
            f"Bytecode: {hex_field(bytes(bytecode))}",
            "",
        ]
    )
    settings = "\n".join(
        [
            "Filetype: TumoCard Settings",
            "Version: 1",
            f"Enabled: {'true' if spec['enabled'] else 'false'}",
            "",
        ]
    )
    return {
        "manifest.tca": manifest,
        "program.tvm": program,
        "settings.tcs": settings,
    }


def compile_applet(definition: Path, output_root: Path, *, force: bool = False) -> Path:
    try:
        raw = json.loads(definition.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CompileError(f"cannot read definition: {error}") from error
    spec = validate_spec(raw)
    destination = output_root / spec["id"]
    temporary = output_root / f".{spec['id']}.tmp"
    if destination.exists() and not force:
        raise CompileError(f"destination already exists: {destination}")

    output_root.mkdir(parents=True, exist_ok=True)
    shutil.rmtree(temporary, ignore_errors=True)
    temporary.mkdir()
    try:
        for name, content in render_files(spec).items():
            (temporary / name).write_text(content, encoding="ascii")
        if destination.exists():
            shutil.rmtree(destination)
        os.replace(temporary, destination)
    except Exception:
        shutil.rmtree(temporary, ignore_errors=True)
        raise
    return destination


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("definition", type=Path, help="bounded applet JSON definition")
    parser.add_argument("output", type=Path, help="target applets directory")
    parser.add_argument("--force", action="store_true", help="replace an existing applet directory")
    args = parser.parse_args()
    try:
        destination = compile_applet(args.definition, args.output, force=args.force)
    except CompileError as error:
        parser.error(str(error))
    print(destination)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
