import tempfile
import unittest
from pathlib import Path

try:
    from .protocol_compiler import (
        MAX_PULSES_PER_CAPTURE,
        ProtocolCompilerError,
        _parse_profile_text,
        compile_profile,
        decode_capture,
        parse_sub_capture,
        profile_to_text,
    )
except ImportError:
    from protocol_compiler import (
        MAX_PULSES_PER_CAPTURE,
        ProtocolCompilerError,
        _parse_profile_text,
        compile_profile,
        decode_capture,
        parse_sub_capture,
        profile_to_text,
    )


SHORT_US = 400
LONG_US = 1200
FRAME_GAP_US = 8000


def parity_even(value: int, bit_count: int) -> int:
    return sum((value >> index) & 1 for index in range(bit_count)) & 1


def encoded_frame(value: int, data_bits: int, jitter: int = 0) -> list[int]:
    short = SHORT_US + jitter
    long = LONG_US + jitter * 2
    pulses = [short, -short] * 4
    pulses.extend((short, -long))
    bits = [((value >> index) & 1) for index in range(data_bits - 1, -1, -1)]
    bits.append(parity_even(value, data_bits))
    for bit in bits:
        pulses.extend((long, -short) if bit else (short, -long))
    return pulses


def write_capture(path: Path, value: int, jitter: int = 0, repeat: bool = True) -> None:
    frame = encoded_frame(value, 11, jitter)
    pulses = frame + [-FRAME_GAP_US]
    if repeat:
        pulses += frame + [-FRAME_GAP_US]
    midpoint = len(pulses) // 2
    lines = (
        "Filetype: Flipper SubGhz RAW File",
        "Version: 1",
        "Frequency: 433920000",
        "Preset: FuriHalSubGhzPresetOok650Async",
        "Protocol: RAW",
        "RAW_Data: " + " ".join(str(value) for value in pulses[:midpoint]),
        "RAW_Data: " + " ".join(str(value) for value in pulses[midpoint:]),
        "",
    )
    path.write_text("\n".join(lines), encoding="utf-8")


class ProtocolCompilerTests(unittest.TestCase):
    def _training(self, directory: Path):
        values = (0x500, 0x5FF, 0x555, 0x5AA)
        captures = []
        for index, value in enumerate(values):
            path = directory / f"train_{index}.sub"
            write_capture(path, value, jitter=(-12, 8, -5, 14)[index])
            captures.append(parse_sub_capture(path))
        return captures

    def test_compiles_and_decodes_held_out_capture(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            profile = compile_profile("Demo Pulse Pair", self._training(directory))
            held_out = directory / "validation.sub"
            write_capture(held_out, 0x53C, jitter=6)
            result = decode_capture(profile, parse_sub_capture(held_out))

            self.assertEqual(profile.bit_count, 13)
            self.assertEqual(profile.checksum, "parity-even-last")
            self.assertTrue(profile.receive_only)
            self.assertTrue(profile.review_required)
            self.assertTrue(result.stable_match)
            self.assertTrue(result.checksum_match)
            self.assertEqual(result.value & 0xFFF, (0x53C << 1) | parity_even(0x53C, 11))

    def test_profile_output_is_deterministic_and_round_trips(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            captures = self._training(directory)
            first = compile_profile("Demo Pulse Pair", captures)
            second = compile_profile("Demo Pulse Pair", captures)
            self.assertEqual(profile_to_text(first), profile_to_text(second))

            path = directory / "demo.tproto"
            path.write_text(profile_to_text(first), encoding="utf-8")
            loaded = _parse_profile_text(path)
            self.assertEqual(loaded, first)

            path.write_text(
                profile_to_text(first).replace("Minimum API: 88", "Minimum API: 87"),
                encoding="utf-8",
            )
            with self.assertRaises(ProtocolCompilerError):
                _parse_profile_text(path)

            path.write_text(
                profile_to_text(first).replace("Stable mask: 0x1E00", "Stable mask: 0xFFFF"),
                encoding="utf-8",
            )
            with self.assertRaises(ProtocolCompilerError):
                _parse_profile_text(path)

            path.write_text(profile_to_text(first) + "Name: duplicate\n", encoding="utf-8")
            with self.assertRaises(ProtocolCompilerError):
                _parse_profile_text(path)

    def test_rejects_held_out_capture_on_another_frequency(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            profile = compile_profile("Demo Pulse Pair", self._training(directory))
            held_out = directory / "validation.sub"
            write_capture(held_out, 0x53C, jitter=6)
            text = held_out.read_text(encoding="utf-8").replace(
                "Frequency: 433920000", "Frequency: 315000000"
            )
            held_out.write_text(text, encoding="utf-8")
            with self.assertRaises(ProtocolCompilerError):
                decode_capture(profile, parse_sub_capture(held_out))

    def test_ambiguous_training_is_marked_for_review(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            first = directory / "first.sub"
            second = directory / "second.sub"
            write_capture(first, 0x555)
            write_capture(second, 0x555, jitter=4)
            profile = compile_profile(
                "Ambiguous", [parse_sub_capture(first), parse_sub_capture(second)]
            )
            self.assertIn("checksum-unknown", profile.ambiguity)
            self.assertIn("no-changing-fields", profile.ambiguity)
            self.assertEqual(profile.variable_mask, 0)
            self.assertTrue(profile.review_required)

    def test_rejects_malformed_and_unbounded_captures(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            malformed = directory / "malformed.sub"
            malformed.write_text(
                "Filetype: Flipper SubGhz RAW File\n"
                "Version: 1\nProtocol: RAW\nRAW_Data: 400 0 -1200\n",
                encoding="utf-8",
            )
            with self.assertRaises(ProtocolCompilerError):
                parse_sub_capture(malformed)

            oversized = directory / "oversized.sub"
            oversized.write_text(
                "Filetype: Flipper SubGhz RAW File\n"
                "Version: 1\nProtocol: RAW\nRAW_Data: "
                + " ".join("400" if index % 2 == 0 else "-400" for index in range(MAX_PULSES_PER_CAPTURE + 1))
                + "\n",
                encoding="utf-8",
            )
            with self.assertRaises(ProtocolCompilerError):
                parse_sub_capture(oversized)

    def test_rejects_non_pair_hypothesis(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            captures = []
            for index in range(2):
                path = directory / f"invalid_{index}.sub"
                path.write_text(
                    "Filetype: Flipper SubGhz RAW File\n"
                    "Version: 1\nFrequency: 433920000\nProtocol: RAW\n"
                    "RAW_Data: 400 -400 400 -400 400 -400 400 -400 -8000\n",
                    encoding="utf-8",
                )
                captures.append(parse_sub_capture(path))
            with self.assertRaises(ProtocolCompilerError):
                compile_profile("Invalid", captures)


if __name__ == "__main__":
    unittest.main()
