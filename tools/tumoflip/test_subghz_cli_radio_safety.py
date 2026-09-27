#!/usr/bin/env python3
"""Regression coverage for Sub-GHz CLI external-radio startup safety."""

from pathlib import Path
import os
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
CLI_SOURCES = (
    REPO_ROOT / "applications/main/subghz/subghz_cli.c",
    REPO_ROOT / "applications_user/arf_subghz_full/subghz_cli.c",
)


def c_function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening_brace = source.index("{", start)
    depth = 0
    for index in range(opening_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]
    raise AssertionError(f"unterminated C function: {signature}")


class SubGhzCliRadioSafetyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.sources = {
            path: path.read_text(encoding="utf-8") for path in CLI_SOURCES
        }

    def test_otg_power_requests_go_through_power_service(self) -> None:
        compiler = os.environ.get("CC", "cc")
        prelude = r"""
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TAG "SubGhzCliTest"
#define RECORD_POWER "power"
#define SUBGHZ_DEVICE_CC1101_EXT_NAME "CC1101_EXT"
#define SUBGHZ_DEVICE_CC1101_INT_NAME "CC1101_INT"
#define FURI_LOG_E(...) ((void)0)

typedef struct { bool otg_requested; } Power;
typedef struct { const char* name; bool connected; bool begin_ok; } SubGhzDevice;

static Power power_service;
static SubGhzDevice internal_device = {SUBGHZ_DEVICE_CC1101_INT_NAME, true, true};
static SubGhzDevice external_device = {SUBGHZ_DEVICE_CC1101_EXT_NAME, true, true};
static SubGhzDevice* external_driver = &external_device;
static unsigned power_on_calls;
static unsigned power_off_calls;
static unsigned direct_hal_power_calls;
static unsigned direct_hal_off_calls;
static unsigned null_connect_calls;

Power* furi_record_open(const char* name) {
    return strcmp(name, RECORD_POWER) == 0 ? &power_service : NULL;
}
void furi_record_close(const char* name) { (void)name; }
void power_enable_otg(Power* power, bool enable) {
    power->otg_requested = enable;
    if(enable) power_on_calls++;
    else power_off_calls++;
}
bool furi_hal_power_enable_otg(void) {
    direct_hal_power_calls++;
    return true;
}
float furi_hal_power_get_usb_voltage(void) { return 0.0f; }
bool furi_hal_power_check_otg_fault(void) { return false; }
bool furi_hal_power_is_otg_enabled(void) { return false; }
void furi_hal_power_disable_otg(void) { direct_hal_off_calls++; }
const SubGhzDevice* subghz_devices_get_by_name(const char* name) {
    if(strcmp(name, SUBGHZ_DEVICE_CC1101_EXT_NAME) == 0) return external_driver;
    if(strcmp(name, SUBGHZ_DEVICE_CC1101_INT_NAME) == 0) return &internal_device;
    return NULL;
}
bool subghz_devices_is_connect(const SubGhzDevice* device) {
    if(!device) {
        null_connect_calls++;
        return false;
    }
    return device->connected;
}
bool subghz_devices_begin(const SubGhzDevice* device) { return device->begin_ok; }
void subghz_devices_end(const SubGhzDevice* device) { (void)device; }
"""

        for path, source in self.sources.items():
            with self.subTest(source=path.name):
                functions = "\n".join(
                    c_function_body(source, signature)
                    for signature in (
                        "static void subghz_cli_radio_device_power_on(",
                        "static void subghz_cli_radio_device_power_off(",
                        "static const SubGhzDevice* subghz_cli_command_get_device(",
                    )
                )
                main = r"""
int main(void) {
    subghz_cli_radio_device_power_on();
    if(power_on_calls != 1 || direct_hal_power_calls != 0) return 1;
    subghz_cli_radio_device_power_off();
    if(power_off_calls != 1 || direct_hal_off_calls != 0) return 2;

    uint32_t selected_index = 1;
    external_driver = NULL;
    const SubGhzDevice* selected = subghz_cli_command_get_device(&selected_index);
    if(selected != &internal_device || selected_index != 0) return 3;
    if(null_connect_calls != 0) return 4;

    external_driver = &external_device;
    external_device.connected = false;
    selected_index = 1;
    selected = subghz_cli_command_get_device(&selected_index);
    if(selected != &internal_device || selected_index != 0) return 5;
    if(null_connect_calls != 0) return 6;

    external_device.connected = true;
    selected_index = 1;
    selected = subghz_cli_command_get_device(&selected_index);
    if(selected != &external_device || selected_index != 1) return 7;
    return 0;
}
"""
                with tempfile.TemporaryDirectory() as temp_dir:
                    source_path = Path(temp_dir) / "subghz_cli_safety.c"
                    binary_path = Path(temp_dir) / "subghz_cli_safety"
                    source_path.write_text(
                        prelude + functions + main, encoding="utf-8"
                    )
                    build = subprocess.run(
                        [
                            compiler,
                            "-std=c11",
                            "-Wall",
                            "-Wextra",
                            "-Werror",
                            str(source_path),
                            "-o",
                            str(binary_path),
                        ],
                        capture_output=True,
                        text=True,
                        check=False,
                    )
                    self.assertEqual(
                        build.returncode, 0, build.stdout + build.stderr
                    )
                    run = subprocess.run(
                        [str(binary_path)],
                        capture_output=True,
                        text=True,
                        timeout=2,
                        check=False,
                    )
                    self.assertEqual(
                        run.returncode,
                        0,
                        f"{path.name}: {run.stdout}{run.stderr} "
                        f"(harness exit {run.returncode})",
                    )

    def test_missing_driver_is_guarded_before_connect_probe(self) -> None:
        for path, source in self.sources.items():
            with self.subTest(source=path.name):
                get_device = c_function_body(
                    source,
                    "static const SubGhzDevice* subghz_cli_command_get_device(",
                )
                self.assertIn("if(device == NULL)", get_device)
                missing_guard = get_device.index("if(device == NULL)")
                connect_probe = get_device.index(
                    "subghz_devices_is_connect(device)"
                )
                self.assertLess(missing_guard, connect_probe)
                self.assertIn("SUBGHZ_DEVICE_CC1101_INT_NAME", get_device[missing_guard:])
                self.assertIn("*device_ind = 0", get_device[missing_guard:])

    def test_start_failure_is_cleaned_up_in_tx_rx_and_file_tx(self) -> None:
        for path, source in self.sources.items():
            with self.subTest(source=path.name):
                self.assertIn(
                    "static bool subghz_cli_command_device_begin(", source
                )
                begin = c_function_body(
                    source,
                    "static bool subghz_cli_command_device_begin(",
                )
                self.assertIn("subghz_devices_begin(device)", begin)
                self.assertIn("subghz_devices_end(device)", begin)

                for signature in (
                    "void subghz_cli_command_tx(",
                    "void subghz_cli_command_rx(",
                    "void subghz_cli_command_tx_from_file(",
                ):
                    body = c_function_body(source, signature)
                    self.assertIn("subghz_cli_command_device_begin(device", body)
                    begin_at = body.index("subghz_cli_command_device_begin(device")
                    reset_at = body.find("subghz_devices_reset(device)")
                    if reset_at >= 0:
                        self.assertLess(begin_at, reset_at)

                for signature in (
                    "void subghz_cli_command_tx(",
                    "void subghz_cli_command_rx(",
                ):
                    body = c_function_body(source, signature)
                    failure_at = body.index(
                        "if(!subghz_cli_command_device_begin(device"
                    )
                    cleanup = body[failure_at : body.index("\n    }", failure_at) + 6]
                    self.assertIn("subghz_devices_deinit()", cleanup)
                    self.assertIn("subghz_cli_radio_device_power_off()", cleanup)
                    self.assertIn("return;", cleanup)


if __name__ == "__main__":
    unittest.main()
