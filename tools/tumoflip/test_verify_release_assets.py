#!/usr/bin/env python3

import hashlib
import tempfile
import unittest
from pathlib import Path

try:
    from .verify_release_assets import (
        ReleaseAssetError,
        parse_sha256_sums,
        verify_release_assets,
    )
except ImportError:
    from verify_release_assets import (
        ReleaseAssetError,
        parse_sha256_sums,
        verify_release_assets,
    )


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class VerifyReleaseAssetsTest(unittest.TestCase):
    def test_verifies_exact_asset_set(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "firmware.dfu"
            second = root / "tumoflip-packages.json"
            first.write_bytes(b"firmware")
            second.write_bytes(b"packages")
            checksums = root / "SHA256SUMS"
            checksums.write_text(
                f"{digest(first.read_bytes())}  {first.name}\n"
                f"{digest(second.read_bytes())} *{second.name}\n",
                encoding="utf-8",
            )

            verify_release_assets(checksums, [first, second])

    def test_rejects_hash_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            asset = root / "firmware.dfu"
            asset.write_bytes(b"changed")
            checksums = root / "SHA256SUMS"
            checksums.write_text(f"{digest(b'original')}  {asset.name}\n")

            with self.assertRaisesRegex(ReleaseAssetError, "SHA-256 mismatch"):
                verify_release_assets(checksums, [asset])

    def test_rejects_missing_or_extra_ledger_entries(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            asset = root / "firmware.dfu"
            asset.write_bytes(b"firmware")
            checksums = root / "SHA256SUMS"
            checksums.write_text(
                f"{digest(asset.read_bytes())}  {asset.name}\n"
                f"{digest(b'extra')}  unexpected.zip\n"
            )

            with self.assertRaisesRegex(ReleaseAssetError, "asset set differs"):
                verify_release_assets(checksums, [asset])

    def test_rejects_unsafe_duplicate_and_malformed_entries(self) -> None:
        cases = (
            f"{digest(b'x')}  ../firmware.dfu\n",
            f"{digest(b'x')}  firmware.dfu\n{digest(b'y')}  firmware.dfu\n",
            "not-a-digest  firmware.dfu\n",
            "\n",
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "SHA256SUMS"
            for content in cases:
                with self.subTest(content=content):
                    path.write_text(content, encoding="utf-8")
                    with self.assertRaises(ReleaseAssetError):
                        parse_sha256_sums(path)


if __name__ == "__main__":
    unittest.main()
