#!/usr/bin/env python3
"""Regression contracts for the selected Unleashed 090 LF RFID fixes."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
FRAME_MASK = (1 << 64) - 1
EPILOGUE_MASK = (1 << 9) - 1


def source(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


def frame_bits(frame: int) -> list[int]:
    return [(frame >> bit) & 1 for bit in range(63, -1, -1)]


def decode_pipeline(bits: list[int]) -> list[int]:
    """Model the decoder's 64-bit frame plus 9-bit lookahead registers."""
    encoded_data = 0
    encoded_epilogue = 0
    decoded_frames: list[int] = []

    for bit in bits:
        carry = (encoded_epilogue >> 8) & 1
        encoded_data = ((encoded_data << 1) | carry) & FRAME_MASK
        encoded_epilogue = ((encoded_epilogue << 1) | bit) & EPILOGUE_MASK

        if encoded_epilogue == EPILOGUE_MASK:
            decoded_frames.append(encoded_data)

    return decoded_frames


class LfRfidUnleashed090Test(unittest.TestCase):
    def test_em4100_uses_only_nine_bits_of_lookahead(self) -> None:
        protocol = source("lib/lfrfid/protocols/protocol_em4100.c")
        self.assertIn("#define EM_EPILOGUE_HEADER (0x1FFULL)", protocol)
        self.assertNotIn("EM_ENCODED_DATA_HEADER", protocol)
        self.assertIn("proto->encoded_epilogue = 0;", protocol)
        self.assertIn("(proto->encoded_epilogue >> 8) & 0b1", protocol)
        self.assertIn("& EM_EPILOGUE_HEADER", protocol)

    def test_nine_bit_pipeline_exposes_alternating_frames(self) -> None:
        header = EPILOGUE_MASK << 55
        frame_a = header | 0x0012_3456_789A_BCDE
        frame_b = header | 0x000F_EDCB_A987_6542
        stream = (
            frame_bits(frame_a)
            + frame_bits(frame_b)
            + frame_bits(frame_a)
            + [1] * 9
        )

        decoded = decode_pipeline(stream)
        visible = [frame for frame in decoded if frame in (frame_a, frame_b)]
        self.assertEqual(visible, [frame_a, frame_b, frame_a])

    def test_pac_stanley_resets_the_shift_buffer(self) -> None:
        protocol = source("lib/lfrfid/protocols/protocol_pac_stanley.c")
        decoder_start = protocol[
            protocol.index("void protocol_pac_stanley_decoder_start") :
            protocol.index("bool protocol_pac_stanley_decoder_feed")
        ]
        self.assertIn(
            "memset(protocol->encoded_data, 0, "
            "PAC_STANLEY_ENCODED_BYTE_FULL_SIZE);",
            decoder_start,
        )


if __name__ == "__main__":
    unittest.main()
