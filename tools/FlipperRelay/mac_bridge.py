#!/usr/bin/env python3
"""macOS host bridge for tumoflip BLE App Bridge events.

The bridge listens for framed events emitted by Flipper apps through the
tumoflip BLE App Bridge and executes only commands explicitly listed in a local
JSON config file.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path


APP_BRIDGE_EVENTS_CHAR_UUID = "7f7d0000-2e31-4c42-8a98-9b2f6b8c0002"
APP_BRIDGE_COMMANDS_CHAR_UUID = "7f7d0000-2e31-4c42-8a98-9b2f6b8c0003"
APP_BRIDGE_FRAME_MAGIC = b"FAB1"
APP_BRIDGE_PAYLOAD_LEN_MAX = 172


@dataclass(frozen=True)
class BridgeCommand:
    app_id: str
    command: str
    run: list[str]
    timeout: float
    cwd: str | None = None


@dataclass(frozen=True)
class BridgeConfig:
    ble_target: str
    scan_timeout: float
    default_timeout: float
    commands: dict[tuple[str, str], BridgeCommand]


def decode_app_bridge_frame(data: bytes) -> tuple[str, str, bytes] | None:
    if len(data) < 8 or data[:4] != APP_BRIDGE_FRAME_MAGIC:
        return None

    app_id_len = data[4]
    command_len = data[5]
    payload_len = int.from_bytes(data[6:8], "little")
    expected_len = 8 + app_id_len + command_len + payload_len
    if (
        app_id_len == 0
        or command_len == 0
        or app_id_len > 32
        or command_len > 32
        or payload_len > APP_BRIDGE_PAYLOAD_LEN_MAX
        or len(data) != expected_len
    ):
        return None

    pos = 8
    app_id = data[pos : pos + app_id_len].decode("ascii", errors="replace")
    pos += app_id_len
    command = data[pos : pos + command_len].decode("ascii", errors="replace")
    pos += command_len
    return app_id, command, data[pos:]


def encode_app_bridge_frame(app_id: str, command: str, payload: bytes = b"") -> bytes:
    app_id_bytes = app_id.encode("ascii")
    command_bytes = command.encode("ascii")
    if not app_id_bytes or len(app_id_bytes) > 32:
        raise ValueError("app_id must be 1..32 ASCII bytes")
    if not command_bytes or len(command_bytes) > 32:
        raise ValueError("command must be 1..32 ASCII bytes")
    if len(payload) > APP_BRIDGE_PAYLOAD_LEN_MAX:
        raise ValueError("payload is too large")

    return (
        APP_BRIDGE_FRAME_MAGIC
        + bytes([len(app_id_bytes), len(command_bytes)])
        + len(payload).to_bytes(2, "little")
        + app_id_bytes
        + command_bytes
        + payload
    )


def load_config(path: Path) -> BridgeConfig:
    with path.open("r", encoding="utf-8") as file:
        raw = json.load(file)

    default_timeout = float(raw.get("default_timeout", 8.0))
    commands: dict[tuple[str, str], BridgeCommand] = {}
    for item in raw.get("commands", []):
        app_id = str(item["app_id"])
        command = str(item["command"])
        run = item.get("run")
        if not isinstance(run, list) or not all(isinstance(part, str) for part in run):
            raise ValueError(f"{app_id}/{command}: run must be a string array")
        if not run:
            raise ValueError(f"{app_id}/{command}: run must not be empty")

        key = (app_id, command)
        if key in commands:
            raise ValueError(f"duplicate command mapping: {app_id}/{command}")
        commands[key] = BridgeCommand(
            app_id=app_id,
            command=command,
            run=run,
            timeout=float(item.get("timeout", default_timeout)),
            cwd=item.get("cwd"),
        )

    return BridgeConfig(
        ble_target=str(raw.get("ble_target", "TUMOFLIP")),
        scan_timeout=float(raw.get("scan_timeout", 5.0)),
        default_timeout=default_timeout,
        commands=commands,
    )


async def find_ble_device(target: str, timeout: float):
    try:
        from bleak import BleakScanner
    except ImportError as exc:
        raise RuntimeError("bleak is not installed; run: python3 -m pip install bleak") from exc

    normalized_target = target.lower()
    devices = await BleakScanner.discover(timeout=timeout)
    for device in devices:
        name = (device.name or "").lower()
        address = (device.address or "").lower()
        if normalized_target in name or normalized_target == address:
            return device
    raise RuntimeError(f"no BLE device found matching {target!r}")


def run_mapped_command(mapping: BridgeCommand, payload: bytes) -> tuple[bool, str]:
    payload_text = payload.decode("utf-8", errors="replace")
    env = dict(os.environ)
    env.update(
        {
            "FLIPPER_APP_ID": mapping.app_id,
            "FLIPPER_COMMAND": mapping.command,
            "FLIPPER_PAYLOAD": payload_text,
        }
    )
    try:
        completed = subprocess.run(
            mapping.run,
            cwd=mapping.cwd,
            env=env,
            timeout=mapping.timeout,
            capture_output=True,
            text=True,
            check=False,
        )
    except subprocess.TimeoutExpired:
        return False, "timeout"
    except OSError as exc:
        return False, str(exc)

    output = (completed.stdout or completed.stderr or "").strip()
    detail = output[:120] if output else f"exit {completed.returncode}"
    return completed.returncode == 0, detail


async def run_once(cfg: BridgeConfig) -> None:
    try:
        from bleak import BleakClient
    except ImportError as exc:
        raise RuntimeError("bleak is not installed; run: python3 -m pip install bleak") from exc

    device = await find_ble_device(cfg.ble_target, cfg.scan_timeout)
    print(f"Connecting BLE: {device.name or '<unnamed>'} {device.address}", flush=True)

    async with BleakClient(device) as client:
        print("tumoflip BLE App Bridge ready.", flush=True)
        events: asyncio.Queue[bytes] = asyncio.Queue()

        def on_notify(_sender, data: bytearray) -> None:
            events.put_nowait(bytes(data))

        await client.start_notify(APP_BRIDGE_EVENTS_CHAR_UUID, on_notify)
        while client.is_connected:
            try:
                data = await asyncio.wait_for(events.get(), timeout=1.0)
            except asyncio.TimeoutError:
                continue

            decoded = decode_app_bridge_frame(data)
            if not decoded:
                print(f"ignored invalid frame: {data.hex()}", flush=True)
                continue

            app_id, command, payload = decoded
            mapping = cfg.commands.get((app_id, command))
            if not mapping:
                print(f"ignored unmapped event: {app_id}/{command}", flush=True)
                continue

            ok, detail = await asyncio.to_thread(run_mapped_command, mapping, payload)
            status = "ok" if ok else "error"
            print(f"{time.strftime('%H:%M:%S')} {app_id}/{command}: {status} {detail}", flush=True)
            reply = f"{status}:{command}".encode("utf-8")
            try:
                await client.write_gatt_char(
                    APP_BRIDGE_COMMANDS_CHAR_UUID,
                    encode_app_bridge_frame(app_id, status, reply),
                    response=False,
                )
            except Exception as exc:
                print(f"reply failed: {exc}", flush=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="tumoflip BLE App Bridge host daemon")
    parser.add_argument(
        "--config",
        default="tools/FlipperRelay/commands.example.json",
        help="Path to bridge JSON config",
    )
    parser.add_argument("--ble-target", help="Override BLE name fragment or CoreBluetooth UUID")
    parser.add_argument("--scan-timeout", type=float, help="Override BLE scan timeout")
    parser.add_argument("--once", action="store_true", help="Exit after the BLE connection closes")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    cfg = load_config(Path(args.config))
    if args.ble_target:
        cfg = BridgeConfig(args.ble_target, cfg.scan_timeout, cfg.default_timeout, cfg.commands)
    if args.scan_timeout is not None:
        cfg = BridgeConfig(cfg.ble_target, args.scan_timeout, cfg.default_timeout, cfg.commands)

    backoff = 2.0
    while True:
        try:
            asyncio.run(run_once(cfg))
            if args.once:
                return 0
            backoff = 2.0
        except KeyboardInterrupt:
            return 130
        except Exception as exc:
            print(f"bridge unavailable: {exc}; retrying in {backoff:.0f}s...", flush=True)
            if args.once:
                return 1
            time.sleep(backoff)
            backoff = min(backoff * 1.5, 20.0)


if __name__ == "__main__":
    raise SystemExit(main())
