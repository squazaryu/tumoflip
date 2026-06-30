#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class SubGhzProtocolPackTest(unittest.TestCase):
    def test_receiver_config_shows_runtime_pack_status(self) -> None:
        receiver_config = (
            REPO_ROOT / "applications/main/subghz/scenes/subghz_scene_receiver_config.c"
        ).read_text(encoding="utf-8")
        txrx = (REPO_ROOT / "applications/main/subghz/helpers/subghz_txrx.c").read_text(
            encoding="utf-8"
        )

        self.assertIn('"Protocol Pack"', receiver_config)
        self.assertIn('"Pack Status"', receiver_config)
        self.assertIn("%zu/%zu %s", receiver_config)
        self.assertIn('"ERR"', receiver_config)
        self.assertIn("subghz_txrx_reload_protocol_pack(subghz->txrx, index)", receiver_config)
        self.assertIn("subghz->last_settings->protocol_pack_group = index", receiver_config)
        self.assertIn("variable_item_set_current_value_index(item, previous)", receiver_config)

        self.assertIn("const bool resume_rx = instance->txrx_state == SubGhzTxRxStateRx", txrx)
        self.assertIn("subghz_receiver_free(instance->receiver)", txrx)
        self.assertIn("subghz_protocol_pack_registry_free(instance->protocol_pack_registry)", txrx)
        self.assertIn("subghz_receiver_set_rx_callback(", txrx)
        self.assertIn("if(resume_rx) subghz_txrx_rx_start(instance);", txrx)

    def test_core_is_default_pack_for_core_and_arf_subghz(self) -> None:
        files = [
            REPO_ROOT / "applications/main/subghz/subghz_last_settings.c",
            REPO_ROOT / "applications_user/arf_subghz_full/subghz_last_settings.c",
            REPO_ROOT / "applications/main/subghz/scenes/subghz_scene_receiver_config.c",
            REPO_ROOT / "applications_user/arf_subghz_full/scenes/subghz_scene_receiver_config.c",
        ]

        for path in files:
            with self.subTest(path=path):
                source = path.read_text(encoding="utf-8")
                self.assertIn("SubGhzProtocolPackGroupCore", source)
                self.assertNotIn("protocol_pack_group = SubGhzProtocolPackGroupLegacy", source)
                self.assertNotIn(
                    "subghz_txrx_reload_protocol_pack(subghz->txrx, SubGhzProtocolPackGroupLegacy)",
                    source,
                )

        docs = (REPO_ROOT / "docs/subghz-protocol-packs.md").read_text(encoding="utf-8")
        self.assertIn("`Core` is the default", docs)
        self.assertNotIn("`Legacy` is the default", docs)

        registry = (REPO_ROOT / "lib/subghz/protocols/plugin_registry.c").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            "if(group >= SubGhzProtocolPackGroupCount) group = SubGhzProtocolPackGroupCore;",
            registry,
        )
        self.assertNotIn(
            "if(group >= SubGhzProtocolPackGroupCount) group = SubGhzProtocolPackGroupLegacy;",
            registry,
        )

    def test_pack_status_screen_lists_files_and_error_causes(self) -> None:
        info_scene = (
            REPO_ROOT / "applications/main/subghz/scenes/subghz_scene_protocol_pack_info.c"
        ).read_text(encoding="utf-8")
        registry = (REPO_ROOT / "lib/subghz/protocols/plugin_registry.c").read_text(
            encoding="utf-8"
        )
        docs = (REPO_ROOT / "docs/subghz-protocol-packs.md").read_text(encoding="utf-8")

        self.assertIn("Plugins: %zu/%zu", info_scene)
        self.assertIn("[ERR] ", info_scene)
        self.assertIn("entry->filename", info_scene)
        self.assertIn("entry->protocol_name", info_scene)
        self.assertIn("subghz_protocol_pack_status_get_name(entry->status)", info_scene)

        self.assertIn("Missing or invalid file", registry)
        self.assertIn("Missing imports", registry)
        self.assertIn("Plugin API mismatch", registry)
        self.assertIn("Duplicate protocol", registry)
        self.assertIn("PluginManagerLoadStatusMissingImports", registry)
        self.assertIn("report.loaded_plugin_count++", registry)

        self.assertIn("loaded versus expected external plugins", docs)
        self.assertIn("Failures distinguish missing or invalid files", docs)

    def test_shuka_toyota_protocol_name_does_not_shadow_core_toyota(self) -> None:
        core_toyota = (REPO_ROOT / "lib/subghz/protocols/toyota.h").read_text(
            encoding="utf-8"
        )
        shuka_toyota = (
            REPO_ROOT / "lib/subghz/protocols/toyota_lexus.h"
        ).read_text(encoding="utf-8")

        self.assertIn('#define SUBGHZ_PROTOCOL_TOYOTA_NAME "Toyota"', core_toyota)
        self.assertIn('#define TOYOTA_LEXUS_PROTOCOL_NAME "Toyota/Lexus"', shuka_toyota)
        self.assertNotIn('#define TOYOTA_LEXUS_PROTOCOL_NAME "Toyota"', shuka_toyota)


if __name__ == "__main__":
    unittest.main()
