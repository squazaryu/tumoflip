#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def source(relative: str) -> str:
    return (REPO_ROOT / relative).read_text(encoding="utf-8")


class TimeLocationConsumersTest(unittest.TestCase):
    def test_shared_client_is_correlated_bounded_and_fail_closed(self) -> None:
        client = source("lib/tumoflip_device_services/tumoflip_device_services.c")
        header = source("lib/tumoflip_device_services/tumoflip_device_services.h")
        target = source("targets/f7/target.json")

        for required in (
            "TUMOFLIP_DEVICE_SERVICES_RESPONSE_MAX    512U",
            "TUMOFLIP_DEVICE_SERVICES_TIMEOUT_MS      12000U",
            "event->request_id != client->pending_request_id",
            "strcmp(event->command, client->pending_command) != 0",
            "event->chunk_index != client->response_next_chunk",
            "pending_request_id != expected_request_id",
            '"invalid_response"',
            '"time_once"',
            '"gps_once"',
        ):
            self.assertIn(required, client)
        self.assertIn("TumoflipDeviceServicesResultCancelled", header)
        self.assertIn('"tumoflip_device_services"', target)

    def test_time_sync_is_explicit_and_totp_validation_is_non_mutating(self) -> None:
        clock_manifest = source(
            "applications/settings/clock_settings/application.fam"
        )
        clock = source(
            "applications/settings/clock_settings/views/clock_settings_module.c"
        )
        totp = source("applications_user/totp/totp_app.c")
        totp_ui = source(
            "applications_user/totp/ui/scenes/generate_token/"
            "totp_scene_generate_token.c"
        )

        self.assertIn(
            'fap_libs=["assets", "tumoflip_device_services"]',
            clock_manifest,
        )
        self.assertIn("event->type == InputTypeLong", clock)
        self.assertIn("event->key == InputKeyOk", clock)
        self.assertIn('request_time(instance->device_services, "clock")', clock)
        self.assertIn("tumoflip_device_services_apply_time", clock)
        self.assertIn('canvas_draw_str(canvas, 0, ROW_0_Y + 14, "Time")', clock)
        for status in ('"Syncing..."', '"Time synced"', '"Sync failed"'):
            self.assertIn(status, clock)

        self.assertIn('request_time(plugin_state->device_services, "totp")', totp)
        self.assertIn("clock_delta <= 10U", totp)
        self.assertIn("offset_delta <= 60", totp)
        self.assertNotIn("tumoflip_device_services_apply_time", totp)
        self.assertIn('"TIME!"', totp_ui)
        self.assertNotIn('"SYNC TIME"', totp_ui)

    def test_clock_long_ok_is_consumed_and_sync_status_uses_a_footer(self) -> None:
        clock = source(
            "applications/settings/clock_settings/views/clock_settings_module.c"
        )

        self.assertIn("bool suppress_ok_short;", clock)
        self.assertIn("model->suppress_ok_short = true;", clock)
        self.assertIn(
            "event->key == InputKeyOk && model->suppress_ok_short", clock
        )
        self.assertIn(
            "event->key == InputKeyOk && event->type == InputTypeShort", clock
        )
        self.assertIn(
            "event->key == InputKeyOk && event->type == InputTypeRepeat", clock
        )
        self.assertIn('return "Hold to sync";', clock)
        self.assertIn('return "Done";', clock)
        self.assertIn("elements_button_center", clock)
        self.assertIn("#define ROW_2_Y (39)", clock)
        self.assertIn("#define ROW_2_H (12)", clock)
        self.assertNotIn(
            "canvas_draw_str(canvas, 0, ROW_0_Y + 7, status);", clock
        )

    def test_sidecars_are_transactional_validated_and_do_not_replace_captures(self) -> None:
        common = source("lib/tumoflip_device_services/tumoflip_device_services.c")
        for required in (
            '"%s.tumoflip.json"',
            '"%s.part"',
            '"%s.bak"',
            "storage_file_sync(file)",
            "memcmp(readback, contents, size) == 0",
            "if(installed_new) storage_common_remove(storage, path)",
            "if(moved_destination) storage_common_rename(storage, backup, path)",
            "storage_file_exists(storage, source_path)",
            "tumoflip_device_services_number_in_range(location->latitude",
            "location->unix_time < 946684800U",
        ):
            self.assertIn(required, common)

        consumers = (
            ("applications/main/subghz/subghz.c", "subghz"),
            (
                "applications/main/nfc/plugins/location_sidecar/"
                "nfc_location_sidecar_plugin.c",
                "nfc",
            ),
            ("applications/main/lfrfid/lfrfid.c", "rfid"),
        )
        for relative, kind in consumers:
            app = source(relative)
            self.assertIn("tumoflip_device_services_client_cancel", app)
            self.assertIn("tumoflip_device_services_client_request_location", app)
            self.assertIn(f'"{kind}"', app)
            self.assertIn("tumoflip_device_services_write_sidecar", app)

    def test_field_consumers_preserve_primary_work_when_phone_is_unavailable(self) -> None:
        field_logger = source("applications_user/field_logger/field_logger.c")
        survey = source("applications_user/wifi_mapper/wifi_mapper.c")
        spectrum = source("applications_user/signal_workbench/signal_workbench.c")

        for required in (
            '"Phone GPS unavailable"',
            '\\"gps_accuracy_m\\":%s',
            "<trkpt lat=",
            'request_location(app->device_services, "journal")',
        ):
            self.assertIn(required, field_logger)
        self.assertIn('"wifi_survey"', survey)
        self.assertIn("gps_unix_time", survey)
        self.assertIn("storage_common_rename(app->storage, app->log_temp_path, app->log_path)", survey)
        self.assertIn("tumoflip_device_services_write_sidecar", survey)
        for required in (
            '"signal_report"',
            '"signal_set"',
            'request_location(app->device_services, "service")',
            "if(valid && report_path && report_path[0])",
        ):
            self.assertIn(required, spectrum)

    def test_runtime_and_wire_contract_advertise_time(self) -> None:
        runtime = source("applications/services/tumoflip_runtime/tumoflip_runtime.c")
        docs = source("docs/app-bridge-v2.md")
        for capability in ("fabric=1", "time=1", "gps=1", "net=1"):
            self.assertIn(capability, runtime)
        self.assertIn("`time_once`", docs)
        self.assertIn("holding `OK` on the Time row", docs)


if __name__ == "__main__":
    unittest.main()
