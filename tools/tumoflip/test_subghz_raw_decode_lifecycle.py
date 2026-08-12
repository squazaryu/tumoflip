#!/usr/bin/env python3
"""Regression contracts for RAW decode lifecycle and duplicate filtering."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


def source(relative: str) -> str:
    return (REPO_ROOT / relative).read_text(encoding="utf-8")


def region(contents: str, start: str, end: str) -> str:
    start_index = contents.index(start)
    end_index = contents.index(end, start_index)
    return contents[start_index:end_index]


CORE_DECODE = "applications/main/subghz/scenes/subghz_scene_decode_raw.c"
ARF_DECODE = "applications_user/arf_subghz_full/scenes/subghz_scene_decode_raw.c"
CORE_RECEIVER = "applications/main/subghz/scenes/subghz_scene_receiver.c"
ARF_RECEIVER = "applications_user/arf_subghz_full/scenes/subghz_scene_receiver.c"
CORE_HISTORY = "applications/main/subghz/subghz_history.c"
ARF_HISTORY = "applications_user/arf_subghz_full/subghz_history.c"
RAW_ENCODERS = (
    "lib/subghz/protocols/raw.c",
    "applications_user/garage_door_remote/protocols/raw.c",
)


class SubGhzRawDecodeLifecycleTest(unittest.TestCase):
    def test_hash_mixes_order_and_breaks_xor_collisions(self) -> None:
        compiler = os.environ.get("CC", "cc")
        with tempfile.TemporaryDirectory() as temp_dir:
            binary = Path(temp_dir) / "subghz_hash_host_test"
            result = subprocess.run(
                [
                    compiler,
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(REPO_ROOT),
                    str(REPO_ROOT / "lib/subghz/blocks/decoder.c"),
                    str(REPO_ROOT / "tools/tumoflip/subghz_hash_host_test.c"),
                    "-o",
                    str(binary),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            result = subprocess.run(
                [str(binary)], capture_output=True, text=True, check=False
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_core_and_arf_raw_decode_are_explicitly_radio_free(self) -> None:
        for relative in (CORE_DECODE, ARF_DECODE):
            with self.subTest(path=relative):
                contents = source(relative)
                start = region(
                    contents,
                    "bool subghz_scene_decode_raw_start(",
                    "bool subghz_scene_decode_raw_next(",
                )
                worker_call = region(
                    start,
                    "subghz_file_encoder_worker_start(",
                    ")) {",
                )
                self.assertIn("NULL", worker_call)
                self.assertNotIn("subghz_txrx_radio_device_get_name", worker_call)
                for radio_operation in (
                    "subghz_txrx_begin(",
                    "subghz_txrx_rx_start(",
                    "subghz_txrx_tx_start(",
                    "subghz_radio_broker_acquire(",
                    "subghz_devices_start_async_tx(",
                ):
                    self.assertNotIn(radio_operation, contents)

        worker = source("lib/subghz/subghz_file_encoder_worker.c")
        self.assertIn("instance->is_decoding = (radio_device_name == NULL);", worker)
        self.assertIn("if(!instance->is_decoding && !instance->device)", worker)
        self.assertLess(
            worker.index("if(!instance->is_decoding && !instance->device)"),
            worker.index("instance->worker_running = true;"),
        )
        invalid_device_guard = region(
            worker,
            "if(!instance->is_decoding && !instance->device)",
            "instance->worker_running = true;",
        )
        self.assertIn("return false;", invalid_device_guard)
        self.assertIn(
            "instance->is_decoding ? SUBGHZ_FILE_ENCODER_DURATION_MAX : 100", worker
        )
        self.assertIn("while(!instance->is_decoding && instance->device", worker)

        # Normal RAW transmission still supplies a concrete radio name.
        raw_tx = source("lib/subghz/protocols/raw.c")
        self.assertIn("furi_string_get_cstr(instance->radio_device_name)", raw_tx)

    def test_zero_hash_and_resume_timeout_are_handled_in_both_histories(self) -> None:
        for relative in (CORE_HISTORY, ARF_HISTORY):
            with self.subTest(path=relative):
                history = source(relative)
                self.assertIn("bool code_last_hash_data_set;", history)
                self.assertIn("instance->code_last_hash_data_set = false;", history)
                self.assertIn(
                    "instance->last_update_timestamp = furi_get_tick();", history
                )
                self.assertIn("instance->code_last_hash_data_set &&", history)
                self.assertIn(
                    "instance->code_last_hash_data == code_hash_data", history
                )
                self.assertIn("instance->code_last_hash_data_set = true;", history)
                self.assertIn("void subghz_history_restart_duplicate_timeout(", history)

        for relative in (CORE_RECEIVER, ARF_RECEIVER):
            receiver = source(relative)
            resume = region(
                receiver,
                "void subghz_scene_receiver_on_enter(",
                "bool subghz_scene_receiver_on_event(",
            )
            self.assertLess(
                resume.index("subghz_history_restart_duplicate_timeout(history)"),
                resume.index("subghz_txrx_rx_start(subghz->txrx)"),
            )

    def test_async_rx_resets_decoders_before_start(self) -> None:
        core_txrx = source("applications/main/subghz/helpers/subghz_txrx.c")
        core_start = region(
            core_txrx,
            "void subghz_txrx_rx_start(",
            "void subghz_txrx_set_need_save_callback(",
        )
        self.assertLess(
            core_start.index("subghz_txrx_stop(instance)"),
            core_start.index("subghz_txrx_receiver_reset(instance)"),
        )
        self.assertLess(
            core_start.index("subghz_txrx_receiver_reset(instance)"),
            core_start.index("subghz_txrx_rx(instance"),
        )

        reset_all = region(
            core_txrx,
            "void subghz_txrx_receiver_reset(",
            "SubGhzReceiver* subghz_txrx_get_receiver(",
        )
        self.assertIn("subghz_receiver_reset(instance->receiver);", reset_all)
        self.assertIn("subghz_receiver_reset(instance->diversity_receiver);", reset_all)

        arf_txrx = source("applications_user/arf_subghz_full/helpers/subghz_txrx.c")
        arf_start = region(
            arf_txrx,
            "void subghz_txrx_rx_start(",
            "void subghz_txrx_set_need_save_callback(",
        )
        self.assertLess(
            arf_start.index("subghz_txrx_stop(instance)"),
            arf_start.index("subghz_txrx_receiver_reset(instance)"),
        )
        self.assertLess(
            arf_start.index("subghz_txrx_receiver_reset(instance)"),
            arf_start.index("subghz_txrx_rx(instance"),
        )
        arf_reset = region(
            arf_txrx,
            "void subghz_txrx_receiver_reset(",
            "SubGhzReceiver* subghz_txrx_get_receiver(",
        )
        self.assertIn("subghz_receiver_reset(instance->receiver);", arf_reset)

    def test_decode_workers_are_freed_once_and_cleared(self) -> None:
        pairs = (
            (
                CORE_DECODE,
                "applications/main/subghz/scenes/subghz_scene_save_success.c",
            ),
            (
                ARF_DECODE,
                "applications_user/arf_subghz_full/scenes/subghz_scene_save_success.c",
            ),
        )
        for decode_path, save_path in pairs:
            with self.subTest(path=decode_path):
                for contents in (source(decode_path), source(save_path)):
                    self.assertIn(
                        "if(subghz->decode_raw_file_worker_encoder != NULL)", contents
                    )
                    self.assertIn(
                        "subghz_file_encoder_worker_free("
                        "subghz->decode_raw_file_worker_encoder)",
                        contents,
                    )
                    self.assertIn(
                        "subghz->decode_raw_file_worker_encoder = NULL;", contents
                    )

    def test_failed_raw_encoder_start_frees_unstarted_worker_for_retry(self) -> None:
        expected_cleanup = """if(instance->file_worker_encoder) {
        if(subghz_file_encoder_worker_is_running(instance->file_worker_encoder)) {
            subghz_file_encoder_worker_stop(instance->file_worker_encoder);
        }
        subghz_file_encoder_worker_free(instance->file_worker_encoder);
        instance->file_worker_encoder = NULL;
    }"""

        for relative in RAW_ENCODERS:
            with self.subTest(path=relative):
                raw = source(relative)
                stop = region(
                    raw,
                    "void subghz_protocol_encoder_raw_stop(",
                    "void subghz_protocol_encoder_raw_free(",
                )
                self.assertIn(expected_cleanup, stop)

                worker_init = region(
                    raw,
                    "static bool subghz_protocol_encoder_raw_worker_init(",
                    "void subghz_protocol_raw_gen_fff_data(",
                )
                self.assertIn(
                    "furi_check(!instance->file_worker_encoder);", worker_init
                )
                self.assertIn(
                    "subghz_protocol_encoder_raw_stop(instance);", worker_init
                )

    def test_keeloq_reset_drops_frame_state_without_touching_custom_buttons(
        self,
    ) -> None:
        keeloq = source("lib/subghz/protocols/keeloq.c")
        reset = region(
            keeloq,
            "void subghz_protocol_decoder_keeloq_reset(",
            "void subghz_protocol_decoder_keeloq_feed(",
        )
        self.assertIn("instance->generic.data = 0;", reset)
        self.assertIn("instance->header_count = 0;", reset)
        self.assertNotIn("subghz_custom_btn", reset)
        self.assertNotIn("instance->generic.btn", reset)

        # Tumoflip's Pecinin button extraction and custom-button routing stay intact.
        self.assertIn("subghz_protocol_keeloq_check_decrypt_pecinin(", keeloq)
        self.assertIn("instance->btn = decrypt >> 28;", keeloq)
        self.assertIn('if(strcmp(*manufacture_name, "Pecinin") != 0)', keeloq)
        self.assertIn("subghz_custom_btn_set_original(instance->btn);", keeloq)

    def test_arf_adaptive_hopping_contract_remains_present(self) -> None:
        receiver = source(ARF_RECEIVER)
        for required in (
            "enable_hopping",
            "enable_combined_hopping",
            "enable_preset_hopping",
            "subghz_txrx_hopper_set_state",
            "subghz_txrx_preset_hopper_set_state",
        ):
            self.assertIn(required, receiver)


if __name__ == "__main__":
    unittest.main()
