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
            ("tmwhflpprarf", "089", "031"),
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
"""
        updated = sync_readme_text(original, "tmwhflpprarf089-031", "v0.3.1")

        self.assertIn("Firmware version: `tmwhflpprarf089-031`", updated)
        self.assertIn("Release: `v0.3.1`", updated)
        self.assertIn("flipper-z-f7-update-tmwhflpprarf089-031.tgz", updated)
        self.assertIn("- `031`: tumoflip internal build version.", updated)
        self.assertNotIn("tmwhflpprarf089-030", updated)

    def test_readme_is_synced_with_dist_suffix(self) -> None:
        readme = (REPO_ROOT / "ReadMe.md").read_text(encoding="utf-8")
        self.assertEqual(sync_readme_text(readme, fbt_options.DIST_SUFFIX), readme)


if __name__ == "__main__":
    unittest.main()
