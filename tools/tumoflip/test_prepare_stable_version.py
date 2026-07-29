#!/usr/bin/env python3

import tempfile
import unittest
from pathlib import Path

from tools.tumoflip.prepare_stable_version import (
    apply_stable_version,
    compute_stable_version,
)


class PrepareStableVersionTest(unittest.TestCase):
    def test_accepts_standalone_stable_identity(self) -> None:
        self.assertEqual(
            compute_stable_version(
                "t-dev-089-041-024",
                set_suffix="t-flppr-fw-001",
            ),
            "t-flppr-fw-001",
        )

    def test_converts_dev_version_to_new_stable_identity(self) -> None:
        self.assertEqual(
            compute_stable_version("t-dev-089-037-033"),
            "t-flppr-fw-089-037",
        )

    def test_promotes_standalone_dev_to_next_immutable_serial(self) -> None:
        self.assertEqual(
            compute_stable_version("t-dev-001-003"),
            "t-flppr-fw-002",
        )

    def test_rejects_standalone_serial_overflow(self) -> None:
        with self.assertRaises(ValueError):
            compute_stable_version("t-dev-999-001")

    def test_converts_legacy_stable_identity_without_renaming_old_release(self) -> None:
        self.assertEqual(
            compute_stable_version("tmwhflpprarf089-036", build="037"),
            "t-flppr-fw-089-037",
        )

    def test_rejects_dev_suffix_as_explicit_stable_version(self) -> None:
        with self.assertRaises(ValueError):
            compute_stable_version(
                "t-dev-089-037-033",
                set_suffix="t-dev-089-037-034",
            )

    def test_updates_options_and_readme_to_standalone_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo_root = Path(directory)
            (repo_root / "fbt_options.py").write_text(
                'DIST_SUFFIX = "t-dev-089-041-024"\n',
                encoding="utf-8",
            )
            (repo_root / "ReadMe.md").write_text(
                """# tumoflip
- Firmware version: `t-dev-089-041-024`
- Release channel: `dev experimental line`
- Release package: `flipper-z-f7-update-t-dev-089-041-024.tgz`
- `t-flppr-fw`: Tumowuh Flipper Firmware stable build prefix.
- `001`: standalone Tumoflip release number.
""",
                encoding="utf-8",
            )

            old_suffix, new_suffix = apply_stable_version(
                repo_root,
                "t-flppr-fw-001",
                update_splash=False,
            )

            self.assertEqual(old_suffix, "t-dev-089-041-024")
            self.assertEqual(new_suffix, "t-flppr-fw-001")
            self.assertIn(
                'DIST_SUFFIX = "t-flppr-fw-001"',
                (repo_root / "fbt_options.py").read_text(encoding="utf-8"),
            )
            readme = (repo_root / "ReadMe.md").read_text(encoding="utf-8")
            self.assertIn("Firmware version: `t-flppr-fw-001`", readme)
            self.assertIn("Release channel: `main stable line`", readme)

    def test_updates_options_and_readme_together(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo_root = Path(directory)
            (repo_root / "fbt_options.py").write_text(
                'DIST_SUFFIX = "t-dev-089-037-033"\n',
                encoding="utf-8",
            )
            (repo_root / "ReadMe.md").write_text(
                """# tumoflip
- Firmware version: `t-dev-089-037-033`
- Release channel: `dev experimental line`
- Release package: `flipper-z-f7-update-t-dev-089-037-033.tgz`
- `t-flppr-fw`: Tumowuh Flipper Firmware stable build prefix.
- `089`: upstream Unleashed base version.
- `037`: tumoflip internal build version.
""",
                encoding="utf-8",
            )

            old_suffix, new_suffix = apply_stable_version(
                repo_root,
                "t-flppr-fw-089-037",
                update_splash=False,
            )

            self.assertEqual(old_suffix, "t-dev-089-037-033")
            self.assertEqual(new_suffix, "t-flppr-fw-089-037")
            self.assertIn(
                'DIST_SUFFIX = "t-flppr-fw-089-037"',
                (repo_root / "fbt_options.py").read_text(encoding="utf-8"),
            )
            readme = (repo_root / "ReadMe.md").read_text(encoding="utf-8")
            self.assertIn("Firmware version: `t-flppr-fw-089-037`", readme)
            self.assertIn("Release channel: `main stable line`", readme)


if __name__ == "__main__":
    unittest.main()
