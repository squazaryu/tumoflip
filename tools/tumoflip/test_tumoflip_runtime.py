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
        self.assertIn("#define TUMOFLIP_RUNTIME_TRACE_DEPTH       8U", runtime)
        self.assertIn("#define TUMOFLIP_RUNTIME_TRACE_MAX         160U", runtime)
        self.assertIn("session=3", runtime)
        self.assertIn("trace=1", runtime)
        self.assertIn("features=transfer_activity,pkg_state,radio_v2,trace_ring", runtime)
        self.assertNotIn('"radio_status"', runtime)
        self.assertNotIn("tumoflip_runtime_radio_state_name", runtime)
        self.assertIn('strcmp(runtime->assembly.command, "status") == 0', runtime)
        self.assertIn(
            'tumoflip_runtime_reply(runtime, event->request_id, "status", payload, false)',
            runtime,
        )
        self.assertIn('strcmp(runtime->assembly.command, "trace") == 0', runtime)
        self.assertIn("tumoflip_runtime_make_trace_payload(runtime, payload, sizeof(payload))", runtime)

        for required in (
            "version_get_version(version)",
            "version_get_githash(version)",
            "version_get_dirty_flag(version)",
            "version_get_firmware_origin(version)",
            "furi_hal_info_get_api_version(&api_major, &api_minor)",
            "version_get_target(version)",
            "runtime->transfer_active",
            "storage_sd_status(runtime->storage)",
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
            "sd=%hhu",
            "pkg=%hhu",
            "sid=%08lX",
            "bo=%.8s",
            "radio=%hhu",
            "owner=%.4s",
        ):
            self.assertIn(field, runtime)
            self.assertIn(field.split("=")[0], bridge_docs)

        self.assertIn("FAB2 Runtime status", checklist)

    def test_runtime_capabilities_payload_fits_one_fab2_frame(self) -> None:
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")
        macro = re.search(
            r"#define TUMOFLIP_RUNTIME_CAPABILITIES\s+\\\n"
            r"(?P<body>(?:\s+\"[^\"]+\"(?:\s+\\)?\n?)+)",
            runtime,
            re.MULTILINE,
        )

        self.assertIsNotNone(macro)
        capabilities = "".join(re.findall(r'"([^"]+)"', macro.group("body")))
        self.assertLessEqual(len(capabilities), 160)
        for required in (
            "runtime=1",
            "fab=2",
            "session=3",
            "status=2",
            "trace=1",
            "packages=1",
            "radio=2",
            "sd=1",
            "features=transfer_activity",
            "pkg_state",
            "radio_v2",
            "trace_ring",
            "transfer_activity",
        ):
            self.assertIn(required, capabilities)

    def test_runtime_trace_ring_is_bounded_and_documented(self) -> None:
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")
        bridge_docs = (REPO_ROOT / "docs/app-bridge-v2.md").read_text(encoding="utf-8")
        checklist = (REPO_ROOT / "docs/hardware-regression-checklist.md").read_text(
            encoding="utf-8"
        )

        for required in (
            "TumoflipRuntimeTraceEvent trace[TUMOFLIP_RUNTIME_TRACE_DEPTH]",
            "runtime->trace_head",
            "runtime->trace_count",
            "runtime->trace_dropped",
            "tumoflip_runtime_trace_add(runtime, \"rx\"",
            "tumoflip_runtime_trace_add(runtime, error ? \"er\" : \"tx\"",
            "tumoflip_runtime_trace_add(runtime, \"tr\"",
            "tumoflip_runtime_trace_add(runtime, \"ss\"",
            "schema=1;depth=%u;count=%u;drop=%lu",
            '"|%02X,%s,%04lX,%.6s,%c"',
        ):
            self.assertIn(required, runtime)

        self.assertIn("`trace` returns `runtime/trace`", bridge_docs)
        self.assertIn("schema=1", bridge_docs)
        self.assertIn("Runtime `trace`", checklist)

    def test_package_state_presence_is_read_only_and_documented(self) -> None:
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")
        app = (
            REPO_ROOT / "applications/services/tumoflip_runtime/application.fam"
        ).read_text(encoding="utf-8")
        bridge_docs = (REPO_ROOT / "docs/app-bridge-v2.md").read_text(
            encoding="utf-8"
        )
        package_docs = (REPO_ROOT / "docs/tumoflip-packages.md").read_text(
            encoding="utf-8"
        )

        for required in (
            'requires=["bt", "storage", "subghz_radio_broker"]',
            'EXT_PATH(".tumoflip/package-state.txt")',
            "storage_sd_status(runtime->storage)",
            "sd=%hhu",
            "pkg=%hhu",
        ):
            self.assertIn(required, runtime if required != 'requires=["bt", "storage", "subghz_radio_broker"]' else app)

        for required in (
            "sd=1",
            "sd=0",
            "pkg=1",
            "pkg=0",
            "package-state.txt",
        ):
            self.assertIn(required, bridge_docs)
            self.assertIn(required, package_docs)

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
