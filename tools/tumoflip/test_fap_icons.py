#!/usr/bin/env python3

import re
import unittest
from pathlib import Path

from PIL import Image


REPO_ROOT = Path(__file__).resolve().parents[2]
APPLICATIONS_USER = REPO_ROOT / "applications_user"


def external_app_blocks(manifest: str) -> list[str]:
    return [
        block
        for block in re.findall(r"App\((.*?)\n\)", manifest, re.DOTALL)
        if "FlipperAppType.EXTERNAL" in block
    ]


def string_field(block: str, field: str) -> str | None:
    match = re.search(rf'\b{field}\s*=\s*"([^"]+)"', block)
    return match.group(1) if match else None


class FapIconsTest(unittest.TestCase):
    def test_all_user_external_apps_have_valid_list_icons(self) -> None:
        checked = 0
        for manifest_path in sorted(APPLICATIONS_USER.rglob("application.fam")):
            manifest = manifest_path.read_text(encoding="utf-8")
            for block in external_app_blocks(manifest):
                appid = string_field(block, "appid") or manifest_path.parent.name
                icon_name = string_field(block, "fap_icon")
                with self.subTest(appid=appid):
                    self.assertIsNotNone(icon_name, "external app has no fap_icon")
                    icon_path = manifest_path.parent / str(icon_name)
                    self.assertTrue(icon_path.is_file(), f"missing icon: {icon_path}")
                    with Image.open(icon_path) as icon:
                        self.assertEqual(icon.size, (10, 10))
                        self.assertEqual(icon.convert("1").getextrema(), (0, 255))
                checked += 1

        self.assertGreater(checked, 0)


if __name__ == "__main__":
    unittest.main()
