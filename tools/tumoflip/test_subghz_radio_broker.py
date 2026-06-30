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
            "SubGhzRadioBrokerStateProbing",
            "SubGhzRadioBrokerStateInitialized",
            "SubGhzRadioBrokerStateRx",
            "SubGhzRadioBrokerStateTx",
            "SubGhzRadioBrokerStateAsyncRx",
            "SubGhzRadioBrokerStateAsyncTx",
            "SubGhzRadioBrokerStateCleaningUp",
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
        self.assertIn("subghz_radio_broker_set_state", header)
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

        self.assertIn("SubGhzRadioBrokerStatusV2 status;", runtime)
        for payload_key in (
            "state=%s",
            "acquired_tick=%lu",
            "held_ticks=%lu",
            "last_transition_tick=%lu",
            "last_error=%s",
        ):
            self.assertIn(payload_key, runtime)
        for state_name in (
            '"probing"',
            '"initialized"',
            '"rx"',
            '"tx"',
            '"async_rx"',
            '"async_tx"',
            '"cleaning_up"',
        ):
            self.assertIn(state_name, runtime)

        self.assertIn("lifecycle state", bridge_docs)
        self.assertIn("async_rx", bridge_docs)
        self.assertIn("async_tx", bridge_docs)
        self.assertIn("observability contract", broker_docs)
        self.assertIn("explicit lifecycle transitions", broker_docs)

    def test_direct_radio_control_paths_are_brokered_or_allowlisted(self) -> None:
        direct_control = re.compile(
            r"furi_hal_power_(?:enable|disable)_otg\(|"
            r"subghz_devices_(?:init|deinit)\("
        )
        allowed_direct_without_broker = {
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

    def test_core_subghz_cli_radio_commands_use_broker(self) -> None:
        cli = (REPO_ROOT / "applications/main/subghz/subghz_cli.c").read_text(
            encoding="utf-8"
        )
        manifest = (REPO_ROOT / "applications/main/subghz/application.fam").read_text(
            encoding="utf-8"
        )

        self.assertIn('appid="cli_subghz"', manifest)
        self.assertIn('requires=["cli", "subghz_radio_broker"]', manifest)

        for required in (
            "subghz_cli_command_needs_radio",
            "SUBGHZ_CLI_RADIO_ACQUIRE_TIMEOUT",
            "subghz_radio_broker_acquire(",
            '"subghz_cli"',
            "subghz_radio_broker_release(radio_broker, &radio_lease)",
            'printf("Sub-GHz radio is busy\\r\\n");',
        ):
            self.assertIn(required, cli)

        for command in ("chat", "tx", "rx", "rx_raw", "tx_from_file"):
            self.assertIn(f'furi_string_cmp_str(cmd, "{command}") == 0', cli)

    def test_brokered_txrx_reports_v2_state_transitions(self) -> None:
        for relative in (
            "applications/main/subghz/helpers/subghz_txrx.c",
            "applications_user/arf_subghz_full/helpers/subghz_txrx.c",
        ):
            source = (REPO_ROOT / relative).read_text(encoding="utf-8")
            self.assertIn("subghz_txrx_radio_state", source)
            for state in (
                "SubGhzRadioBrokerStateProbing",
                "SubGhzRadioBrokerStateInitialized",
                "SubGhzRadioBrokerStateRx",
                "SubGhzRadioBrokerStateTx",
                "SubGhzRadioBrokerStateAsyncRx",
                "SubGhzRadioBrokerStateAsyncTx",
                "SubGhzRadioBrokerStateCleaningUp",
            ):
                self.assertIn(state, source)


if __name__ == "__main__":
    unittest.main()
