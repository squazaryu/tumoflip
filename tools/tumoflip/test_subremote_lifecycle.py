"""Native regressions for the actual Remote scene and checked signal saving.

The filesystem and radio are in-memory mocks. No device or user file is touched.
"""
from pathlib import Path
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


def compile_run(source):
    with tempfile.TemporaryDirectory(prefix="subremote-native-") as directory:
        work = Path(directory)
        (work / "test.c").write_text(source)
        result = subprocess.run([
            "cc", "-std=c11", "-O1", "-g", "-fsanitize=address,undefined",
            "-fno-sanitize-recover=all", str(work / "test.c"), "-o", str(work / "test"),
        ], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        result = subprocess.run([str(work / "test")], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)


class SubRemoteLifecycleTests(unittest.TestCase):
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
        helper = APP / "helpers/subrem_safe_save.c"
        source = (ROOT / "tools/tumoflip/subremote_save_host_fixture.h").read_text()
        if helper.exists():
            code = "\n".join(line for line in helper.read_text().splitlines()
                             if not line.startswith("#include"))
            source = source.replace("/* SAVE */", code)
        else:
            # Exercise the old production wrapper too, rather than failing on
            # the absence of a new helper. Its unchecked write loses the file.
            code = function((APP / "subghz_remote_app_i.c").read_text(),
                            "subrem_save_protocol_to_file")
            source = source.replace("/* SAVE */", code + "\n" + r'''
static bool subrem_safe_save(Storage* storage, Stream* stream, const char* path) {
    (void)storage; FlipperFormat ff={.stream=stream};
    return subrem_save_protocol_to_file(&ff,path);
}
''')
        compile_run(source)


if __name__ == "__main__":
    unittest.main()
