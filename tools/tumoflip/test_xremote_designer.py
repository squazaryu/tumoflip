#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
APP_DIR = REPO_ROOT / "applications_user/flipper_xremote"


class XRemoteDesignerTest(unittest.TestCase):
    def test_designer_is_visible_from_xremote_menu(self) -> None:
        source = (APP_DIR / "xremote.c").read_text(encoding="utf-8")
        header = (APP_DIR / "views/xremote_common_view.h").read_text(encoding="utf-8")
        version_header = (APP_DIR / "xremote.h").read_text(encoding="utf-8")
        manifest = (APP_DIR / "application.fam").read_text(encoding="utf-8")

        self.assertIn('#include "xremote_designer.h"', source)
        self.assertIn('"Designer", XRemoteViewDesigner', source)
        self.assertIn("xremote_designer_alloc(app->app_ctx)", source)
        self.assertIn("XRemoteViewDesigner", header)
        self.assertIn("XRemoteViewDesignerMap", header)
        self.assertIn('fap_version="1.11.0"', manifest)
        self.assertIn("#define XREMOTE_VERSION_MIN  11", version_header)

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
        header = (APP_DIR / "views/xremote_common_view.h").read_text(encoding="utf-8")

        self.assertIn("ac_file_load_signal", engine)
        self.assertIn("ac_ir_read_signal_body", engine)
        self.assertIn("ac_ir_signal_send", engine)
        self.assertIn('"Add Smart AC"', app)
        self.assertIn('"Add Button AC"', app)
        self.assertIn("xremote_ac_smart_send", app)
        self.assertIn("ac_remote_panel_add_item", app)
        self.assertIn("XRemoteViewAcSmartDelete", header)

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

    def test_ac_smart_long_ok_deletes_saved_remote_with_confirmation(self) -> None:
        app = (APP_DIR / "xremote_ac.c").read_text(encoding="utf-8")

        self.assertIn("submenu_add_item_ex(", app)
        self.assertIn("xremote_ac_submenu_callback_ex", app)
        self.assertIn("input_type == InputTypeLong", app)
        self.assertIn('dialog_ex_set_header(ctx->delete_dialog, "Delete AC?"', app)
        self.assertIn('dialog_ex_set_right_button_text(ctx->delete_dialog, "Delete")', app)
        self.assertIn("storage_common_remove(ctx->storage, path)", app)
        self.assertIn("(error == FSE_OK) || (error == FSE_NOT_EXIST)", app)

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


class XRemoteDeviceProfilesTest(unittest.TestCase):
    def test_device_profiles_are_integrated_without_replacing_ir_or_ac(self) -> None:
        source = (APP_DIR / "xremote.c").read_text(encoding="utf-8")
        header = (APP_DIR / "views/xremote_common_view.h").read_text(encoding="utf-8")
        manifest = (APP_DIR / "application.fam").read_text(encoding="utf-8")

        self.assertIn('#include "xremote_device_profiles.h"', source)
        self.assertIn('"Device Profiles", XRemoteViewDeviceProfiles', source)
        self.assertIn("xremote_device_profiles_alloc(app->app_ctx)", source)
        self.assertIn("XRemoteViewDeviceLibrary", header)
        self.assertIn("XRemoteViewDeviceRuntime", header)
        self.assertIn('"subghz_radio_broker"', manifest)
        self.assertIn('"AC Smart", XRemoteViewAcSmart', source)
        self.assertIn('"Saved", XRemoteViewIRSubmenu', source)

    def test_profile_schema_is_versioned_and_references_source_files(self) -> None:
        header = (APP_DIR / "xremote_device_profile.h").read_text(encoding="utf-8")
        storage = (APP_DIR / "xremote_device_profile.c").read_text(encoding="utf-8")

        self.assertIn('"Tumo XRemote Device Profile"', header)
        self.assertIn("#define XREMOTE_DEVICE_PROFILE_VERSION   1U", header)
        self.assertIn('EXT_PATH("apps_data/tumoflip_xremote")', header)
        self.assertIn(
            '#define XREMOTE_DEVICE_PROFILE_FOLDER    XREMOTE_DEVICE_PROFILE_ROOT "/devices"',
            header,
        )
        for field in (
            '"IR File"',
            '"RF Count"',
            '"RF%lu Name"',
            '"RF%lu File"',
            '"RF%lu Protocol"',
            '"RF%lu Adapter"',
        ):
            self.assertIn(field, storage)
        self.assertNotIn("storage_common_copy", storage)
        self.assertIn("xremote_device_profile_storage_ready(storage)", storage)
        self.assertIn("xremote_device_profile_verify(storage, profile)", storage)
        self.assertIn('"%s.tmp"', storage)
        self.assertIn("storage_common_rename(storage, temporary, profile->path)", storage)

    def test_empty_ir_remote_cannot_create_a_device_profile(self) -> None:
        source = (APP_DIR / "xremote_device_profiles.c").read_text(encoding="utf-8")

        self.assertIn("infrared_remote_get_button_count(remote) == 0U", source)
        self.assertIn('"IR remote has no\\nusable commands."', source)

    def test_subghz_runtime_uses_broker_region_gate_and_existing_encoder(self) -> None:
        source = (APP_DIR / "xremote_subghz.c").read_text(encoding="utf-8")

        for required in (
            "subghz_radio_broker_acquire(",
            "furi_hal_subghz_is_tx_allowed(info.frequency_hz)",
            "subghz_transmitter_alloc_init(",
            "subghz_transmitter_deserialize(",
            "subghz_devices_start_async_tx(",
            "subghz_devices_stop_async_tx(device)",
            "subghz_radio_broker_release(broker, &lease)",
            "XRemoteSubGhzStatusExternalUnavailable",
        ):
            self.assertIn(required, source)

    def test_only_reviewed_princeton_field_can_be_changed(self) -> None:
        source = (APP_DIR / "xremote_subghz.c").read_text(encoding="utf-8")
        profiles = (APP_DIR / "xremote_device_profiles.c").read_text(encoding="utf-8")

        self.assertIn('strcmp(info->protocol, "Princeton") != 0', source)
        self.assertIn("info->bit_count != 24U", source)
        self.assertIn("{0x1U, 0x2U, 0x4U, 0x8U}", source)
        self.assertIn("subghz_block_generic_global_button_override_set(button)", source)
        self.assertNotIn("counter_override_set", source)
        for protocol in (
            "SUBGHZ_PROTOCOL_KEELOQ_NAME",
            "SUBGHZ_PROTOCOL_NICE_FLOR_S_NAME",
            "SUBGHZ_PROTOCOL_SECPLUS_V2_NAME",
            "SUBGHZ_PROTOCOL_SOMFY_TELIS_NAME",
            "SUBGHZ_PROTOCOL_FAAC_SLH_NAME",
        ):
            self.assertIn(protocol, source)
        self.assertIn("XRemoteSubGhzStatusChangingCodeBlocked", source)
        self.assertIn("if(info.changing_code)", profiles)
        self.assertIn('"Changing-code signal\\nis not supported."', profiles)

    def test_runtime_has_adaptive_paged_controls_and_actionable_states(self) -> None:
        source = (APP_DIR / "xremote_device_profiles.c").read_text(encoding="utf-8")
        version = (APP_DIR / "xremote.h").read_text(encoding="utf-8")

        for required in (
            'elements_button_left(canvas, "Back")',
            'elements_button_center(canvas, "Send")',
            "#define XREMOTE_DEVICE_PAGE_SIZE",
            "#define XREMOTE_DEVICE_GRID_COLUMNS",
            "xremote_device_page_count",
            "xremote_device_page_start",
            "xremote_device_runtime_draw_cell",
            "xremote_device_runtime_command_label",
            "canvas_draw_rbox",
            "canvas_draw_rframe",
            "xremote_device_grid_move",
            "xremote_device_list_move",
            "InputKeyUp",
            "InputKeyDown",
            "InputKeyLeft",
            "InputKeyRight",
            "InputKeyOk",
            "InputTypeLong && event->key == InputKeyOk",
            "xremote_device_runtime_toggle_radio",
            "xremote_device_runtime_update_selection_status",
            "context->rf_status[rf_index] = status",
            '"IR file missing"',
            '"No usable commands"',
            "xremote_subghz_status_name(status)",
        ):
            self.assertIn(required, source)
        self.assertIn("row == 0U ? 15U : 32U", source)
        self.assertIn("(uint8_t)(17U + slot * 18U)", source)
        self.assertIn('"OK sends, hold OK INT/EXT.', source)
        self.assertIn("#define XREMOTE_VERSION_MIN  11", version)

    def test_profile_editor_supports_on_device_management(self) -> None:
        source = (APP_DIR / "xremote_device_profiles.c").read_text(encoding="utf-8")
        header = (APP_DIR / "views/xremote_common_view.h").read_text(encoding="utf-8")

        for required in (
            '"Profile Library"',
            "XRemoteViewDeviceEditor",
            "xremote_device_editor_start_text",
            "xremote_device_editor_replace_ir",
            "xremote_device_editor_detach_ir",
            "xremote_device_editor_remove_rf",
            "xremote_device_profile_move_rf",
            '"Profile Editor"',
            '"Source missing"',
            '"Save failed; reopen profile"',
        ):
            self.assertIn(required, source if required != "XRemoteViewDeviceEditor" else header)

    def test_profile_editor_mutations_preserve_source_files(self) -> None:
        storage = (APP_DIR / "xremote_device_profile.c").read_text(encoding="utf-8")
        source = (APP_DIR / "xremote_device_profiles.c").read_text(encoding="utf-8")

        for required in (
            "xremote_device_profile_rename",
            "xremote_device_profile_set_ir",
            "xremote_device_profile_rename_rf",
            "xremote_device_profile_move_rf",
            "xremote_device_profile_remove_rf",
            "memmove(",
        ):
            self.assertIn(required, storage)
        self.assertIn('"Source .ir stays on SD."', source)
        self.assertIn(".sub stays on SD.", source)
        self.assertNotIn("storage_common_remove(context->storage, context->profile->ir_path)", source)
        self.assertNotIn(
            "storage_common_remove(context->storage, context->profile->rf", source
        )

    def test_profile_library_lists_health_and_bounded_actions(self) -> None:
        source = (APP_DIR / "xremote_device_profiles.c").read_text(encoding="utf-8")

        for required in (
            "#define XREMOTE_DEVICE_LIBRARY_MAX",
            "storage_dir_open(directory, XREMOTE_DEVICE_PROFILE_FOLDER)",
            "XRemoteDeviceLibraryReady",
            "XRemoteDeviceLibraryMissing",
            "XRemoteDeviceLibraryInvalid",
            "xremote_device_library_ir_count",
            "xremote_device_library_refresh",
            '"Profile Library"',
            '"MISS"',
            '"BAD"',
            '"Open"',
            '"Edit"',
            '"Copy"',
            '"Delete"',
            "xremote_device_library_keep_visible",
        ):
            self.assertIn(required, source)
        self.assertNotIn('"Open Profile"', source)
        self.assertNotIn('"Manage Profile"', source)
        self.assertNotIn('"Delete Profile"', source)

    def test_profile_duplicate_is_transactional_and_sources_are_references(self) -> None:
        storage = (APP_DIR / "xremote_device_profile.c").read_text(encoding="utf-8")
        header = (APP_DIR / "xremote_device_profile.h").read_text(encoding="utf-8")
        source = (APP_DIR / "xremote_device_profiles.c").read_text(encoding="utf-8")

        self.assertIn("xremote_device_profile_duplicate(", header)
        self.assertIn("*duplicate = *source", storage)
        self.assertIn("xremote_device_profile_create_path(", storage)
        self.assertIn("xremote_device_profile_store(storage, duplicate)", storage)
        self.assertIn("xremote_device_profile_delete_path(", header)
        self.assertIn("XREMOTE_DEVICE_PROFILE_FOLDER", storage)
        self.assertIn("XREMOTE_DEVICE_PROFILE_EXTENSION", storage)
        self.assertIn("strchr(filename, '/') != NULL", storage)
        self.assertIn("Sources stay unchanged.", source)
        self.assertNotIn("storage_common_copy", storage)

    def test_profile_library_supports_both_screen_orientations(self) -> None:
        source = (APP_DIR / "xremote_device_profiles.c").read_text(encoding="utf-8")

        self.assertIn("ViewOrientationVertical", source)
        self.assertIn("vertical ? 60U : 124U", source)
        self.assertIn("vertical ? 22U : 11U", source)
        self.assertIn("(uint8_t)(17U + action * 20U)", source)
        self.assertIn("vertical && event->key == InputKeyUp", source)
        self.assertIn("vertical && event->key == InputKeyDown", source)
        self.assertIn("!vertical && event->key == InputKeyLeft", source)
        self.assertIn("!vertical && event->key == InputKeyRight", source)

    def test_profile_editor_keeps_atomic_rollback_and_nonempty_profile(self) -> None:
        storage = (APP_DIR / "xremote_device_profile.c").read_text(encoding="utf-8")
        source = (APP_DIR / "xremote_device_profiles.c").read_text(encoding="utf-8")

        self.assertIn('"%s.tmp"', storage)
        self.assertIn('"%s.bak"', storage)
        self.assertIn("storage_common_rename(storage, backup, profile->path)", storage)
        self.assertIn("storage_common_remove(storage, profile->path)", storage)
        self.assertIn("xremote_device_profile_reload(context)", source)
        self.assertIn('"Save failed; restored"', source)
        self.assertIn('"Profile needs command"', source)
        self.assertIn("profile->ir_path[0] == '\\0' && profile->rf_count == 1U", storage)

    def test_profile_editor_uses_framed_controls_without_footer_overlap(self) -> None:
        source = (APP_DIR / "xremote_device_profiles.c").read_text(encoding="utf-8")
        editor = source.split("static void xremote_device_editor_draw", 1)[1].split(
            "static void xremote_device_editor_refresh", 1
        )[0]

        self.assertIn("elements_slightly_rounded_box", editor)
        self.assertIn("elements_button_left", editor)
        self.assertIn("elements_button_center", editor)
        self.assertIn("elements_button_right", editor)
        self.assertIn("canvas, 3, 50, 122, AlignLeft", editor)
        self.assertNotIn("canvas, 3, 59", editor)


if __name__ == "__main__":
    unittest.main()
