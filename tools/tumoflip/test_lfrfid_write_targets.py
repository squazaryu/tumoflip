#!/usr/bin/env python3
"""Regression contracts for the API-safe LFRFID write-target settings port."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class LfRfidWriteTargetTests(unittest.TestCase):
    def test_settings_persistence_is_not_linked_into_the_core_library(self) -> None:
        api = source("targets/f7/api_symbols.csv")
        app = source("applications/main/lfrfid/application.fam")
        cli = source("applications/main/lfrfid/application.fam")

        self.assertFalse((ROOT / "lib/lfrfid/lfrfid_settings.c").exists())
        self.assertTrue((ROOT / "applications/main/lfrfid/lfrfid_settings.c").exists())
        self.assertIn('sources=["plugins/settings/*.c", "lfrfid_settings.c"]', app)
        self.assertIn('sources=["lfrfid_cli.c", "lfrfid_settings.c"]', cli)
        self.assertNotIn("Function,+,lfrfid_settings_get_write_targets", api)
        self.assertNotIn("Function,+,lfrfid_settings_set_write_targets", api)

    def test_target_table_covers_every_default_chip_variant(self) -> None:
        header = source("lib/lfrfid/lfrfid_write_targets.h")
        worker = source("lib/lfrfid/lfrfid_worker_modes.c")
        plugin = source("applications/main/lfrfid/plugins/settings/lfrfid_settings_write_targets.c")

        for name in (
            "LFRFIDWriteTargetT5577",
            "LFRFIDWriteTargetEM4305",
            "LFRFIDWriteTargetHitagMicro8265",
            "LFRFIDWriteTargetHitagMicro8210",
            "LFRFIDWriteTargetHitagMicroH55",
        ):
            self.assertIn(name, header)
        self.assertIn("LFRFIDWriteTargetHitagS8268", header)
        self.assertIn("LFRFIDWriteTargetMax", plugin)
        self.assertIn("LFRFID_WRITE_TARGET_MASK_ALL", header)
        self.assertIn("LFRFIDWriteTargetMax < 32", header)
        self.assertIn('if(target == LFRFIDWriteTargetHitagS8268) return "8268"', worker)
        self.assertIn('if(target == LFRFIDWriteTargetHitagS8268) return "8268"', plugin)

    def test_hitags_writer_is_opt_in_and_clock_limited(self) -> None:
        header = source("lib/lfrfid/protocols/lfrfid_protocols.h")
        protocol = source("lib/lfrfid/protocols/protocol_em4100.c")
        worker = source("lib/lfrfid/lfrfid_worker_modes.c")
        module = ROOT / "lib/lfrfid/tools/hitags.c"
        module_header = ROOT / "lib/lfrfid/tools/hitags.h"

        self.assertTrue(module.exists())
        self.assertTrue(module_header.exists())
        self.assertIn("LFRFIDWriteTypeHitagS", header)
        self.assertIn("LFRFIDHitagS hitags", header)
        self.assertIn("protocol->clock_per_bit == 64", protocol)
        self.assertIn("hitags_plugin->read_uid", worker)
        self.assertIn("if(attempted) hitags_plugin->write", worker)
        self.assertIn("#ifndef FURI_NDEBUG", source("lib/lfrfid/tools/hitags.c"))

        settings = source("applications/main/lfrfid/lfrfid_settings.c")
        worker_alloc = source("lib/lfrfid/lfrfid_worker.c")
        self.assertIn("lfrfid_write_targets_default()", settings)
        self.assertIn("lfrfid_write_targets_default()", worker_alloc)
        targets = source("lib/lfrfid/lfrfid_write_targets.c")
        self.assertIn("LFRFIDWriteTargetHitagS8268", targets)
        self.assertIn("return LFRFID_WRITE_TARGET_MASK_ALL & ~", targets)

    def test_hitags_runtime_is_a_lazy_package_plugin(self) -> None:
        library = source("lib/lfrfid/SConscript")
        worker = source("lib/lfrfid/lfrfid_worker_modes.c")
        manifest_path = ROOT / "applications_user/lfrfid_hitags/application.fam"

        self.assertTrue(manifest_path.exists())
        manifest = manifest_path.read_text(encoding="utf-8")
        self.assertIn('appid="lfrfid_hitags"', manifest)
        self.assertIn('requires=["lfrfid"]', manifest)
        self.assertIn("fal_embedded=False", manifest)
        self.assertIn("fap_package_only=True", manifest)
        self.assertIn("../../lib/lfrfid/tools/hitags.c", manifest)
        self.assertIn('exclude=["hitags.c"]', library)
        self.assertIn("plugin_manager_load_single", worker)
        self.assertIn('APP_ASSETS_PATH("plugins/lfrfid_hitags.fal")', worker)
        self.assertIn("LFRFID_HITAGS_PLUGIN_API_VERSION", worker)

    def test_write_loop_uses_settings_mask_as_support_probe(self) -> None:
        worker = source("lib/lfrfid/lfrfid_worker_modes.c")
        snapshot = worker.index("protocol_dict_get_data(worker->protocols, protocol, verify_data")
        mask = worker.index("LFRFIDWriteTargetMask targets = worker->write_target_mask")
        self.assertLess(snapshot, mask)
        self.assertIn("This is also the support probe", worker)
        self.assertIn("while(targets != 0 && !done", worker)
        self.assertIn("LFRFIDWorkerWriteNoEnabledTarget", worker)

    def test_settings_default_is_all_enabled_and_persists_only_known_bits(self) -> None:
        settings = source("applications/main/lfrfid/lfrfid_settings.c")
        self.assertIn("return lfrfid_write_targets_default()", settings)
        self.assertIn("mask & LFRFID_WRITE_TARGET_MASK_ALL", settings)
        self.assertIn("settings.write_target_mask & LFRFID_WRITE_TARGET_MASK_ALL", settings)
        self.assertIn("saved_struct_load", settings)
        self.assertIn("saved_struct_save", settings)

    def test_app_and_cli_use_the_same_persisted_mask(self) -> None:
        app = source("applications/main/lfrfid/scenes/lfrfid_scene_write.c")
        cli = source("applications/main/lfrfid/lfrfid_cli.c")
        self.assertIn("lfrfid_settings_get_write_targets()", app)
        self.assertIn("lfrfid_worker_set_write_targets(app->lfworker, enabled)", app)
        self.assertIn("lfrfid_worker_set_write_targets(worker, lfrfid_settings_get_write_targets())", cli)
        self.assertIn("LfRfidEventWriteNoEnabledTarget", app)

    def test_api_minor_bump_exports_only_the_new_scalar_surface(self) -> None:
        api = source("targets/f7/api_symbols.csv")
        self.assertEqual(api.splitlines()[1], "Version,+,88.12,,")
        for symbol in (
            "lfrfid_worker_set_write_targets",
            "lfrfid_write_targets_default",
        ):
            self.assertIn(symbol, api)
        self.assertNotIn("lfrfid_write_targets_supported", api)
        self.assertNotIn("lfrfid_write_target_variant", api)
        self.assertIn("Header,+,lib/lfrfid/lfrfid_write_targets.h,,", api)

    def test_keri_keeps_one_buffer_per_timing_candidate(self) -> None:
        keri = source("lib/lfrfid/protocols/protocol_keri.c")
        self.assertNotIn("negative_encoded_data", keri)
        self.assertIn("inverted", keri)
        self.assertIn("memcmp(data + 4, data + 12, KERI_DECODED_DATA_SIZE)", keri)
        self.assertIn("memcpy(data_to, data_from + 4, KERI_DECODED_DATA_SIZE)", keri)

    def test_target_mapping_stays_local_except_for_the_default_mask(self) -> None:
        api = source("targets/f7/api_symbols.csv")
        worker = source("lib/lfrfid/lfrfid_worker_modes.c")
        plugin = source("applications/main/lfrfid/plugins/settings/lfrfid_settings_write_targets.c")
        target_defaults = source("lib/lfrfid/lfrfid_write_targets.c")

        self.assertTrue((ROOT / "lib/lfrfid/lfrfid_write_targets.c").exists())
        self.assertNotIn("lfrfid_write_target_type", api)
        self.assertNotIn("lfrfid_write_target_name", api)
        self.assertIn("lfrfid_worker_write_target_name", worker)
        self.assertIn('"T5577"', plugin)
        self.assertIn("lfrfid_write_targets_default", target_defaults)

    def test_write_failure_diagnostic_does_not_format_a_target_name(self) -> None:
        worker = source("lib/lfrfid/lfrfid_worker_modes.c")
        self.assertIn('FURI_LOG_E(TAG, "Encoding target %u failed"', worker)


if __name__ == "__main__":
    unittest.main()
