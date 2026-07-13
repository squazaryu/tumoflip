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
        self.assertIn("InputKeyRight && event->type == InputTypeShort", view)
        self.assertIn("case SubGhzCustomEventViewReceiverToggleDiversity", scene)
        self.assertIn("subghz_txrx_set_preset_internal", scene)
        self.assertIn("subghz_txrx_rx_start", scene)

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
        self.assertIn('"%.2d:%.2d %s%.0f"', history)
        self.assertIn('"D:%s"', receiver_scene)
        self.assertIn("subghz_view_receiver_update_item_time", receiver_scene)
        self.assertIn("subghz_txrx_get_diversity_decoder", receiver_scene)


if __name__ == "__main__":
    unittest.main()
