#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class SubGhzRadioBrokerTest(unittest.TestCase):
    def test_broker_status_exposes_observable_lifecycle(self) -> None:
        header = (
            REPO_ROOT
            / "applications/services/subghz_radio_broker/subghz_radio_broker.h"
        ).read_text(encoding="utf-8")
        broker = (
            REPO_ROOT
            / "applications/services/subghz_radio_broker/subghz_radio_broker.c"
        ).read_text(encoding="utf-8")
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")
        bridge_docs = (REPO_ROOT / "docs/app-bridge-v2.md").read_text(encoding="utf-8")
        broker_docs = (REPO_ROOT / "docs/subghz-radio-broker.md").read_text(
            encoding="utf-8"
        )

        for state in (
            "SubGhzRadioBrokerStateIdle",
            "SubGhzRadioBrokerStateAcquired",
            "SubGhzRadioBrokerStateExternalPowerOn",
            "SubGhzRadioBrokerStateReleasing",
            "SubGhzRadioBrokerStateError",
        ):
            self.assertIn(state, header)
            self.assertIn(state, broker + runtime)

        for field in (
            "SubGhzRadioBrokerStatus base;",
            "uint32_t acquired_tick;",
            "uint32_t last_transition_tick;",
            "char last_error[SUBGHZ_RADIO_BROKER_ERROR_MAX + 1];",
        ):
            self.assertIn(field, header)

        self.assertIn("SubGhzRadioBrokerStatusV2", header)
        self.assertIn("subghz_radio_broker_get_status_v2", header)
        self.assertIn("*status = broker->status.base;", broker)
        self.assertIn("*status = broker->status;", broker)
        self.assertIn("subghz_radio_broker_get_status_v2(runtime->radio_broker", runtime)

        for transition in (
            "subghz_radio_broker_set_state_locked",
            '"acquire_timeout"',
            '"invalid_lease"',
            '"invalid_release"',
            '"external_power_failed"',
        ):
            self.assertIn(transition, broker)

        self.assertIn("radio_status_v2", runtime)
        for payload_key in (
            "state=%s",
            "acquired_tick=%lu",
            "held_ticks=%lu",
            "last_transition_tick=%lu",
            "last_error=%s",
        ):
            self.assertIn(payload_key, runtime)

        self.assertIn("lifecycle state", bridge_docs)
        self.assertIn("observability contract", broker_docs)

    def test_direct_radio_control_paths_are_brokered_or_allowlisted(self) -> None:
        direct_control = re.compile(
            r"furi_hal_power_(?:enable|disable)_otg\(|"
            r"subghz_devices_(?:init|deinit)\("
        )
        allowed_direct_without_broker = {
            "applications/main/subghz/subghz_cli.c",
            "applications_user/arf_subghz_full/subghz_cli.c",
            "applications_user/flipper_companion/helpers/subghz_txrx.c",
            "applications_user/flipper_xremote/xremote.c",
            "applications_user/garage_door_remote/helpers/radio_device_loader.c",
            "applications_user/garage_door_remote/protopirate_app.c",
            "applications_user/garage_door_remote/scenes/protopirate_scene_dual_receiver.c",
            "applications_user/garage_door_remote/scenes/protopirate_scene_shield_receiver.c",
            "applications_user/quac/actions/action_ir.c",
            "applications_user/quac/actions/helpers/subghz_txrx.c",
            "applications_user/rolljam_standalone/helpers/radio_device_loader.c",
            "applications_user/rolljam_standalone/rolljam_app.c",
            "applications_user/rolljam_standalone/scenes/rolljam_scene_dual_receiver.c",
            "applications_user/rolljam_standalone/scenes/rolljam_scene_shield_receiver.c",
        }
        roots = (
            REPO_ROOT / "applications/main/subghz",
            REPO_ROOT / "applications_user",
            REPO_ROOT / "applications/debug/subghz_test",
        )

        unexpected = []
        for root in roots:
            for path in sorted(root.rglob("*")):
                if path.suffix not in {".c", ".h"}:
                    continue
                source = path.read_text(encoding="utf-8")
                if not direct_control.search(source):
                    continue
                relative = path.relative_to(REPO_ROOT).as_posix()
                if "subghz_radio_broker_" in source:
                    continue
                if relative in allowed_direct_without_broker:
                    continue
                unexpected.append(relative)

        self.assertEqual(unexpected, [])


if __name__ == "__main__":
    unittest.main()
