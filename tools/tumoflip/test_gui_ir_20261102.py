"""Execute production redraw/timer/status and IR parsing code without device I/O."""

import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]


class NativeRegression(unittest.TestCase):
    @classmethod
    def compile_fixture(cls, fixture, production, prefix, replacements=None):
        cls.temporary = tempfile.TemporaryDirectory(prefix=prefix)
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.work = Path(cls.temporary.name)
        cls.input = cls.work / "test.c"
        text = (ROOT / fixture).read_text().replace("/* PRODUCTION */", production)
        for marker, value in (replacements or {}).items():
            text = text.replace(marker, value)
        cls.input.write_text(text)
        cls.binary = cls.work / "test"
        cls.coverage = os.environ.get("GUI_IR_COVERAGE") == "1"
        flags = ["-fprofile-instr-generate", "-fcoverage-mapping"] if cls.coverage else []
        compiled = subprocess.run([
            "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
            "-fsanitize=address,undefined", "-fno-sanitize-recover=all", *flags,
            str(cls.input), "-o", str(cls.binary),
        ], capture_output=True, text=True)
        if compiled.returncode:
            raise AssertionError(compiled.stderr)

    @classmethod
    def tearDownClass(cls):
        if not getattr(cls, "coverage", False):
            return
        profiles = sorted(str(p) for p in cls.work.glob("*.profraw"))
        merged = cls.work / "coverage.profdata"
        subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", *profiles,
                        "-o", str(merged)], check=True)
        report = json.loads(subprocess.check_output([
            "xcrun", "llvm-cov", "export", str(cls.binary),
            "-instr-profile=" + str(merged),
        ], text=True))
        for item in report["data"][0]["functions"]:
            name = item["name"].rsplit(":", 1)[-1]
            if name not in cls.covered_functions:
                continue
            # File 0 is the production function's source body. Additional file
            # IDs are expansions of host-only assert/model/logging stubs, not
            # firmware branches. Clang also emits zero-length source anchors
            # for assert macro failure arms. Keep the inclusive count visible,
            # but gate non-empty function-body regions, not those anchors.
            inclusive = [r for r in item["regions"] if r[-1] == 0]
            regions = [r for r in inclusive if r[5] == 0 and r[:2] != r[2:4]]
            covered = sum(r[4] > 0 for r in regions)
            print(f"{name}: {covered}/{len(regions)} source regions; "
                  f"including host macros {sum(r[4] > 0 for r in inclusive)}/{len(inclusive)}")
            if not regions or covered / len(regions) < 0.8:
                lines = cls.input.read_text().splitlines()
                for region in regions:
                    if not region[4]:
                        print("uncovered", region[:4], lines[region[0] - 1].strip())
                raise AssertionError(f"Coverage below 80% for {name}")

    def run_case(self, case):
        executed = subprocess.run([str(self.binary), case], capture_output=True, text=True,
            env={**os.environ, "LLVM_PROFILE_FILE": str(self.work / "%p.profraw")})
        self.assertEqual(executed.returncode, 0, executed.stderr)


class GuiRedrawTests(NativeRegression):
    covered_functions = {
        "view_port_alloc", "view_port_update", "view_port_draw",
        "variable_item_list_enter_callback", "variable_item_list_exit_callback",
        "button_menu_enter_callback", "button_menu_exit_callback", "button_menu_free",
        "bt_statusbar_update", "storage_tick",
        "view_port_lock", "view_port_unlock", "view_port_get_orientation",
        "view_port_try_lock",
    }

    @classmethod
    def setUpClass(cls):
        gui = ROOT / "applications/services/gui"
        viewport = (gui / "view_port.c").read_text()
        production = ""
        for name in ("lock", "unlock"):
            signature = f"static __attribute__((noinline)) void view_port_{name}("
            if signature in viewport:
                production += function(viewport, signature) + "\n"
        signature = "static __attribute__((noinline)) bool view_port_try_lock("
        if signature in viewport:
            production += function(viewport, signature) + "\n"
        production += "\n".join(function(viewport, signature) for signature in (
            "ViewPort* view_port_alloc(", "void view_port_free(", "void view_port_set_width(",
            "uint8_t view_port_get_width(", "void view_port_set_height(", "uint8_t view_port_get_height(",
            "bool view_port_is_enabled(", "void view_port_enabled_set(",
            "void view_port_draw_callback_set(", "void view_port_gui_set(",
            "ViewPortOrientation view_port_get_orientation(",
            "void view_port_update(", "void view_port_draw("))
        replacements = {}
        for module, functions in (
            ("variable_item_list", ["static void variable_item_list_scroll_timer_callback(",
                "void variable_item_list_locked_timer_callback(", "VariableItemList* variable_item_list_alloc(",
                "void variable_item_list_free("]),
            ("button_menu", ["static void button_menu_process_timer_callback(",
                "ButtonMenu* button_menu_alloc(", "void button_menu_free("]),
        ):
            source = (gui / "modules" / (module + ".c")).read_text()
            owner = "ButtonMenu" if module == "button_menu" else "VariableItemList"
            array = "ButtonMenuItemArray_t" if module == "button_menu" else "VariableItemArray_t"
            controller = re.search(rf"struct {owner} \{{.*?^}};", source, re.M | re.S).group()
            model = re.search(rf"typedef struct \{{\n    {array} items;.*?}} {owner}Model;", source, re.S).group()
            replacements[f"/* {owner.upper()}_TYPES */"] = controller + "\n" + model
            if owner == "ButtonMenu":
                replacements["/* BUTTON_TIMER */"] = ("menu->scroll_timer" if "scroll_timer" in controller
                    else "((ButtonMenuModel*)menu->view->model)->scroll_timer")
            for suffix in ("enter_callback", "exit_callback"):
                signature = f"static void {module}_{suffix}("
                if signature in source:
                    production += "\n" + function(source, signature)
            production += "\n" + "\n".join(function(source, s) for s in functions)
        production += "\n" + function((ROOT / "applications/services/bt/bt_service/bt.c").read_text(),
                                        "static void bt_statusbar_update(Bt* bt) {")
        storage = (ROOT / "applications/services/storage/storage.c").read_text()
        production += "\n" + function(storage, "Storage* storage_app_alloc(")
        production += "\n" + function(storage, "void storage_tick(")
        cls.compile_fixture("tools/tumoflip/tests/gui_redraw_host.c", production, "tumo-gui-redraw-", replacements)

    def test_covered_viewport_coalesces_updates_and_rearms_after_actual_draw(self):
        self.run_case("viewport")

    def test_mutex_timeout_forwards_update_without_unowned_unlock_or_flag_write(self):
        self.run_case("mutex")

    def test_blocking_viewport_accessors_keep_values_and_balanced_mutex(self):
        self.run_case("getters")

    def test_variable_list_timer_runs_only_between_enter_and_exit(self):
        self.run_case("list")

    def test_ir_button_timer_and_teardown_do_not_block_under_model_lock(self):
        self.run_case("buttons")

    def test_ble_status_redraw_preserves_beacon_and_app_bridge_transfer_icon(self):
        self.run_case("bt")

    def test_sd_mounted_error_transitions_redraw_without_fake_mount_events(self):
        self.run_case("storage")

    def test_uncover_paths_still_force_gui_redraw(self):
        source = (ROOT / "applications/services/gui/gui.c").read_text()
        for name in ("void gui_add_view_port(", "void gui_remove_view_port(",
                     "void gui_view_port_send_to_front(", "void gui_view_port_send_to_back(",
                     "void gui_add_framebuffer_callback(", "void gui_set_lockdown(",
                     "void gui_set_lockdown_inhibit(", "void gui_set_status_bar_hidden(",
                     "void gui_direct_draw_release("):
            self.assertIn("gui_update(", function(source, name))
        viewport = (ROOT / "applications/services/gui/view_port.c").read_text()
        self.assertIn("gui_update(", function(viewport, "void view_port_enabled_set("))

    def test_no_retained_apps_browser_or_api_bump(self):
        loader = (ROOT / "applications/services/loader/loader_applications.c").read_text()
        self.assertIn("dialog_file_browser_show(", loader)
        self.assertNotIn("FileBrowser* browser;", loader)
        self.assertEqual((ROOT / "targets/f7/api_symbols.csv").read_text().splitlines()[1],
                         "Version,+,88.23,,")


class InfraredDatabaseTests(NativeRegression):
    covered_functions = {"infrared_brute_force_calculate_messages"}

    @classmethod
    def setUpClass(cls):
        source = (ROOT / "lib/infrared/signal/infrared_brute_force.c").read_text()
        error = (ROOT / "lib/infrared/signal/infrared_error_code.h").read_text()
        error = re.sub(r'^#pragma once\n|^#ifdef __cplusplus\nextern "C" \{\n#endif\n|'
                       r'^#ifdef __cplusplus\n}\n#endif\n', '', error, flags=re.M)
        cls.compile_fixture("tools/tumoflip/tests/ir_database_host.c", error + "\n" +
            function(source, "InfraredErrorCode infrared_brute_force_calculate_messages("),
            "tumo-ir-database-")

    def test_unknown_first_button_fails_without_dereference_or_file_write(self):
        self.run_case("unknown")

    def test_unrequested_valid_names_are_skipped_without_losing_requested_offsets(self):
        self.run_case("mixed")

    def test_known_names_keep_offsets_and_successful_eof(self):
        self.run_case("valid")

    def test_read_failure_and_invalid_body_do_not_become_success(self):
        self.run_case("invalid")

    def test_empty_database_and_open_failure_report_error_and_free_scratch(self):
        self.run_case("empty")

    def test_unconfigured_database_returns_error_without_opening_storage(self):
        self.run_case("unconfigured")


if __name__ == "__main__":
    unittest.main()
