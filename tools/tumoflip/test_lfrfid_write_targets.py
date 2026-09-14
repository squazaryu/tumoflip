#!/usr/bin/env python3
"""Regression contracts for the API-safe LFRFID write-target settings port."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


def source(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


class LfRfidWriteTargetTests(unittest.TestCase):
    def test_target_table_covers_every_default_chip_variant(self) -> None:
        header = source("lib/lfrfid/lfrfid_write_targets.h")
        implementation = source("lib/lfrfid/lfrfid_write_targets.c")

        for name in (
            "LFRFIDWriteTargetT5577",
            "LFRFIDWriteTargetEM4305",
            "LFRFIDWriteTargetHitagMicro8265",
            "LFRFIDWriteTargetHitagMicro8210",
            "LFRFIDWriteTargetHitagMicroH55",
        ):
            self.assertIn(name, header)
            self.assertIn(name, implementation)
        self.assertIn("LFRFID_WRITE_TARGET_MASK_ALL", header)
        self.assertIn("LFRFIDWriteTargetMax < 32", implementation)

    def test_write_loop_snapshots_before_probe_and_intersects_settings(self) -> None:
        worker = source("lib/lfrfid/lfrfid_worker_modes.c")
        snapshot = worker.index("protocol_dict_get_data(worker->protocols, protocol, verify_data")
        probe = worker.index("lfrfid_write_targets_supported(worker->protocols, protocol)")
        intersection = worker.index("worker->write_target_mask & supported", probe)
        self.assertLess(snapshot, probe)
        self.assertLess(probe, intersection)
        self.assertIn("while(targets != 0 && !done", worker)
        self.assertIn("LFRFIDWorkerWriteNoEnabledTarget", worker)

    def test_settings_default_is_all_enabled_and_persists_only_known_bits(self) -> None:
        settings = source("lib/lfrfid/lfrfid_settings.c")
        self.assertIn("return LFRFID_WRITE_TARGET_MASK_ALL", settings)
        self.assertIn("mask & LFRFID_WRITE_TARGET_MASK_ALL", settings)
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
        self.assertIn("Version,+,88.8,,", api)
        for symbol in (
            "lfrfid_settings_get_write_targets",
            "lfrfid_settings_set_write_targets",
            "lfrfid_worker_set_write_targets",
            "lfrfid_write_target_name",
            "lfrfid_write_targets_supported",
        ):
            self.assertIn(symbol, api)
        self.assertIn("Header,+,lib/lfrfid/lfrfid_settings.h,,", api)
        self.assertIn("Header,+,lib/lfrfid/lfrfid_write_targets.h,,", api)


if __name__ == "__main__":
    unittest.main()
