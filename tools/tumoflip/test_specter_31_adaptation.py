"""Regressions for the selected Specter 3.1.1 fixes and package boundary."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/specter"


def read(path: str) -> str:
    return (APP / path).read_text(encoding="utf-8")


def function(text: str, signature: str) -> str:
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start : index + 1]
    raise AssertionError(f"unterminated function: {signature}")


class Specter31AdaptationTests(unittest.TestCase):
    def test_proximity_and_feedback_use_the_canonical_meter_scale(self) -> None:
        detector = read("helpers/field_detector.c")
        stats = read("helpers/field_detector.h")
        sweep = read("views/sweep_view.c")
        sweep_scene = read("scenes/specter_scene_sweep.c")
        watch_scene = read("scenes/specter_scene_watch.c")

        self.assertTrue("strength_ref" in stats and "saturated_ref" in stats)
        self.assertIn("s->strength_ref = shown_ref;", detector)
        self.assertIn("s->saturated_ref = field_scale_is_saturated", detector)
        self.assertIn("field_proximity_word(m->strength_ref, m->saturated_ref)", sweep)
        self.assertIn("st.saturated_ref && !was_saturated", sweep_scene)
        self.assertIn("360u - 3u * st.strength_ref", sweep_scene)
        self.assertIn("360u - 3u * st.strength_ref", watch_scene)

    def test_v2_settings_migrate_without_losing_preferences(self) -> None:
        compiler = shutil.which("cc")
        if compiler is None:
            self.skipTest("host C compiler is unavailable")

        settings = read("helpers/specter_settings.c")

        program = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "applications_user/specter/helpers/specter_settings.h"
#define furi_assert(value) assert(value)
#define SETTINGS_PATH "specter.conf"
#define SETTINGS_MAGIC 0x5Cu
#define SETTINGS_VERSION 3u
#define SETTINGS_VERSION_V2 2u
typedef struct {
    uint8_t sensitivity_index;
    uint8_t custom_threshold;
    uint8_t survey_index;
    bool sound;
    bool vibro;
    bool led;
    bool stealth;
    bool logging;
    bool meter_raw;
} SpecterSettingsV2;
''' + r'''
static SpecterSettingsV2 saved_old;
static bool saved_struct_load(const char* path, void* out, size_t bytes,
                              uint8_t magic, uint8_t version) {
    (void)path;
    assert(magic == SETTINGS_MAGIC);
    if(version != SETTINGS_VERSION_V2 || bytes != sizeof(saved_old)) return false;
    memcpy(out, &saved_old, bytes);
    return true;
}
''' + function(settings, "void specter_settings_set_defaults(") + "\n" + function(
            settings, "static void specter_settings_sanitise("
        ) + "\n" + function(settings, "void specter_settings_load(") + r'''
int main(void) {
    saved_old.sensitivity_index = 3;
    saved_old.custom_threshold = 17;
    saved_old.survey_index = 2;
    saved_old.sound = false;
    saved_old.vibro = false;
    saved_old.led = true;
    saved_old.stealth = true;
    saved_old.logging = false;
    saved_old.meter_raw = true;
    SpecterSettings current = {0};
    specter_settings_load(&current);
    assert(current.sensitivity_index == 3);
    assert(current.custom_threshold == 17);
    assert(current.survey_index == 2);
    assert(!current.sound && !current.vibro && current.led);
    assert(current.stealth && !current.logging && current.meter_raw);
    assert(current.intro);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "settings_migration.c"
            executable = Path(directory) / "settings_migration"
            source.write_text(program, encoding="utf-8")
            compile_result = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                 str(source), "-o", str(executable)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            subprocess.run([str(executable)], check=True)
        self.assertIn("SETTINGS_VERSION_V2", settings)
        self.assertIn("SpecterSettingsV2 old", settings)

    def test_intro_is_optional_and_never_replays_on_back_navigation(self) -> None:
        self.assertTrue(
            (APP / "scenes/specter_scene_splash.c").is_file(),
            "optional splash scene is missing",
        )
        scene = read("scenes/specter_scene_splash.c")
        settings_scene = read("scenes/specter_scene_settings.c")
        renderer = (ROOT / "tools/tumoflip/render_specter_native.py").read_text(
            encoding="utf-8"
        )

        self.assertIn("app->settings.intro", scene)
        self.assertIn("scene_manager_get_scene_state(app->scene_manager, SpecterSceneSplash)", scene)
        self.assertIn("view_dispatcher_stop(app->view_dispatcher)", scene)
        self.assertIn('"Intro"', settings_scene)
        self.assertIn('"splash"', renderer)

    def test_provenance_and_version_mark_selective_package_adaptation(self) -> None:
        manifest = read("application.fam")
        provenance = read("PROVENANCE.md")
        self.assertIn('fap_package_only=True', manifest)
        self.assertTrue('fap_version="3.3.0"' in manifest, "package version not advanced")
        self.assertIn("079474ba10c54fc7f9e23d4f896a2982dda0a64e", provenance)


if __name__ == "__main__":
    unittest.main()
