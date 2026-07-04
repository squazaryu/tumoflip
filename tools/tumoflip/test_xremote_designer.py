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
        self.assertIn('fap_version="1.7"', manifest)

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
            "dialog_message_show_storage_error",
        ):
            self.assertIn(required, designer)

    def test_designer_allows_row_preview_before_export(self) -> None:
        designer = (APP_DIR / "xremote_designer.c").read_text(encoding="utf-8")

        self.assertIn("xremote_designer_map_enter_callback", designer)
        self.assertIn("xremote_app_send_signal", designer)
        self.assertIn('"No command\\nmapped"', designer)
        self.assertIn('"Save As..."', designer)
        self.assertIn("XRemoteViewTextInput", designer)


if __name__ == "__main__":
    unittest.main()
