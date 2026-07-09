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

    def test_ac_smart_is_integrated_as_xremote_child_app(self) -> None:
        source = (APP_DIR / "xremote.c").read_text(encoding="utf-8")
        header = (APP_DIR / "views/xremote_common_view.h").read_text(encoding="utf-8")
        manifest = (APP_DIR / "application.fam").read_text(encoding="utf-8")

        self.assertIn('#include "xremote_ac.h"', source)
        self.assertIn('"AC Smart", XRemoteViewAcSmart', source)
        self.assertIn("xremote_ac_alloc(app->app_ctx)", source)
        self.assertIn("XRemoteViewAcSmart", header)
        self.assertIn("XRemoteViewAcSmartRemote", header)
        self.assertIn('"storage"', manifest)

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


class XRemoteACSmartTest(unittest.TestCase):
    def test_ac_smart_engine_uses_fz_ac_compatible_ir_names(self) -> None:
        engine = (APP_DIR / "ac_smart/ac_ir.c").read_text(encoding="utf-8")
        header = (APP_DIR / "ac_smart/ac_ir.h").read_text(encoding="utf-8")
        app = (APP_DIR / "xremote_ac.c").read_text(encoding="utf-8")

        self.assertIn('#define XREMOTE_AC_DIR      EXT_PATH("apps_data/fz_ac")', app)
        self.assertIn('#define AC_OFF_NAME         "Off"', header)
        self.assertIn('snprintf(out, out_size, "%s %u", preset, temp);', engine)
        self.assertIn("ac_smart_name_parse", engine)
        self.assertIn("ac_smart_write_preset", engine)

    def test_ac_smart_runtime_loads_single_frame_on_send(self) -> None:
        engine = (APP_DIR / "ac_smart/ac_ir.c").read_text(encoding="utf-8")
        app = (APP_DIR / "xremote_ac.c").read_text(encoding="utf-8")

        self.assertIn("ac_file_load_signal", engine)
        self.assertIn("ac_ir_read_signal_body", engine)
        self.assertIn("ac_ir_signal_send", engine)
        self.assertIn('"Add Smart AC"', app)
        self.assertIn('"Add Button AC"', app)
        self.assertIn("xremote_ac_smart_send", app)
        self.assertIn("ac_remote_panel_add_item", app)

    def test_ac_smart_creator_writes_standard_ir_profile(self) -> None:
        app = (APP_DIR / "xremote_ac.c").read_text(encoding="utf-8")

        for required in (
            '"Add Smart AC"',
            '"Add Button AC"',
            "xremote_ac_start_name_input",
            "InfraredWorker* rx_worker;",
            "infrared_worker_rx_start(ctx->rx_worker)",
            "infrared_worker_rx_stop(ctx->rx_worker)",
            "ac_remote_save(&ctx->staged, ctx->storage, path)",
            "ac_smart_write_preset(",
            "LearnView* learn_view;",
            "SweepView* sweep_view;",
        ):
            self.assertIn(required, app)

    def test_ac_button_capture_keeps_hisense_basics_required(self) -> None:
        app = (APP_DIR / "xremote_ac.c").read_text(encoding="utf-8")
        engine = (APP_DIR / "ac_smart/ac_ir.c").read_text(encoding="utf-8")

        self.assertIn("#define XREMOTE_AC_BUTTON_REQUIRED_COUNT 4", app)
        self.assertIn("total = XREMOTE_AC_BUTTON_REQUIRED_COUNT;", app)
        self.assertIn("ctx->learn_index < XREMOTE_AC_BUTTON_REQUIRED_COUNT", app)
        self.assertIn('"POWER"', engine)
        self.assertIn("const bool has_fan = !smart && ctx->remote.signals[AcButtonFan].present;", app)
        self.assertIn("const bool has_vane = !smart && ctx->remote.signals[AcButtonVane].present;", app)

    def test_ac_smart_creator_cleans_up_rx_on_cancel_and_exit(self) -> None:
        app = (APP_DIR / "xremote_ac.c").read_text(encoding="utf-8")

        self.assertIn("xremote_ac_cancel_flow", app)
        self.assertIn("xremote_ac_rx_stop(ctx);", app)
        self.assertIn("infrared_worker_free(ctx->rx_worker)", app)
        self.assertIn("view_dispatcher_remove_view(view_disp, XRemoteViewAcSmartLearn)", app)
        self.assertIn("view_dispatcher_remove_view(view_disp, XRemoteViewAcSmartSweep)", app)
        self.assertIn("ac_ir_signal_reset(&ctx->off_capture)", app)

    def test_ac_smart_panel_initializes_button_matrix(self) -> None:
        panel = (APP_DIR / "ac_smart/views/ac_remote_panel.c").read_text(encoding="utf-8")
        reserve = panel.split("void ac_remote_panel_reserve", 1)[1].split(
            "void ac_remote_panel_free", 1
        )[0]

        self.assertIn("IconList_init(model->icons);", panel)
        self.assertIn("ButtonMatrix_safe_get(model->button_matrix, x)", reserve)
        self.assertIn("ButtonArray_safe_get(*array, y)", reserve)
        self.assertIn("*item = NULL;", reserve)
        self.assertNotIn("i > model->reserve_y", reserve)
        self.assertNotIn("LabelList_init(model->labels);", reserve)

    def test_ac_smart_capture_defers_rx_ui_work_to_dispatcher(self) -> None:
        app = (APP_DIR / "xremote_ac.c").read_text(encoding="utf-8")
        rx_callback = app.split("static void xremote_ac_rx_callback", 1)[1].split(
            "static void xremote_ac_rx_alloc", 1
        )[0]

        self.assertIn("view_dispatcher_send_custom_event", rx_callback)
        self.assertNotIn("xremote_ac_rx_stop(ctx);", rx_callback)
        self.assertNotIn("view_dispatcher_switch_to_view", rx_callback)
        self.assertIn("static bool xremote_ac_custom_event_callback", app)
        self.assertIn(
            "view_dispatcher_set_custom_event_callback(\n"
            "        ctx->app_ctx->view_dispatcher, xremote_ac_custom_event_callback)",
            app,
        )


if __name__ == "__main__":
    unittest.main()
