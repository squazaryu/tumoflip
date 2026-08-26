#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class TumoflipRuntimeTest(unittest.TestCase):
    def test_runtime_command_contract_snapshot(self) -> None:
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")

        bridge_handler = runtime.split(
            "static void\n    tumoflip_runtime_handle_request(", 1
        )[1].split("\nint32_t tumoflip_runtime_srv", 1)[0]
        commands = set(
            re.findall(r'strcmp\(command, "([^"]+)"\) == 0', bridge_handler)
        )
        self.assertEqual(
            commands,
            {
                "ping",
                "capabilities",
                "status",
                "trace",
                "twin",
                "transfer_begin",
                "transfer_progress",
                "transfer_end",
                "hello",
                "fabric_caps",
                "fabric_open",
                "fabric_state",
                "fabric_step",
                "fabric_cancel",
                "diagnostics",
                "diagnostics_export",
                "radio_caps",
                "radio_sessions",
                "radio_sessions_export",
                "radio_protocols",
                "journal",
            },
        )
        self.assertIn("diag=1;rc=1;rs=2;rp=1;", runtime)
        self.assertIn("trace=0;", runtime)
        self.assertIn("trace=1;", runtime)
        self.assertIn('"feat=pkg,radio,twin,transfer,fabric;"', runtime)
        self.assertIn("feat=pkg,radio,trace,twin,transfer,fabric", runtime)

    def test_runtime_status_schema_is_documented_and_bounded(self) -> None:
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")
        bridge_docs = (REPO_ROOT / "docs/app-bridge-v2.md").read_text(encoding="utf-8")
        checklist = (REPO_ROOT / "docs/hardware-regression-checklist.md").read_text(
            encoding="utf-8"
        )

        for macro in (
            "TUMOFLIP_RUNTIME_STATUS_MAX",
            "TUMOFLIP_RUNTIME_TRACE_MAX",
            "TUMOFLIP_RUNTIME_TWIN_MAX",
        ):
            self.assertRegex(runtime, rf"#define\s+{macro}\s+160U")
        self.assertRegex(runtime, r"#define\s+TUMOFLIP_RUNTIME_TRACE_DEPTH\s+8U")
        self.assertIn("session=3", runtime)
        self.assertIn("trace=0;", runtime)
        self.assertIn("trace=1", runtime)
        self.assertIn("twin=1", runtime)
        self.assertIn("diag=1;rc=1;rs=2;rp=1;", runtime)
        self.assertIn("feat=pkg,radio,trace,twin,transfer,fabric", runtime)
        self.assertIn('"feat=pkg,radio,twin,transfer,fabric;"', runtime)
        self.assertNotIn('"radio_status"', runtime)
        self.assertNotIn("tumoflip_runtime_radio_state_name", runtime)
        self.assertIn('strcmp(command, "status") == 0', runtime)
        self.assertIn(
            'tumoflip_runtime_reply(runtime, event->request_id, "status", payload, false)',
            runtime,
        )
        self.assertIn('strcmp(command, "trace") == 0', runtime)
        self.assertIn("tumoflip_runtime_make_trace_payload(runtime, payload, sizeof(payload))", runtime)
        self.assertIn('strcmp(command, "twin") == 0', runtime)
        self.assertIn("tumoflip_runtime_make_twin_payload(runtime, payload, sizeof(payload))", runtime)

        for required in (
            "version_get_version(version)",
            "version_get_githash(version)",
            "version_get_dirty_flag(version)",
            "version_get_firmware_origin(version)",
            "furi_hal_info_get_api_version(&api_major, &api_minor)",
            "version_get_target(version)",
            "storage_sd_status(runtime->storage)",
            "subghz_radio_broker_get_status_v2(runtime->radio_broker, &radio_status)",
            "runtime->transfer_active",
            'strcmp(command, "transfer_begin") == 0',
            'strcmp(command, "transfer_progress") == 0',
            'strcmp(command, "transfer_end") == 0',
            "BtMessageTypeTransferActivity",
            "TUMOFLIP_RUNTIME_RADIO_SESSIONS_PATH",
            "tumoflip_runtime_radio_sessions_export",
            'strcmp(command, "radio_sessions_export") == 0',
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
            "twin=1",
            "pkg=1",
            "radio=2",
            "sd=1",
            "diag=1",
            "rc=1",
            "rs=2",
            "rp=1",
            "feat=pkg",
            "pkg",
            "radio",
            "trace",
            "twin",
            "transfer",
            "fabric",
        ):
            self.assertIn(required, capabilities)

    def test_compact_profile_disables_only_optional_trace(self) -> None:
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")

        self.assertIn("#define TUMOFLIP_RUNTIME_TRACE TUMOFLIP_ROADMAP_FULL", runtime)
        self.assertIn("schema=1;depth=0;count=0;drop=0", runtime)
        self.assertIn("trace=0;", runtime)
        self.assertIn("fabric=1;diag=0;rc=0;rs=0;rp=0;", runtime)
        self.assertIn('"feat=pkg,radio,twin,transfer,fabric;"', runtime)

    def test_runtime_device_twin_payload_is_live_and_bounded(self) -> None:
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")
        bridge_docs = (REPO_ROOT / "docs/app-bridge-v2.md").read_text(encoding="utf-8")
        checklist = (REPO_ROOT / "docs/hardware-regression-checklist.md").read_text(
            encoding="utf-8"
        )

        for required in (
            "tumoflip_runtime_make_twin_payload",
            "furi_hal_power_get_pct()",
            "furi_hal_power_is_charging()",
            "furi_hal_power_is_otg_enabled()",
            "memmgr_heap_get_max_free_block()",
            "subghz_radio_broker_get_status_v2(runtime->radio_broker, &radio_status)",
            "schema=1;fw=%.8s;cm=%.8s;dy=%hhu;sd=%hhu;pkg=%hhu;bat=%u;chg=%hhu;otg=%hhu;",
            "heap=%lu;rf=%hhu;ro=%.4s;sid=%08lX;bo=%.8s",
            "ro=%.4s;sid=%08lX;bo=%.8s",
            'tumoflip_runtime_reply(runtime, event->request_id, "twin", payload, false)',
        ):
            self.assertIn(required, runtime)

        worst_case_twin = (
            "schema=1;fw=12345678;cm=12345678;dy=1;sd=1;pkg=1;bat=100;"
            "chg=1;otg=1;heap=4294967295;rf=255;ro=1234;sid=FFFFFFFF;bo=12345678"
        )
        self.assertLessEqual(len(worst_case_twin), 160)

        for field in ("`twin` returns `runtime/twin`", "`chg`", "`otg`", "`heap`"):
            self.assertIn(field, bridge_docs)
        self.assertIn("Device Twin", bridge_docs)
        self.assertIn("Runtime `twin`", checklist)

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
            "FuriMutex* trace_mutex",
            "runtime->trace_head",
            "runtime->trace_count",
            "runtime->trace_dropped",
            "tumoflip_runtime_trace_add(runtime, 'r'",
            "tumoflip_runtime_trace_add(runtime, error ? 'e' : 't'",
            "tumoflip_runtime_trace_add(runtime, 's'",
            "TumoflipRuntimeApi",
            "furi_record_create(RECORD_TUMOFLIP_RUNTIME, &runtime->api)",
            "tumoflip_runtime_api_get_trace",
            "furi_mutex_acquire(runtime->trace_mutex, FuriWaitForever)",
            "schema=1;depth=%u;count=%u;drop=%lu",
            '"|%c,%c,%c"',
        ):
            self.assertIn(required, runtime)

        worst_case_trace = (
            "schema=1;depth=8;count=8;drop=4294967295"
            + "|r,s,o" * 8
        )
        self.assertLessEqual(len(worst_case_trace), 160)

        header = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.h"
        ).read_text(encoding="utf-8")
        self.assertIn('#define RECORD_TUMOFLIP_RUNTIME "tumoflip_runtime"', header)
        self.assertIn("bool (*get_trace)", header)
        self.assertIn("`trace` returns `runtime/trace`", bridge_docs)
        self.assertIn("schema=1", bridge_docs)
        self.assertIn("`drop`", bridge_docs)
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
            'requires=["bt", "storage", "subghz_radio_broker", "cli"]',
            'EXT_PATH(".tumoflip/package-state.txt")',
            "storage_sd_status(runtime->storage)",
            "sd=%hhu",
            "pkg=%hhu",
        ):
            self.assertIn(
                required,
                runtime
                if required != 'requires=["bt", "storage", "subghz_radio_broker", "cli"]'
                else app,
            )

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

        self.assertRegex(
            runtime, r"#define\s+TUMOFLIP_RUNTIME_SESSION_OWNER_MAX\s+24U"
        )
        for required in (
            "typedef struct {\n    uint32_t session_id;",
            'strcmp(command, "hello") == 0',
            "chunk",
            "owner",
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

    def test_tumofabric_usb_cli_is_bounded_and_documented(self) -> None:
        runtime = (
            REPO_ROOT / "applications/services/tumoflip_runtime/tumoflip_runtime.c"
        ).read_text(encoding="utf-8")
        bridge_docs = (REPO_ROOT / "docs/app-bridge-v2.md").read_text(
            encoding="utf-8"
        )
        checklist = (REPO_ROOT / "docs/hardware-regression-checklist.md").read_text(
            encoding="utf-8"
        )

        for required in (
            'cli_registry_add_command_ex(',
            '"tumofabric"',
            "CliCommandFlagParallelSafe",
            'strcmp(command, "caps") == 0',
            'strcmp(command, "state") == 0',
            'strcmp(command, "start") == 0',
            'strcmp(command, "step") == 0',
            'strcmp(command, "cancel") == 0',
            'strcmp(command, "trace") == 0',
            "tumoflip_runtime_api_get_fabric_state",
            "tumoflip_runtime_api_open_local_fabric",
            "tumoflip_runtime_api_step_local_fabric",
            "tumoflip_runtime_api_cancel_fabric",
            "FABRIC schema=1;status=ok",
        ):
            self.assertIn(required, runtime)

        cli_section = runtime.split("static void tumoflip_runtime_cli(", 1)[1]
        cli_section = cli_section.split("static void tumoflip_runtime_bridge_callback", 1)[0]
        self.assertNotIn("snapshot.token", cli_section)
        self.assertNotIn("snapshot.session_id", cli_section)
        self.assertNotIn("system(", cli_section)
        self.assertNotIn("popen(", cli_section)

        self.assertIn("### TumoFabric USB operator plane", bridge_docs)
        self.assertIn("`tumofabric caps`", bridge_docs)
        self.assertIn("must not expose the BLE token", bridge_docs)
        self.assertIn("TumoFabric Mac Node", checklist)

    def test_diagnostics_are_available_from_the_on_device_viewer(self) -> None:
        viewer = (
            REPO_ROOT
            / "applications_user/runtime_trace_viewer/runtime_trace_viewer.c"
        ).read_text(encoding="utf-8")
        manifest = (
            REPO_ROOT
            / "applications_user/runtime_trace_viewer/application.fam"
        ).read_text(encoding="utf-8")
        self.assertIn("RuntimeTraceViewerActionDiagnostics", viewer)
        self.assertIn("Hardware Diagnostics", viewer)
        self.assertIn("get_diagnostics", viewer)
        self.assertIn('name="Tumo Diagnostics"', manifest)


if __name__ == "__main__":
    unittest.main()
