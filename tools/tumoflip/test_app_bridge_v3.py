#!/usr/bin/env python3

import unittest

try:
    from .app_bridge_v2 import Frame, decode, encode
    from .app_bridge_v3 import (
        decode_fields,
        hello_payload,
        parse_hello_response,
        session_payload,
    )
except ImportError:
    from app_bridge_v2 import Frame, decode, encode
    from app_bridge_v3 import (
        decode_fields,
        hello_payload,
        parse_hello_response,
        session_payload,
    )


class AppBridgeV3Test(unittest.TestCase):
    def test_hello_payload_is_fab2_runtime_payload(self) -> None:
        payload = hello_payload("iphone")
        frame = Frame(
            app_id="runtime",
            command="hello",
            request_id=1,
            payload=payload,
        )

        decoded = decode(encode(frame))
        self.assertEqual(decoded.command, "hello")
        self.assertEqual(
            decode_fields(decoded.payload),
            {"owner": "iphone"},
        )

    def test_session_payload_normalizes_hex_session(self) -> None:
        self.assertEqual(session_payload("12ab34cd"), b"session=12AB34CD")

    def test_rejects_invalid_owner_and_session(self) -> None:
        with self.assertRaises(ValueError):
            hello_payload("bad owner")
        with self.assertRaises(ValueError):
            session_payload("not-hex")

    def test_parse_hello_response_validates_contract(self) -> None:
        parsed = parse_hello_response(b"sid=1234ABCD")
        self.assertEqual(parsed["sid"], "1234ABCD")

        with self.assertRaises(ValueError):
            parse_hello_response(b"session=1234ABCD;owner=iphone")


if __name__ == "__main__":
    unittest.main()
