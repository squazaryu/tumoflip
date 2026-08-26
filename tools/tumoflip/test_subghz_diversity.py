#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SUBGHZ = REPO_ROOT / "applications/main/subghz"


class SubGhzDiversityTest(unittest.TestCase):
    def test_dual_mode_is_separate_from_the_module_selector(self) -> None:
        types = (SUBGHZ / "helpers/subghz_types.h").read_text(encoding="utf-8")
        settings = (SUBGHZ / "scenes/subghz_scene_radio_settings.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("SubGhzRadioDeviceTypeAuto", types)
        self.assertIn("#define RADIO_DEVICE_COUNT 2", settings)
        self.assertIn("#define RX_MODE_COUNT 2", settings)
        self.assertIn('"RX Mode"', settings)
        self.assertIn('"AUTO"', settings)
        self.assertIn('"DUAL"', settings)
        self.assertNotIn('"Auto Dual"', settings)
        self.assertIn("SubGhzRadioDeviceTypeAuto", settings)
        self.assertIn("subghz_txrx_radio_device_is_external_connected", settings)

    def test_receiver_has_a_direct_auto_dual_toggle(self) -> None:
        events = (SUBGHZ / "helpers/subghz_custom_event.h").read_text(encoding="utf-8")
        view = (SUBGHZ / "views/receiver.c").read_text(encoding="utf-8")
        scene = (SUBGHZ / "scenes/subghz_scene_receiver.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("SubGhzCustomEventViewReceiverToggleDiversity", events)
        self.assertIn("elements_button_right", view)
        self.assertIn('SubGhzRadioDeviceTypeAuto ? "Auto" : "Dual"', view)
        self.assertIn("const bool show_diversity_toggle", view)
        self.assertGreaterEqual(
            view.count("model->device_type != SubGhzRadioDeviceTypeInternal"), 2
        )
        self.assertIn("model->history_item == 0", view)
        self.assertIn("if(!show_diversity_toggle)", view)
        self.assertIn("InputKeyRight && event->type == InputTypeShort", view)
        self.assertIn("case SubGhzCustomEventViewReceiverToggleDiversity", scene)
        self.assertIn("if(current == SubGhzRadioDeviceTypeInternal)", scene)
        self.assertIn("subghz_txrx_set_preset_internal", scene)
        self.assertIn("subghz_txrx_rx_start", scene)

    def test_external_disconnect_is_non_fatal_during_receiver_exit(self) -> None:
        txrx = (SUBGHZ / "helpers/subghz_txrx.c").read_text(encoding="utf-8")
        scene = (SUBGHZ / "scenes/subghz_scene_receiver.c").read_text(
            encoding="utf-8"
        )
        driver = (
            REPO_ROOT
            / "applications/drivers/subghz/cc1101_ext/cc1101_ext.c"
        ).read_text(encoding="utf-8")

        self.assertIn("subghz_device_cc1101_ext_try_idle", driver)
        self.assertIn('FURI_LOG_W(TAG, "CC1101 disconnected while stopping RX")', driver)
        self.assertIn("subghz_scene_receiver_recover_disconnected_external", scene)
        self.assertIn(
            "subghz_txrx_radio_device_fallback_internal(subghz->txrx)",
            scene,
        )
        self.assertNotIn(
            "subghz_devices_stop_async_rx(instance->radio_device);\n"
            "        subghz_devices_idle(instance->radio_device);",
            txrx,
        )

    def test_receiver_reprobes_the_preferred_radio_before_rx(self) -> None:
        internal = (SUBGHZ / "helpers/subghz_txrx_i.h").read_text(encoding="utf-8")
        txrx = (SUBGHZ / "helpers/subghz_txrx.c").read_text(encoding="utf-8")
        scene = (SUBGHZ / "scenes/subghz_scene_receiver.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("preferred_radio_device_type", internal)
        self.assertIn("subghz_txrx_radio_device_reprobe_preferred", txrx)
        self.assertIn("subghz_txrx_radio_device_is_external_connected", txrx)
        self.assertIn("subghz_txrx_radio_device_fallback_internal", txrx)
        on_enter = scene[
            scene.index("void subghz_scene_receiver_on_enter") : scene.index(
                "bool subghz_scene_receiver_on_event"
            )
        ]
        self.assertIn("subghz_txrx_stop(subghz->txrx);", on_enter)
        self.assertIn(
            "subghz_txrx_radio_device_reprobe_preferred(subghz->txrx);", on_enter
        )
        self.assertLess(
            on_enter.index("subghz_txrx_stop(subghz->txrx);"),
            on_enter.index("subghz_txrx_radio_device_reprobe_preferred(subghz->txrx);"),
        )
        self.assertLess(
            on_enter.index("subghz_txrx_radio_device_reprobe_preferred(subghz->txrx);"),
            on_enter.index("subghz_txrx_rx_start(subghz->txrx);"),
        )

    def test_live_external_probe_accepts_cc1101_partnum_zero(self) -> None:
        driver = (
            REPO_ROOT
            / "applications/drivers/subghz/cc1101_ext/cc1101_ext.c"
        ).read_text(encoding="utf-8")
        probe = driver[
            driver.index("bool subghz_device_cc1101_ext_is_connect") : driver.index(
                "void subghz_device_cc1101_ext_sleep"
            )
        ]

        self.assertIn("cc1101_get_partnumber", probe)
        self.assertIn("cc1101_get_version", probe)
        self.assertIn("(partnumber != 0xFF)", probe)
        self.assertIn("(version != 0x00)", probe)
        self.assertIn("(version != 0xFF)", probe)
        self.assertNotIn("(partnumber != 0)", probe)

    def test_failed_live_probe_reinitializes_the_preferred_external_radio(self) -> None:
        txrx = (SUBGHZ / "helpers/subghz_txrx.c").read_text(encoding="utf-8")
        reprobe = txrx[
            txrx.index("SubGhzRadioDeviceType subghz_txrx_radio_device_reprobe_preferred") :
            txrx.index("SubGhzRadioDeviceType subghz_txrx_radio_device_fallback_internal")
        ]

        live_failure = reprobe.index(
            'FURI_LOG_W(TAG, "External live probe failed, retrying cold")'
        )
        teardown = reprobe.index(
            "subghz_txrx_radio_device_apply(instance, SubGhzRadioDeviceTypeInternal);",
            live_failure,
        )
        cold_restore = reprobe.index(
            "return subghz_txrx_radio_device_apply(instance, preferred);"
        )

        self.assertIn(
            "instance->radio_device_type != SubGhzRadioDeviceTypeInternal", reprobe
        )
        self.assertLess(live_failure, teardown)
        self.assertLess(teardown, cold_restore)

    def test_external_hopping_timeout_falls_back_without_blocking(self) -> None:
        txrx = (SUBGHZ / "helpers/subghz_txrx.c").read_text(encoding="utf-8")
        driver = (
            REPO_ROOT
            / "applications/drivers/subghz/cc1101_ext/cc1101_ext.c"
        ).read_text(encoding="utf-8")
        set_frequency = driver[
            driver.index("uint32_t subghz_device_cc1101_ext_set_frequency") : driver.index(
                "static bool subghz_device_cc1101_ext_start_debug"
            )
        ]

        self.assertIn("cc1101_wait_status_state", set_frequency)
        self.assertIn("if(!idle_after_tune)", set_frequency)
        self.assertIn("return 0;", set_frequency)
        self.assertNotIn("while(true)", set_frequency)
        self.assertIn(
            'FURI_LOG_W(TAG, "External radio retune failed, falling back to internal")',
            txrx,
        )
        self.assertIn("subghz_txrx_radio_device_fallback_internal(instance);", txrx)
        self.assertIn("return subghz_txrx_rx(instance, frequency);", txrx)

    def test_dual_receive_uses_independent_workers_and_decoders(self) -> None:
        internal = (SUBGHZ / "helpers/subghz_txrx_i.h").read_text(encoding="utf-8")
        source = (SUBGHZ / "helpers/subghz_txrx.c").read_text(encoding="utf-8")

        for required in (
            "SubGhzWorker* worker;",
            "SubGhzWorker* diversity_worker;",
            "SubGhzReceiver* receiver;",
            "SubGhzReceiver* diversity_receiver;",
            "FuriMutex* rx_callback_mutex;",
        ):
            self.assertIn(required, internal)

        self.assertIn("subghz_txrx_diversity_alloc", source)
        self.assertIn("subghz_txrx_diversity_free", source)
        self.assertIn("SUBGHZ_DIVERSITY_STREAM_CAPACITY 1024U", source)
        self.assertIn("subghz_worker_alloc_with_capacity", source)
        self.assertIn("instance->diversity_receiver_context", source)
        self.assertIn("subghz_worker_rx_callback,\n            instance->diversity_worker", source)
        self.assertIn("SubGhzRadioBrokerDeviceDual", source)
        self.assertNotIn(
            "subghz_worker_set_context(instance->worker, instance->diversity_receiver)",
            source,
        )

        worker_public = (REPO_ROOT / "lib/subghz/subghz_worker.h").read_text(
            encoding="utf-8"
        )
        worker_private = (REPO_ROOT / "lib/subghz/subghz_worker_i.h").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("subghz_worker_alloc_with_capacity", worker_public)
        self.assertIn("subghz_worker_alloc_with_capacity", worker_private)

    def test_preset_frequency_and_lifecycle_cover_both_radios(self) -> None:
        source = (SUBGHZ / "helpers/subghz_txrx.c").read_text(encoding="utf-8")

        for required in (
            "subghz_devices_load_preset(\n            instance->diversity_radio_device",
            "subghz_devices_set_frequency(instance->diversity_radio_device, frequency)",
            "subghz_devices_start_async_rx(\n            instance->diversity_radio_device",
            "subghz_devices_stop_async_rx(instance->diversity_radio_device)",
            "subghz_devices_idle(instance->diversity_radio_device)",
            "subghz_worker_stop(instance->diversity_worker)",
        ):
            self.assertIn(required, source)

        self.assertIn("if(!external_ready)", source)
        self.assertIn("const bool external_connected", source)
        self.assertRegex(
            source,
            r"external_was_active\s*\|\|\s*subghz_txrx_radio_device_is_external_connected",
        )
        self.assertIn("if(external_connected)", source)
        self.assertIn("SubGhzRadioDeviceTypeInternal", source)
        self.assertIn("subghz_txrx_radio_device_power_off(instance)", source)

    def test_duplicate_frames_keep_the_stronger_source(self) -> None:
        txrx = (SUBGHZ / "helpers/subghz_txrx.c").read_text(encoding="utf-8")
        history = (SUBGHZ / "subghz_history.c").read_text(encoding="utf-8")
        receiver_scene = (SUBGHZ / "scenes/subghz_scene_receiver.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("const bool same_frame", txrx)
        self.assertIn("source_rssi > instance->last_rx_rssi", txrx)
        self.assertIn("rssi > item->rssi", history)
        self.assertIn("item->source = source", history)
        self.assertIn("SubGhzHistoryAddResultUpdated", history)
        self.assertIn("air_time_ms - instance->last_update_air_time", history)
        self.assertIn("subghz_txrx_get_air_time_ms(subghz->txrx)", receiver_scene)
        self.assertIn('"%.2d:%.2d %s%.0f"', history)
        self.assertIn('"D:%s"', receiver_scene)
        self.assertIn("subghz_view_receiver_update_item_time", receiver_scene)
        self.assertIn("subghz_txrx_get_diversity_decoder", receiver_scene)


if __name__ == "__main__":
    unittest.main()
