#!/usr/bin/env python3

import hashlib
import os
import stat
import tempfile
import unittest
import zipfile
from pathlib import Path

try:
    from .make_packages_zip import PackageError, build_packages_zip, iter_package_sources
except ImportError:
    from make_packages_zip import PackageError, build_packages_zip, iter_package_sources


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


class MakePackagesZipTest(unittest.TestCase):
    def _tree(self, directory: Path, files: dict[str, bytes]) -> dict:
        """Write `files` (source -> bytes) under `directory` and return a manifest."""
        packages: dict[str, list] = {"base": [], "arf": [], "module_one": [], "protocol_packs": []}
        for source, data in files.items():
            path = directory / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            packages["base"].append(
                {"source": source, "sha256": _sha(data), "bytes": len(data),
                 "target": "/ext/" + source}
            )
        return {"schema": 2, "packages": packages}

    def test_successful_archive(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            root = Path(d) / "resources"
            out = Path(d) / "out" / "tumoflip-packages.zip"
            manifest = self._tree(root, {
                "apps/Bluetooth/flipper_companion.fap": b"companion",
                "apps_data/subghz/plugins/protocol_x.fal": b"protocol",
            })
            count = build_packages_zip(manifest, root, out)
            self.assertEqual(count, 2)
            self.assertTrue(out.is_file())
            with zipfile.ZipFile(out) as archive:
                self.assertEqual(
                    sorted(archive.namelist()),
                    ["apps/Bluetooth/flipper_companion.fap", "apps_data/subghz/plugins/protocol_x.fal"],
                )
                self.assertEqual(archive.read("apps/Bluetooth/flipper_companion.fap"), b"companion")
            # No leftover temp files.
            self.assertEqual(list(out.parent.glob("*.tmp")), [])

    def test_archive_is_reproducible_across_source_metadata_and_manifest_order(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            root = Path(d) / "resources"
            files = {
                "apps/Zeta/z.fap": b"zeta payload",
                "apps/Alpha/a.fap": b"alpha payload",
                "apps_data/subghz/plugins/protocol.fal": b"protocol payload",
            }
            manifest = self._tree(root, files)
            manifest["packages"]["arf"] = manifest["packages"].pop("base")
            manifest["packages"]["arf"].reverse()

            first = Path(d) / "first.zip"
            second = Path(d) / "second.zip"
            build_packages_zip(manifest, root, first)

            for index, source in enumerate(files):
                path = root / source
                os.utime(path, (1_700_000_000 + index, 1_700_000_000 + index))
                path.chmod(0o600 if index % 2 else 0o755)
            manifest["packages"]["arf"].reverse()
            build_packages_zip(manifest, root, second)

            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(_sha(first.read_bytes()), _sha(second.read_bytes()))

            with zipfile.ZipFile(first) as archive:
                self.assertEqual(archive.namelist(), sorted(files))
                for info in archive.infolist():
                    self.assertEqual(info.date_time, (1980, 1, 1, 0, 0, 0))
                    self.assertEqual(info.create_system, 3)
                    self.assertEqual(info.compress_type, zipfile.ZIP_DEFLATED)
                    self.assertEqual(stat.S_IMODE(info.external_attr >> 16), 0o644)
                    self.assertTrue(stat.S_ISREG(info.external_attr >> 16))
                    self.assertEqual(info.extra, b"")
                    self.assertEqual(info.comment, b"")

    def test_missing_source(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            root = Path(d) / "resources"
            root.mkdir()
            manifest = {"schema": 2, "packages": {"base": [
                {"source": "apps/gone.fap", "sha256": _sha(b"x"), "bytes": 1, "target": "/ext/x"}
            ]}}
            out = Path(d) / "out.zip"
            with self.assertRaises(PackageError):
                build_packages_zip(manifest, root, out)
            self.assertFalse(out.exists())                 # atomic: nothing published
            self.assertEqual(list(Path(d).glob("*.tmp")), [])

    def test_sha_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as d:
            root = Path(d) / "resources"
            root.mkdir()
            (root / "a.fap").write_bytes(b"real")
            manifest = {"schema": 2, "packages": {"base": [
                {"source": "a.fap", "sha256": _sha(b"different"), "bytes": 4, "target": "/ext/a.fap"}
            ]}}
            out = Path(d) / "out.zip"
            with self.assertRaises(PackageError):
                build_packages_zip(manifest, root, out)
            self.assertFalse(out.exists())

    def test_duplicate_source(self) -> None:
        manifest = {"schema": 2, "packages": {
            "base": [{"source": "a.fap", "sha256": _sha(b"x"), "bytes": 1, "target": "/ext/a"}],
            "arf": [{"source": "a.fap", "sha256": _sha(b"x"), "bytes": 1, "target": "/ext/a"}],
        }}
        with self.assertRaises(PackageError):
            iter_package_sources(manifest)

    def test_path_traversal_and_absolute_rejected(self) -> None:
        for bad in ["../escape.fap", "apps/../../etc/passwd", "/abs/path.fap", "a/./b.fap", "x\\y.fap"]:
            manifest = {"schema": 2, "packages": {"base": [
                {"source": bad, "sha256": _sha(b"x"), "bytes": 1, "target": "/ext/x"}
            ]}}
            with self.assertRaises(PackageError, msg=bad):
                iter_package_sources(manifest)

    def test_unsupported_schema(self) -> None:
        with self.assertRaises(PackageError):
            iter_package_sources({"schema": 1, "packages": {}})


if __name__ == "__main__":
    unittest.main()
