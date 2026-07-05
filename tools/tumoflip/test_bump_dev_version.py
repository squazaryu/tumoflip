#!/usr/bin/env python3

import tempfile
import unittest
from pathlib import Path

import fbt_options

from tools.tumoflip.bump_dev_version import (
    apply_dev_version,
    compute_dev_version,
)
from tools.tumoflip.sync_readme_version import parse_dist_suffix


class BumpDevVersionTest(unittest.TestCase):
    def test_compute_next_iteration_by_default(self) -> None:
        self.assertEqual(
            compute_dev_version("t-dev-089-036-001"),
            "t-dev-089-036-002",
        )

    def test_new_internal_build_resets_iteration_to_one(self) -> None:
        self.assertEqual(
            compute_dev_version("t-dev-089-035-007", build="036"),
            "t-dev-089-036-001",
        )

    def test_explicit_iteration_is_preserved(self) -> None:
        self.assertEqual(
            compute_dev_version("t-dev-089-036-001", iteration="010"),
            "t-dev-089-036-010",
        )

    def test_rejects_stable_source_version(self) -> None:
        with self.assertRaises(ValueError):
            compute_dev_version("tmwhflpprarf089-035")

    def test_updates_options_and_readme_together(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            repo_root = Path(directory)
            (repo_root / "fbt_options.py").write_text(
                'DIST_SUFFIX = "t-dev-089-035-001"\n',
                encoding="utf-8",
            )
            (repo_root / "ReadMe.md").write_text(
                """# tumoflip
- Firmware version: `t-dev-089-035-001`
- Release package: `flipper-z-f7-update-t-dev-089-035-001.tgz`
- `t-dev`: Tumoflip development build prefix for unstable builds.
- `089`: upstream Unleashed base version.
- `035`: tumoflip internal build version.
- `001`: development iteration inside the tumoflip internal build version.
""",
                encoding="utf-8",
            )

            old_suffix, new_suffix = apply_dev_version(
                repo_root,
                "t-dev-089-036-001",
                update_splash=False,
            )

            self.assertEqual(old_suffix, "t-dev-089-035-001")
            self.assertEqual(new_suffix, "t-dev-089-036-001")
            self.assertIn(
                'DIST_SUFFIX = "t-dev-089-036-001"',
                (repo_root / "fbt_options.py").read_text(encoding="utf-8"),
            )
            readme = (repo_root / "ReadMe.md").read_text(encoding="utf-8")
            self.assertIn("Firmware version: `t-dev-089-036-001`", readme)
            self.assertIn(
                "flipper-z-f7-update-t-dev-089-036-001.tgz",
                readme,
            )
            self.assertIn("- `036`: tumoflip internal build version.", readme)
            self.assertIn(
                "- `001`: development iteration inside the tumoflip internal build version.",
                readme,
            )

    def test_current_dist_suffix_is_dev_version(self) -> None:
        self.assertEqual(
            parse_dist_suffix(fbt_options.DIST_SUFFIX),
            ("t-dev", "089", "036", "042"),
        )


if __name__ == "__main__":
    unittest.main()
