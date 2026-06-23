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


if __name__ == "__main__":
    unittest.main()
