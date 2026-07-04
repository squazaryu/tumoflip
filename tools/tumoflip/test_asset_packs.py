#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class AssetPacksTest(unittest.TestCase):
    def test_loader_asset_pack_is_limited_to_custom_ok_menu_icons(self) -> None:
        source = (
            REPO_ROOT / "applications/services/loader/loader_asset_pack.c"
        ).read_text(encoding="utf-8")
        header = (
            REPO_ROOT / "applications/services/loader/loader_asset_pack.h"
        ).read_text(encoding="utf-8")
        loader_menu = (
            REPO_ROOT / "applications/services/loader/loader_menu.c"
        ).read_text(encoding="utf-8")

        self.assertIn(
            'EXT_PATH("apps_data/tumoflip/asset_packs/active.txt")',
            source,
        )
        self.assertIn(
            'EXT_PATH("apps_data/tumoflip/asset_packs/%s/manifest.txt")',
            source,
        )
        self.assertIn(
            'EXT_PATH("apps_data/tumoflip/asset_packs/%s/Icons/%s")',
            source,
        )
        self.assertIn('"ModuleOne_14.bmx"', source)
        self.assertNotIn('"ARFTools_14.bmx"', source)
        self.assertIn("LoaderAssetPackIconModuleOne", header)
        self.assertNotIn("LoaderAssetPackIconArfTools", header)
        self.assertIn('#include "loader_asset_pack.h"', loader_menu)
        self.assertIn("loader_asset_pack_get_icon", loader_menu)
        self.assertIn("LoaderAssetPackIconModuleOne", loader_menu)
        self.assertNotIn("LoaderAssetPackIconArfTools", loader_menu)

    def test_asset_pack_loader_has_safe_fallback_boundaries(self) -> None:
        source = (
            REPO_ROOT / "applications/services/loader/loader_asset_pack.c"
        ).read_text(encoding="utf-8")

        self.assertIn("LOADER_ASSET_PACK_NAME_MAX", source)
        self.assertIn("LOADER_ASSET_PACK_MAX_ICON_FRAME_SIZE", source)
        self.assertIn("LOADER_ASSET_PACK_MANIFEST_HEADER", source)
        self.assertIn("LOADER_ASSET_PACK_MANIFEST_VERSION", source)
        self.assertIn("loader_asset_pack_validate_manifest", source)
        self.assertIn("flipper_format_read_header", source)
        self.assertIn('"Target"', source)
        self.assertIn('"ModuleOneIcon"', source)
        self.assertNotIn('"ARFToolsIcon"', source)
        self.assertIn("header.width != LOADER_ASSET_PACK_ICON_WIDTH", source)
        self.assertIn("header.height != LOADER_ASSET_PACK_ICON_HEIGHT", source)
        self.assertIn("loader_asset_pack_validate_name", source)
        self.assertIn("return icon ? &icon->icon : fallback;", source)
        self.assertIn("free(pack->module_one_icon);", source)
        self.assertNotIn("free(pack->arf_tools_icon);", source)

    def test_docs_describe_sd_layout_and_fallback(self) -> None:
        docs = (REPO_ROOT / "docs/tumoflip-asset-packs.md").read_text(
            encoding="utf-8"
        )

        self.assertIn("/ext/apps_data/tumoflip/asset_packs/active.txt", docs)
        self.assertIn("/ext/apps_data/tumoflip/asset_packs/<pack>/manifest.txt", docs)
        self.assertIn("/ext/apps_data/tumoflip/asset_packs/<pack>/Icons/", docs)
        self.assertIn("Filetype: Tumoflip Asset Pack", docs)
        self.assertIn("Target: desktop-ok-menu", docs)
        self.assertIn("ModuleOne_14.bmx", docs)
        self.assertIn("ARFTools_14.bmx", docs)
        self.assertIn("legacy optional", docs)
        self.assertIn("no SD card: built-in icons", docs)
        self.assertIn("missing or invalid manifest", docs)
        self.assertIn("Loaded", docs)
        self.assertIn("icons live only while that menu is open", docs)


if __name__ == "__main__":
    unittest.main()
