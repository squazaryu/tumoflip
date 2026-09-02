#!/usr/bin/env python3
"""Source contracts for saving a paused Universal Remote candidate."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMMON = ROOT / "applications/main/infrared/scenes/common/infrared_scene_universal_common.c"
SAVE = ROOT / "applications/main/infrared/scenes/infrared_scene_universal_save.c"
SAVE_NAME = ROOT / "applications/main/infrared/scenes/infrared_scene_universal_save_name.c"
CONFIG = ROOT / "applications/main/infrared/scenes/infrared_scene_config.h"
PROGRESS = ROOT / "applications/main/infrared/views/infrared_progress_view.c"
PROGRESS_HEADER = ROOT / "applications/main/infrared/views/infrared_progress_view.h"
APP = ROOT / "applications/main/infrared/infrared_app.c"
REMOTE_LIST = ROOT / "applications/main/infrared/scenes/infrared_scene_remote_list.c"
BRUTE_FORCE = ROOT / "lib/infrared/signal/infrared_brute_force.c"
API = ROOT / "targets/f7/api_symbols.csv"


def source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def region(contents: str, start: str, end: str) -> str:
    start_index = contents.index(start)
    end_index = contents.index(end, start_index)
    return contents[start_index:end_index]


class InfraredUniversalSaveTest(unittest.TestCase):
    def test_save_flow_is_registered_as_three_explicit_scenes(self) -> None:
        config = source(CONFIG)
        for line in (
            "ADD_SCENE(infrared, universal_save, UniversalSave)",
            "ADD_SCENE(infrared, universal_save_name, UniversalSaveName)",
            "ADD_SCENE(infrared, universal_save_done, UniversalSaveDone)",
        ):
            self.assertIn(line, config)

    def test_down_saves_only_from_the_paused_progress_state(self) -> None:
        progress = source(PROGRESS)
        header = source(PROGRESS_HEADER)
        input_handler = region(
            progress,
            "bool infrared_progress_view_input_callback(",
            "InfraredProgressView* infrared_progress_view_alloc(",
        )
        paused_start = input_handler.index("if(model->is_paused) {")
        running_start = input_handler.index("} else {", paused_start)
        paused = input_handler[paused_start:running_start]
        running = input_handler[running_start:]

        self.assertIn("InfraredProgressViewInputSave", header)
        self.assertIn("InputKeyDown", paused)
        self.assertIn("InfraredProgressViewInputSave", paused)
        self.assertNotIn("InputKeyDown", running)

    def test_paused_controls_fit_inside_the_vertical_progress_card(self) -> None:
        progress = source(PROGRESS)
        self.assertIn("uint8_t y = 25;", progress)
        self.assertIn("uint8_t height = 81;", progress)
        self.assertIn("model->is_paused ? 42 : 50", progress)
        self.assertIn('buttons_y + 35, "save"', progress)
        # Card bottom is 25 + 81 = 106; the last text baseline is 25 + 42 + 35 = 102.

    def test_candidate_is_copied_before_the_bruteforce_session_is_closed(self) -> None:
        common = source(COMMON)
        handler = region(
            common,
            "case InfraredProgressViewInputSave:",
            "case InfraredProgressViewInputSendSingle:",
        )

        load = handler.index("infrared_brute_force_load_signal")
        name = handler.index("infrared_brute_force_get_current_record_name")
        stop = handler.index("infrared_brute_force_stop")
        next_scene = handler.index("InfraredSceneUniversalSave")
        self.assertLess(load, stop)
        self.assertLess(name, stop)
        self.assertLess(stop, next_scene)

    def test_signal_loader_does_not_transmit(self) -> None:
        brute_force = source(BRUTE_FORCE)
        loader = region(
            brute_force,
            "bool infrared_brute_force_load_signal(",
            "const char* infrared_brute_force_get_current_record_name(",
        )
        sender = region(
            brute_force,
            "bool infrared_brute_force_send(",
            "void infrared_brute_force_add_record(",
        )

        self.assertNotIn("infrared_signal_transmit", loader)
        self.assertIn("infrared_brute_force_load_signal", sender)
        self.assertIn("infrared_signal_transmit", sender)

    def test_existing_remote_is_backed_up_and_restored_on_append_failure(self) -> None:
        save = source(SAVE)
        preserving = region(
            save,
            "static InfraredErrorCode infrared_scene_universal_save_append_preserving(",
            "static void infrared_scene_universal_save_add_to_existing(",
        )

        backup = preserving.index("storage_common_copy")
        append = preserving.index("infrared_remote_append_signal")
        restore = preserving.index("storage_common_rename")
        self.assertLess(backup, append)
        self.assertLess(append, restore)
        self.assertIn("backup_info.size != original_info.size", preserving)
        self.assertIn("if(INFRARED_ERROR_PRESENT(error))", preserving)
        self.assertIn("backup is intentionally retained", preserving)

    def test_new_remote_uses_a_vacant_name_and_removes_only_partial_new_file(self) -> None:
        app = source(APP)
        helper = region(
            app,
            "InfraredErrorCode infrared_add_named_remote_with_button(",
            "InfraredErrorCode infrared_add_remote_with_button(",
        )

        self.assertLess(
            helper.index("infrared_find_vacant_remote_name"),
            helper.index("infrared_remote_create"),
        )
        self.assertIn("infrared_remote_remove(remote);", helper)
        self.assertIn("infrared_add_named_remote_with_button", source(SAVE_NAME))

    def test_file_browsers_start_in_the_infrared_directory(self) -> None:
        for contents in (source(SAVE), source(REMOTE_LIST)):
            self.assertIn("browser_options.base_path = INFRARED_APP_FOLDER;", contents)
            self.assertIn("furi_string_empty(infrared->file_path)", contents)
            self.assertIn("furi_string_set(infrared->file_path, INFRARED_APP_FOLDER)", contents)

    def test_internal_library_symbols_do_not_change_public_api_version(self) -> None:
        api = source(API)
        self.assertIn("Version,+,88.4,,", api)
        self.assertIn(
            "Function,-,infrared_brute_force_get_current_record_name", api
        )
        self.assertIn("Function,-,infrared_brute_force_load_signal", api)
        self.assertNotIn(
            "Function,+,infrared_brute_force_get_current_record_name", api
        )
        self.assertNotIn("Function,+,infrared_brute_force_load_signal", api)


if __name__ == "__main__":
    unittest.main()
