"""Native regressions for the actual Remote scene and checked signal saving.

The filesystem and radio are in-memory mocks. No device or user file is touched.
"""
from pathlib import Path
import json
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/main/subghz_remote"


def function(text, name):
    match = re.search(r"(?m)^(?:static )?[A-Za-z_]\w*\s+" + re.escape(name) + r"\(", text)
    if not match:
        raise AssertionError(f"No definition: {name}")
    start = match.start()
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def compile_run(source, signal_implementation=None):
    with tempfile.TemporaryDirectory(prefix="subremote-native-") as directory:
        work = Path(directory)
        if signal_implementation is not None:
            (work / "signal_file.c").write_text(signal_implementation)
            source = source.replace("/* SIGNAL_IMPLEMENTATION */", '#include "signal_file.c"')
        (work / "test.c").write_text(source)
        result = subprocess.run([
            "cc", "-std=c11", "-O1", "-g", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", str(work / "test.c"), "-o", str(work / "test"),
            *(["-fprofile-instr-generate", "-fcoverage-mapping"]
              if os.environ.get("TUMO_NATIVE_COVERAGE") == "1" else []),
        ], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        environment = dict(os.environ, LLVM_PROFILE_FILE=str(work / "test.profraw"))
        result = subprocess.run([str(work / "test")], capture_output=True, text=True, env=environment)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        if os.environ.get("TUMO_NATIVE_COVERAGE") == "1":
            subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", str(work / "test.profraw"),
                            "-o", str(work / "test.profdata")], check=True, capture_output=True)
            report = json.loads(subprocess.check_output([
                "xcrun", "llvm-cov", "export", str(work / "test"),
                "-instr-profile=" + str(work / "test.profdata"),
            ], text=True))
            measured = False
            for file in report["data"][0]["files"]:
                if Path(file["filename"]) == work / "signal_file.c":
                    measured = True
                    summary = file["summary"]
                    print("signal_file native coverage:",
                          {key: round(summary[key]["percent"], 2) for key in ("lines", "regions")})
                    if min(summary[key]["percent"] for key in ("lines", "regions")) < 80:
                        raise AssertionError("Signal update coverage below 80%")
            if signal_implementation is not None and not measured:
                raise AssertionError("No coverage data for actual signal helper")


class SubRemoteLifecycleTests(unittest.TestCase):
    def test_signal_helper_is_linked_into_faps_not_exported_from_core(self):
        api = (ROOT / "targets/f7/api_symbols.csv").read_text()
        self.assertIn("Function,-,signal_file_update,", api)
        self.assertNotIn("Function,+,signal_file_update,", api)
        for app in (APP, ROOT / "applications_user/quac"):
            self.assertIn('fap_libs=["signal_file"]', (app / "application.fam").read_text())

    def test_owner_stops_before_freeing_callback_dependencies(self):
        owner = function((APP / "subghz_remote_app.c").read_text(), "subghz_remote_app_free")
        self.assertLess(owner.index("subrem_tx_stop_sub"), owner.index("furi_record_close(RECORD_DIALOGS)"))
        self.assertLess(owner.index("subghz_txrx_free"), owner.index("subrem_sub_file_preset_free"))

    def test_actual_radio_deserializes_once_and_never_transmits_during_cleanup(self):
        radio = (APP / "helpers/txrx/subghz_txrx.c").read_text()
        code = function(radio, "subghz_txrx_tx_start") + "\n" + function(radio, "subghz_txrx_tx_stop")
        source = (ROOT / "tools/tumoflip/subremote_radio_host_fixture.h").read_text()
        compile_run(source.replace("/* RADIO */", code))

    def test_actual_scene_finishes_frames_and_handles_raw_eof(self):
        scene = (APP / "scenes/subrem_scene_remote.c").read_text()
        scene = "\n".join(line for line in scene.splitlines() if not line.startswith("#include"))
        enums = (APP / "helpers/subrem_custom_event.h").read_text().replace("#pragma once", "")
        source = (ROOT / "tools/tumoflip/subremote_scene_host_fixture.h").read_text()
        source = source.replace("/* EVENTS */", enums)
        source = source.replace("/* STOP */", function(
            (APP / "subghz_remote_app_i.c").read_text(), "subrem_tx_stop_sub"))
        source = source.replace("/* SCENE */", scene)
        compile_run(source)

    def test_actual_save_keeps_original_or_recoverable_backup_on_io_failure(self):
        helper = ROOT / "lib/signal_file/signal_file.c"
        source = (ROOT / "tools/tumoflip/subremote_save_host_fixture.h").read_text()
        if helper.exists():
            code = "\n".join("" if line.startswith("#include") else line
                             for line in helper.read_text().splitlines())
            compile_run(source.replace("/* SAVE */", "/* SIGNAL_IMPLEMENTATION */"), code)
            return
        else:
            # Exercise the old production wrapper too, rather than failing on
            # the absence of a new helper. Its unchecked write loses the file.
            code = function((APP / "subghz_remote_app_i.c").read_text(),
                            "subrem_save_protocol_to_file")
            source = source.replace("/* SAVE */", code + "\n" + r'''
static bool signal_file_update(Storage* storage, Stream* stream, const char* path) {
    (void)storage; FlipperFormat ff={.stream=stream};
    return subrem_save_protocol_to_file(&ff,path);
}
''')
        compile_run(source)


if __name__ == "__main__":
    unittest.main()
