#!/usr/bin/env python3
"""Generate and verify the deterministic Protocol Compiler demo corpus."""

from __future__ import annotations

import argparse
from pathlib import Path

try:
    from .protocol_compiler import RawCapture, compile_profile, profile_to_text
except ImportError:
    from protocol_compiler import RawCapture, compile_profile, profile_to_text


SHORT_US = 400
LONG_US = 1200
FRAME_GAP_US = 8000
DATA_BITS = 11


def parity_even(value: int) -> int:
    return sum((value >> index) & 1 for index in range(DATA_BITS)) & 1


def encoded_frame(value: int, jitter: int) -> list[int]:
    short = SHORT_US + jitter
    long = LONG_US + jitter * 2
    pulses = [short, -short] * 4
    pulses.extend((short, -long))
    bits = [((value >> index) & 1) for index in range(DATA_BITS - 1, -1, -1)]
    bits.append(parity_even(value))
    for bit in bits:
        pulses.extend((long, -short) if bit else (short, -long))
    return pulses


def capture_pulses(value: int, jitter: int) -> tuple[int, ...]:
    frame = encoded_frame(value, jitter)
    return tuple(frame + [-FRAME_GAP_US] + frame + [-FRAME_GAP_US])


def capture_text(pulses: tuple[int, ...]) -> str:
    midpoint = len(pulses) // 2
    return "\n".join(
        (
            "Filetype: Flipper SubGhz RAW File",
            "Version: 1",
            "Frequency: 433920000",
            "Preset: FuriHalSubGhzPresetOok650Async",
            "Protocol: RAW",
            "RAW_Data: " + " ".join(str(value) for value in pulses[:midpoint]),
            "RAW_Data: " + " ".join(str(value) for value in pulses[midpoint:]),
            "",
        )
    )


def expected_outputs() -> dict[Path, str]:
    base = Path(__file__).resolve().parent
    corpus = base / "protocol_corpus" / "demo_pulse_pair"
    package = base / "sd_resources" / "apps_data" / "signal_workbench"
    values = (0x500, 0x5FF, 0x555, 0x5AA)
    jitters = (-12, 8, -5, 14)
    captures: list[RawCapture] = []
    outputs: dict[Path, str] = {}
    for index, (value, jitter) in enumerate(zip(values, jitters, strict=True)):
        pulses = capture_pulses(value, jitter)
        path = corpus / "train" / f"train_{index}.sub"
        outputs[path] = capture_text(pulses)
        captures.append(RawCapture(path=path, frequency_hz=433920000, pulses=pulses))

    validation_pulses = capture_pulses(0x53C, 6)
    validation_text = capture_text(validation_pulses)
    outputs[corpus / "validation.sub"] = validation_text
    outputs[package / "demo" / "validation.sub"] = validation_text
    outputs[package / "profiles" / "demo_pulse_pair.tproto"] = profile_to_text(
        compile_profile("Demo Pulse Pair", captures)
    )
    return outputs


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true", help="write generated artifacts")
    mode.add_argument("--check", action="store_true", help="fail if artifacts drift")
    args = parser.parse_args()

    failures: list[str] = []
    for path, expected in expected_outputs().items():
        if args.write:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(expected, encoding="utf-8")
        else:
            try:
                actual = path.read_text(encoding="utf-8")
            except (OSError, UnicodeError):
                failures.append(f"missing or unreadable: {path}")
                continue
            if actual != expected:
                failures.append(f"generated artifact drift: {path}")
    if failures:
        raise SystemExit("\n".join(failures))
    print(f"protocol demo: {'written' if args.write else 'verified'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
