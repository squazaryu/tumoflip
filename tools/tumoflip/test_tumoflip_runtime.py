#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class TumoflipRuntimeTest(unittest.TestCase):
    def test_runtime_status_schema_is_documented_and_bounded(self) -> None:
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")
        bridge_docs = (REPO_ROOT / "docs/app-bridge-v2.md").read_text(encoding="utf-8")
        checklist = (REPO_ROOT / "docs/hardware-regression-checklist.md").read_text(
            encoding="utf-8"
        )

        self.assertIn("#define TUMOFLIP_RUNTIME_STATUS_MAX", runtime)
        self.assertIn("TUMOFLIP_RUNTIME_STATUS_MAX        160U", runtime)
        self.assertIn("session=3", runtime)
        self.assertIn("features=transfer_activity", runtime)
        self.assertNotIn('"radio_status"', runtime)
        self.assertIn('strcmp(runtime->assembly.command, "status") == 0', runtime)
        self.assertIn(
            'tumoflip_runtime_reply(runtime, event->request_id, "status", payload, false)',
            runtime,
        )

        for required in (
            "version_get_version(version)",
            "version_get_githash(version)",
            "version_get_dirty_flag(version)",
            "version_get_firmware_origin(version)",
            "furi_hal_info_get_api_version(&api_major, &api_minor)",
            "version_get_target(version)",
            "runtime->transfer_active",
            "subghz_radio_broker_get_status_v2(runtime->radio_broker, &radio_status)",
        ):
            self.assertIn(required, runtime)

        for field in (
            "schema=2",
            "fw=%.8s",
            "commit=%.8s",
            "dirty=%hhu",
            "origin=%.4s",
            "api=%hu.%hu",
            "target=%hhu",
            "transfer=%hhu",
            "sid=%08lX",
            "bo=%.8s",
            "radio=%.8s",
            "owner=%.4s",
        ):
            self.assertIn(field, runtime)
            self.assertIn(field.split("=")[0], bridge_docs)

        self.assertIn("FAB2 Runtime status", checklist)

    def test_runtime_capabilities_payload_fits_one_fab2_frame(self) -> None:
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")
        match = re.search(
            r"#define TUMOFLIP_RUNTIME_CAPABILITIES\s+\\\n"
            r'\s+"([^"]+)"',
            runtime,
        )

        self.assertIsNotNone(match)
        capabilities = "".join(match.groups())
        self.assertLessEqual(len(capabilities), 160)
        for required in (
            "runtime=1",
            "fab=2",
            "session=3",
            "features=transfer_activity",
            "transfer_activity",
        ):
            self.assertIn(required, capabilities)

    def test_app_bridge_v3_sessions_are_documented_and_guarded(self) -> None:
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")
        bridge_v2_docs = (REPO_ROOT / "docs/app-bridge-v2.md").read_text(
            encoding="utf-8"
        )
        bridge_v3_docs = (REPO_ROOT / "docs/app-bridge-v3.md").read_text(
            encoding="utf-8"
        )
        checklist = (REPO_ROOT / "docs/hardware-regression-checklist.md").read_text(
            encoding="utf-8"
        )

        for required in (
            "#define TUMOFLIP_RUNTIME_SESSION_OWNER_MAX 24U",
            "typedef struct {\n    uint32_t session_id;",
            'strcmp(runtime->assembly.command, "hello") == 0',
            "owner_byte != ';'",
            "owner_byte != '='",
            "invalid_owner",
        ):
            self.assertIn(required, runtime)

        for required in (
            "App Bridge v3",
            "hello",
            "sid=<session id>",
            "bo=<owner>",
        ):
            self.assertIn(required, bridge_v3_docs)

        self.assertIn("docs/app-bridge-v3.md", bridge_v2_docs)
        self.assertIn("Runtime `hello`", checklist)
        self.assertIn("`sid` and `bo`", checklist)


if __name__ == "__main__":
    unittest.main()
