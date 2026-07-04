#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/flipper_xremote"


class XRemoteDesignerTest(unittest.TestCase):
    def test_designer_is_visible_from_xremote_menu(self) -> None:
        source = (APP_DIR / "xremote.c").read_text(encoding="utf-8")
        header = (APP_DIR / "views/xremote_common_view.h").read_text(encoding="utf-8")
        manifest = (APP_DIR / "application.fam").read_text(encoding="utf-8")

        self.assertIn('#include "xremote_designer.h"', source)
        self.assertIn('"Designer", XRemoteViewDesigner', source)
        self.assertIn("xremote_designer_alloc(app->app_ctx)", source)
        self.assertIn("XRemoteViewDesigner", header)
        self.assertIn("XRemoteViewDesignerMap", header)
        self.assertIn('fap_version="1.7.1"', manifest)

    def test_designer_exports_standard_ir_with_xremote_layout_metadata(self) -> None:
        designer = (APP_DIR / "xremote_designer.c").read_text(encoding="utf-8")

        for required in (
            "XREMOTE_APP_FOLDER",
            "XREMOTE_APP_EXTENSION",
            "infrared_remote_store(ctx->output_remote)",
            "infrared_remote_push_button(ctx->output_remote, target_name, signal)",
            "xremote_app_extension_store(buttons, output_path)",
            "xremote_app_buttons_set_remote_type(buttons, ctx->remote_type)",
            "xremote_designer_sanitize_name",
        ):
            self.assertIn(required, designer)

    def test_designer_handles_invalid_or_empty_authoring_states(self) -> None:
        designer = (APP_DIR / "xremote_designer.c").read_text(encoding="utf-8")

        for required in (
            "Cannot load\\nsource .ir",
            "Source .ir has\\nno commands",
            "Nothing mapped\\nSelect commands first",
            "Invalid remote\\nname",
            "Cannot save\\nlayout data",
            "xremote_designer_show_status",
        ):
            self.assertIn(required, designer)

        self.assertNotIn("xremote_designer_show_message", designer)

    def test_designer_allows_row_preview_before_export(self) -> None:
        designer = (APP_DIR / "xremote_designer.c").read_text(encoding="utf-8")

        self.assertIn("xremote_designer_map_enter_callback", designer)
        self.assertIn("xremote_app_send_signal", designer)
        self.assertIn('"No command\\nmapped"', designer)
        self.assertIn('"Save As..."', designer)
        self.assertIn("XRemoteViewTextInput", designer)

    def test_designer_status_uses_dispatcher_dialog_not_blocking_dialog(self) -> None:
        designer = (APP_DIR / "xremote_designer.c").read_text(encoding="utf-8")

        self.assertIn("DialogEx* dialog_ex;", designer)
        self.assertIn("XRemoteViewDialogExit", designer)
        self.assertIn("dialog_ex_set_result_callback", designer)
        self.assertIn("xremote_designer_dialog_callback", designer)
        self.assertNotIn("dialog_message_show(dialogs, message)", designer)


class XRemoteACLayoutTest(unittest.TestCase):
    def test_ac_layout_uses_standard_xremote_builder(self) -> None:
        source = (APP_DIR / "views/xremote_universal_view.c").read_text(encoding="utf-8")

        self.assertIn("static void xremote_universal_build_ac", source)
        self.assertIn("case XRemoteRemoteTypeAC:", source)
        self.assertIn("xremote_universal_build_ac(universal);", source)
        self.assertNotIn("xremote_hisense_ac", source)
        self.assertNotIn("XREMOTE_AC_PAGE_", source)

    def test_standard_ac_layout_keeps_aligned_two_by_three_grid(self) -> None:
        source = (APP_DIR / "views/xremote_universal_view.c").read_text(encoding="utf-8")
        start = source.index("static void xremote_universal_build_ac")
        end = source.index("static void xremote_universal_build_layout", start)
        ac_source = source[start:end]

        self.assertIn("button_panel_reserve(button_panel, 2, 3);", ac_source)
        for command in (
            "XREMOTE_COMMAND_OFF",
            "XREMOTE_COMMAND_DRY",
            "XREMOTE_COMMAND_COOL_HI",
            "XREMOTE_COMMAND_HEAT_HI",
            "XREMOTE_COMMAND_COOL_LO",
            "XREMOTE_COMMAND_HEAT_LO",
        ):
            self.assertIn(command, ac_source)


if __name__ == "__main__":
    unittest.main()
