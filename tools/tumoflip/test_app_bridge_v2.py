#!/usr/bin/env python3

import unittest

try:
    from .app_bridge_v2 import Frame, decode, encode
except ImportError:
    from app_bridge_v2 import Frame, decode, encode


class AppBridgeV2Test(unittest.TestCase):
    def test_runtime_ping_vector(self) -> None:
        frame = Frame(
            app_id="runtime",
            command="ping",
            request_id=0x12345678,
            flags=1,
        )
        encoded = encode(frame)
        self.assertEqual(
            encoded.hex(),
            "4641423201070400010000007856341272756e74696d6570696e67",
        )
        self.assertEqual(decode(encoded), frame)

    def test_chunk_round_trip(self) -> None:
        frame = Frame(
            app_id="runtime",
            command="capabilities",
            request_id=7,
            chunk_index=1,
            chunk_count=3,
            payload=b"part-2",
        )
        self.assertEqual(decode(encode(frame)), frame)

    def test_rejects_invalid_chunk(self) -> None:
        with self.assertRaises(ValueError):
            encode(Frame("runtime", "ping", 1, chunk_index=1, chunk_count=1))

    def test_rejects_oversized_payload(self) -> None:
        self.assertEqual(
            len(encode(Frame("x", "y", 1, payload=bytes(160)))),
            178,
        )
        with self.assertRaises(ValueError):
            encode(Frame("runtime", "ping", 1, payload=bytes(161)))

    def test_rejects_reserved_and_unknown_flags(self) -> None:
        encoded = bytearray(encode(Frame("runtime", "ping", 1)))
        encoded[9] = 1
        with self.assertRaises(ValueError):
            decode(bytes(encoded))

        encoded[9] = 0
        encoded[4] = 0x80
        with self.assertRaises(ValueError):
            decode(bytes(encoded))
        with self.assertRaises(ValueError):
            encode(Frame("runtime", "ping", 1, flags=0x80))

    def test_rejects_trailing_and_truncated_frames(self) -> None:
        encoded = encode(Frame("runtime", "ping", 1))
        with self.assertRaises(ValueError):
            decode(encoded[:-1])
        with self.assertRaises(ValueError):
            decode(encoded + b"\x00")

    def test_rejects_invalid_utf8(self) -> None:
        encoded = bytearray(encode(Frame("x", "ping", 1)))
        encoded[16] = 0xFF
        with self.assertRaises(UnicodeDecodeError):
            decode(bytes(encoded))


if __name__ == "__main__":
    unittest.main()
