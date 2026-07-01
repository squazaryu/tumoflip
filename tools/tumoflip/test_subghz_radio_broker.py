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
            "SubGhzRadioBrokerState state;",
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
            "subghz_radio_broker_note_error_locked",
            "SubGhzRadioBrokerStateError",
        ):
            self.assertIn(transition, broker)

        for payload_key in (
            "radio=%s",
            "owner=%s",
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
            "applications_user/flipper_xremote/xremote.c",
            "applications_user/quac/actions/action_ir.c",
        }
        roots = (
            REPO_ROOT / "applications/main/subghz",
            REPO_ROOT / "applications/main/subghz_remote",
            REPO_ROOT / "applications/system/js_app/modules/js_subghz",
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
            "subghz_cli_radio_acquire",
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

    def test_companion_and_quac_subghz_helpers_use_broker(self) -> None:
        for relative, owner in (
            ("applications_user/flipper_companion/helpers/subghz_txrx.c", "flipper_companion"),
            ("applications_user/quac/actions/helpers/subghz_txrx.c", "quac_subghz"),
        ):
            source = (REPO_ROOT / relative).read_text(encoding="utf-8")
            header = (REPO_ROOT / relative.replace(".c", "_i.h")).read_text(
                encoding="utf-8"
            )

            for required in (
                "SubGhzRadioBroker* radio_broker;",
                "SubGhzRadioBrokerLease radio_lease;",
            ):
                self.assertIn(required, header)

            for required in (
                "furi_record_open(RECORD_SUBGHZ_RADIO_BROKER)",
                "subghz_radio_broker_acquire(",
                f'"{owner}"',
                "subghz_radio_broker_external_power_on(",
                "subghz_radio_broker_external_power_off(",
                "subghz_radio_broker_set_selected_device(",
                "subghz_radio_broker_set_state(",
                "subghz_radio_broker_release(",
                "furi_record_close(RECORD_SUBGHZ_RADIO_BROKER)",
            ):
                self.assertIn(required, source)

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

        for relative in (
            "applications_user/flipper_companion/application.fam",
            "applications_user/quac/application.fam",
        ):
            manifest = (REPO_ROOT / relative).read_text(encoding="utf-8")
            self.assertIn('"subghz_radio_broker"', manifest)

    def test_subghz_remote_uses_broker(self) -> None:
        source = (
            REPO_ROOT
            / "applications/main/subghz_remote/helpers/txrx/subghz_txrx.c"
        ).read_text(encoding="utf-8")
        header = (
            REPO_ROOT
            / "applications/main/subghz_remote/helpers/txrx/subghz_txrx_i.h"
        ).read_text(encoding="utf-8")
        manifest = (
            REPO_ROOT / "applications/main/subghz_remote/application.fam"
        ).read_text(encoding="utf-8")

        for required in (
            "SubGhzRadioBroker* radio_broker;",
            "SubGhzRadioBrokerLease radio_lease;",
        ):
            self.assertIn(required, header)

        for required in (
            "furi_record_open(RECORD_SUBGHZ_RADIO_BROKER)",
            "subghz_radio_broker_acquire(",
            '"subghz_remote"',
            "subghz_radio_broker_external_power_on(",
            "subghz_radio_broker_external_power_off(",
            "subghz_radio_broker_set_selected_device(",
            "subghz_radio_broker_set_state(",
            "subghz_radio_broker_release(",
            "furi_record_close(RECORD_SUBGHZ_RADIO_BROKER)",
        ):
            self.assertIn(required, source)

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

        self.assertIn('"subghz_radio_broker"', manifest)

    def test_standalone_arf_apps_use_brokered_radio_loaders(self) -> None:
        apps = (
            (
                "applications_user/garage_door_remote",
                "protopirate_app",
                "garage_door_remote",
                "protopirate",
            ),
            (
                "applications_user/rolljam_standalone",
                "rolljam_app",
                "rolljam_standalone",
                "rolljam",
            ),
        )
        for app_dir, app_prefix, owner, chain_prefix in apps:
            manifest = (REPO_ROOT / app_dir / "application.fam").read_text(
                encoding="utf-8"
            )
            app_i = (REPO_ROOT / app_dir / f"{app_prefix}_i.h").read_text(
                encoding="utf-8"
            )
            app_c = (REPO_ROOT / app_dir / f"{app_prefix}.c").read_text(
                encoding="utf-8"
            )
            loader_h = (REPO_ROOT / app_dir / "helpers/radio_device_loader.h").read_text(
                encoding="utf-8"
            )
            loader_c = (REPO_ROOT / app_dir / "helpers/radio_device_loader.c").read_text(
                encoding="utf-8"
            )
            rx_chain = (REPO_ROOT / app_dir / f"helpers/{chain_prefix}_rx_chain.c").read_text(
                encoding="utf-8"
            )
            tx_chain = (REPO_ROOT / app_dir / f"helpers/{chain_prefix}_tx_chain.c").read_text(
                encoding="utf-8"
            )
            dual_scene = (
                REPO_ROOT / app_dir / f"scenes/{chain_prefix}_scene_dual_receiver.c"
            ).read_text(encoding="utf-8")
            shield_scene = (
                REPO_ROOT / app_dir / f"scenes/{chain_prefix}_scene_shield_receiver.c"
            ).read_text(encoding="utf-8")
            broker_sources = app_c + loader_c + rx_chain + tx_chain + dual_scene + shield_scene

            self.assertIn('"subghz_radio_broker"', manifest)
            self.assertIn("SubGhzRadioBroker* radio_broker;", app_i)
            self.assertIn("SubGhzRadioBrokerLease radio_lease;", app_i)
            self.assertIn(f'"{owner}"', app_c)
            self.assertIn("subghz_radio_broker_acquire(", app_c)
            self.assertIn("subghz_radio_broker_release(", app_c)
            self.assertIn("SubGhzRadioBrokerDeviceDual", broker_sources)
            for state in (
                "SubGhzRadioBrokerStateProbing",
                "SubGhzRadioBrokerStateInitialized",
                "SubGhzRadioBrokerStateAsyncRx",
                "SubGhzRadioBrokerStateCleaningUp",
                "SubGhzRadioBrokerStateAcquired",
            ):
                self.assertIn(state, broker_sources)

            for signature in (
                "SubGhzRadioBroker* broker",
                "const SubGhzRadioBrokerLease* lease",
            ):
                self.assertIn(signature, loader_h)
                self.assertIn(signature, loader_c)
                self.assertIn(signature, rx_chain)
                self.assertIn(signature, tx_chain)

            self.assertIn("subghz_radio_broker_external_power_on(", loader_c)
            self.assertIn("subghz_radio_broker_external_power_off(", loader_c)

    def test_js_subghz_module_uses_brokered_radio_loader(self) -> None:
        source = (
            REPO_ROOT / "applications/system/js_app/modules/js_subghz/js_subghz.c"
        ).read_text(encoding="utf-8")
        manifest = (REPO_ROOT / "applications/system/js_app/application.fam").read_text(
            encoding="utf-8"
        )
        loader_h = (
            REPO_ROOT / "applications/system/js_app/modules/js_subghz/radio_device_loader.h"
        ).read_text(encoding="utf-8")
        loader_c = (
            REPO_ROOT / "applications/system/js_app/modules/js_subghz/radio_device_loader.c"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="js_subghz"', manifest)
        self.assertIn('requires=["js_app", "subghz_radio_broker"]', manifest)
        self.assertIn("SubGhzRadioBroker* radio_broker;", source)
        self.assertIn("SubGhzRadioBrokerLease radio_lease;", source)
        self.assertIn("memset(js_subghz, 0, sizeof(JsSubghzInst));", source)
        self.assertIn("furi_record_open(RECORD_SUBGHZ_RADIO_BROKER)", source)
        self.assertIn("subghz_radio_broker_acquire(", source)
        self.assertIn('"js_subghz"', source)
        self.assertIn("subghz_radio_broker_release(", source)
        self.assertIn("furi_record_close(RECORD_SUBGHZ_RADIO_BROKER)", source)
        self.assertIn("radio_device_loader_set(", source)
        self.assertIn("radio_device_loader_end(", source)
        self.assertIn("radio_device_loader_is_external(", source)
        self.assertIn("subghz_radio_broker_set_selected_device(", source)
        self.assertIn("subghz_radio_broker_set_state(", source)

        for state in (
            "SubGhzRadioBrokerStateAcquired",
            "SubGhzRadioBrokerStateProbing",
            "SubGhzRadioBrokerStateInitialized",
            "SubGhzRadioBrokerStateRx",
            "SubGhzRadioBrokerStateTx",
            "SubGhzRadioBrokerStateAsyncTx",
            "SubGhzRadioBrokerStateCleaningUp",
        ):
            self.assertIn(state, source)

        for signature in (
            "SubGhzRadioBroker* broker",
            "const SubGhzRadioBrokerLease* lease",
        ):
            self.assertIn(signature, loader_h)
            self.assertIn(signature, loader_c)

        self.assertIn("subghz_radio_broker_external_power_on(", loader_c)
        self.assertIn("subghz_radio_broker_external_power_off(", loader_c)


if __name__ == "__main__":
    unittest.main()
