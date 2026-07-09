#!/usr/bin/env python3

import unittest

import fbt_options

from tools.tumoflip.sync_readme_version import (
    REPO_ROOT,
    parse_dist_suffix,
    sync_readme_text,
)


class ReadmeVersionSyncTest(unittest.TestCase):
    def test_parse_dist_suffix(self) -> None:
        self.assertEqual(
            parse_dist_suffix("tmwhflpprarf089-031"),
            ("tmwhflpprarf", "089", "031", None),
        )
        self.assertEqual(
            parse_dist_suffix("t-dev-089-035-001"),
            ("t-dev", "089", "035", "001"),
        )

    def test_sync_updates_versions_and_release_tag(self) -> None:
        original = """# tumoflip
- Firmware version: `tmwhflpprarf089-030`
- Release: `v0.3.0` published release (hardware validation in progress)
- Release package: `flipper-z-f7-update-tmwhflpprarf089-030.tgz`
tmwhflpprarf089-030
- `tmwhflpprarf`: tumoflip firmware name shown as the installed firmware
- `089`: upstream Unleashed base version.
- `030`: tumoflip internal build version.
- `001`: development iteration inside the tumoflip internal build version.
"""
        updated = sync_readme_text(original, "tmwhflpprarf089-031", "v0.3.1")

        self.assertIn("Firmware version: `tmwhflpprarf089-031`", updated)
        self.assertIn("Release: `v0.3.1`", updated)
        self.assertIn("flipper-z-f7-update-tmwhflpprarf089-031.tgz", updated)
        self.assertIn("- `031`: tumoflip internal build version.", updated)
        self.assertNotIn("tmwhflpprarf089-030", updated)

    def test_sync_updates_dev_versions(self) -> None:
        original = """# tumoflip
- Firmware version: `tmwhflpprarf089-034`
- Release channel: `main` stable line
- Release package: `flipper-z-f7-update-tmwhflpprarf089-034.tgz`
tmwhflpprarf089-034
- `t-dev`: Tumoflip development build prefix for unstable builds.
- `089`: upstream Unleashed base version.
- `034`: tumoflip internal build version.
- `000`: development iteration inside the tumoflip internal build version.
"""
        updated = sync_readme_text(original, "t-dev-089-035-001")

        self.assertIn("Firmware version: `t-dev-089-035-001`", updated)
        self.assertIn("Release channel: `dev experimental line`", updated)
        self.assertIn("flipper-z-f7-update-t-dev-089-035-001.tgz", updated)
        self.assertIn("- `035`: tumoflip internal build version.", updated)
        self.assertIn(
            "- `001`: development iteration inside the tumoflip internal build version.",
            updated,
        )
        self.assertNotIn("tmwhflpprarf089-034", updated)

    def test_readme_is_synced_with_dist_suffix(self) -> None:
        readme = (REPO_ROOT / "ReadMe.md").read_text(encoding="utf-8")
        self.assertEqual(sync_readme_text(readme, fbt_options.DIST_SUFFIX), readme)


if __name__ == "__main__":
    unittest.main()
