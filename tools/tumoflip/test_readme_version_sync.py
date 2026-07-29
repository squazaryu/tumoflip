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
            parse_dist_suffix("t-flppr-fw-001"),
            ("t-flppr-fw", None, "001", None),
        )
        self.assertEqual(
            parse_dist_suffix("t-flppr-fw-089-037"),
            ("t-flppr-fw", "089", "037", None),
        )
        self.assertEqual(
            parse_dist_suffix("tmwhflpprarf089-031"),
            ("tmwhflpprarf", "089", "031", None),
        )
        self.assertEqual(
            parse_dist_suffix("t-dev-089-035-001"),
            ("t-dev", "089", "035", "001"),
        )
        self.assertEqual(
            parse_dist_suffix("t-dev-002-001"),
            ("t-dev", None, "002", "001"),
        )

    def test_sync_updates_standalone_stable_version(self) -> None:
        original = """# tumoflip
- Firmware version: `t-dev-089-041-024`
- Release channel: `dev experimental line`
- Release package: `flipper-z-f7-update-t-dev-089-041-024.tgz`
- `t-flppr-fw`: Tumowuh Flipper Firmware stable build prefix.
- `001`: standalone Tumoflip release number.
"""
        updated = sync_readme_text(original, "t-flppr-fw-001")

        self.assertIn("Firmware version: `t-flppr-fw-001`", updated)
        self.assertIn("Release channel: `main stable line`", updated)
        self.assertIn("flipper-z-f7-update-t-flppr-fw-001.tgz", updated)
        self.assertIn("- `001`: standalone Tumoflip release number.", updated)

    def test_sync_updates_new_stable_version(self) -> None:
        original = """# tumoflip
- Firmware version: `t-dev-089-037-033`
- Release channel: `dev experimental line`
- Release package: `flipper-z-f7-update-t-dev-089-037-033.tgz`
- `t-flppr-fw`: Tumowuh Flipper Firmware stable build prefix.
- `089`: upstream Unleashed base version.
- `037`: tumoflip internal build version.
"""
        updated = sync_readme_text(original, "t-flppr-fw-089-037")

        self.assertIn("Firmware version: `t-flppr-fw-089-037`", updated)
        self.assertIn("Release channel: `main stable line`", updated)
        self.assertIn("flipper-z-f7-update-t-flppr-fw-089-037.tgz", updated)

    def test_sync_keeps_legacy_stable_version_supported(self) -> None:
        original = """# tumoflip
- Firmware version: `tmwhflpprarf089-036`
- Release channel: `main stable line`
- Release package: `flipper-z-f7-update-tmwhflpprarf089-036.tgz`
- `tmwhflpprarf`: legacy stable prefix kept for existing releases.
- `089`: upstream Unleashed base version.
- `036`: tumoflip internal build version.
"""
        updated = sync_readme_text(original, "tmwhflpprarf089-036")
        self.assertEqual(updated, original)

    def test_sync_updates_versions_and_release_tag(self) -> None:
        original = """# tumoflip
- Firmware version: `tmwhflpprarf089-030`
- Release: `v0.3.0` published release (hardware validation in progress)
- Release package: `flipper-z-f7-update-tmwhflpprarf089-030.tgz`
tmwhflpprarf089-030
- `tmwhflpprarf`: legacy stable prefix kept for existing releases.
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

    def test_sync_updates_target_stable_semver(self) -> None:
        original = """# tumoflip
- Firmware version: `t-dev-001-003`
- Target stable SemVer: `v1.0.1`
- Release channel: `dev experimental line`
- Release package: `flipper-z-f7-update-t-dev-001-003.tgz`
- `t-dev`: Tumoflip development build prefix for unstable builds.
- `001`: standalone Tumoflip release number.
- `003`: development iteration inside the standalone Tumoflip release number.
"""
        updated = sync_readme_text(
            original,
            "t-dev-001-003",
            release_tag="v1.0.2",
        )

        self.assertIn("Target stable SemVer: `v1.0.2`", updated)

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

    def test_sync_starts_dev_iteration_from_stable_readme(self) -> None:
        original = """# tumoflip
- Firmware version: `t-flppr-fw-089-039`
- Release channel: `main stable line`
- Release package: `flipper-z-f7-update-t-flppr-fw-089-039.tgz`
- `t-dev`: Tumoflip development build prefix for unstable builds.
- `089`: upstream Unleashed base version.
- `039`: tumoflip internal build version.
- `<iteration>`: monotonically increasing revision used only by `t-dev` builds.
"""
        updated = sync_readme_text(original, "t-dev-089-039-001")

        self.assertIn("Firmware version: `t-dev-089-039-001`", updated)
        self.assertIn("Release channel: `dev experimental line`", updated)
        self.assertIn(
            "- `001`: development iteration inside the tumoflip internal build version.",
            updated,
        )
        self.assertNotIn("<iteration>", updated)

    def test_sync_starts_standalone_dev_iteration_without_placeholder(self) -> None:
        original = """# tumoflip
- Firmware version: `t-flppr-fw-001`
- Release channel: `main stable line`
- Release package: `flipper-z-f7-update-t-flppr-fw-001.tgz`
- `t-dev`: Tumoflip development build prefix for unstable builds.
- `001`: standalone Tumoflip release number.

The first standalone stable line is
`t-flppr-fw-001`, published as SemVer `v1.0.0`, and remains historical.

- Rebranded firmware origin to `tumoflip` and distribution/version suffix to
  `t-flppr-fw-001`.
"""
        updated = sync_readme_text(original, "t-dev-001-001")

        self.assertIn("Firmware version: `t-dev-001-001`", updated)
        self.assertIn("Release channel: `dev experimental line`", updated)
        self.assertIn("flipper-z-f7-update-t-dev-001-001.tgz", updated)
        self.assertIn("- `001`: standalone Tumoflip release number.", updated)
        self.assertIn(
            "- `001`: development iteration inside the standalone Tumoflip release number.",
            updated,
        )
        self.assertIn(
            "`t-flppr-fw-001`, published as SemVer `v1.0.0`",
            updated,
        )
        self.assertIn(
            "distribution/version suffix to\n  `t-flppr-fw-001`.",
            updated,
        )
        self.assertNotIn(
            "`t-dev-001-001`, published as SemVer `v1.0.0`",
            updated,
        )

    def test_readme_is_synced_with_dist_suffix(self) -> None:
        readme = (REPO_ROOT / "ReadMe.md").read_text(encoding="utf-8")
        self.assertEqual(sync_readme_text(readme, fbt_options.DIST_SUFFIX), readme)


if __name__ == "__main__":
    unittest.main()
