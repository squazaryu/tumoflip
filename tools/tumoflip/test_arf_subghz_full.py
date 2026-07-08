#!/usr/bin/env python3

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


class ArfSubGhzFullTest(unittest.TestCase):
    def test_full_is_a_lightweight_hub(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/arf_subghz_full/application.fam"
        ).read_text(encoding="utf-8")

        self.assertIn('appid="arf_subghz_full"', manifest)
        self.assertIn('sources=["arf_subghz_hub.c"]', manifest)
        self.assertNotIn('appid="arf_subghz_standard"', manifest)
        self.assertNotIn('"scenes/*.c"', manifest)
        self.assertNotIn('"views/*.c"', manifest)

    def test_child_launchers_have_packaged_apps(self) -> None:
        start_scene = (
            REPO_ROOT / "applications_user/arf_subghz_full/arf_subghz_hub.c"
        ).read_text(encoding="utf-8")
        child_faps = set(re.findall(r'ARF_MODULES_PATH "([^"/]+\.fap)', start_scene))
        app_manifests = "\n".join(
            path.read_text(encoding="utf-8")
            for root in (REPO_ROOT / "applications", REPO_ROOT / "applications_user")
            for path in root.rglob("application.fam")
        )

        self.assertEqual(
            child_faps,
            {
                "arf_car_emulate.fap",
                "arf_counter_bf.fap",
                "arf_keeloq.fap",
                "arf_psa_decrypt.fap",
                "arf_status.fap",
                "proto_pirate.fap",
                "rolljam.fap",
                "subghz_bruteforcer.fap",
            },
        )
        self.assertIn('.target = "Sub-GHz"', start_scene)
        self.assertNotIn("#define ARF_STANDARD_PATH", start_scene)
        self.assertNotIn("arf_subghz_standard.fap", start_scene)
        self.assertNotIn('appid="arf_subghz_standard"', app_manifests)
        self.assertGreaterEqual(
            app_manifests.count(
                'fap_dist_path="apps_data/arf_subghz_full/modules/{filename}"'
            ),
            len(child_faps),
        )
        for filename in child_faps:
            appid = filename.removesuffix(".fap")
            self.assertIn(f'appid="{appid}"', app_manifests)

        self.assertIn('appid="rolljam"', app_manifests)
        self.assertNotIn(
            'fap_dist_path="apps_data/arf_subghz_full/modules/rolljam.fap"',
            app_manifests,
        )

    def test_legacy_duplicate_is_removed(self) -> None:
        self.assertFalse((REPO_ROOT / "applications_user/arf_subghz").exists())

    def test_desktop_routes_subghz_to_arf_hub_and_arf_tools_to_cockpit(self) -> None:
        loader_menu = (
            REPO_ROOT / "applications/services/loader/loader_menu.c"
        ).read_text(encoding="utf-8")
        self.assertIn(
            '#define ARF_SUBGHZ_FULL_APP_PATH EXT_PATH("apps/ARF Tools/arf_subghz_full.fap")',
            loader_menu,
        )
        self.assertIn(
            '#define MODULE_ONE_COCKPIT_APP_PATH EXT_PATH("apps/Module One/Diagnostics/cockpit.fap")',
            loader_menu,
        )
        self.assertIn('#define ARF_TOOLS_MENU_NAME "Cockpit"', loader_menu)
        self.assertNotIn('#define ARF_TOOLS_MENU_NAME "ARF Tools"', loader_menu)
        self.assertNotIn("loader_menu_arf_tools_callback", loader_menu)
        self.assertNotIn("loader_menu_arf_subghz_full_callback", loader_menu)
        self.assertIn('strcmp(name, "Sub-GHz") == 0', loader_menu)
        self.assertIn("name = ARF_SUBGHZ_FULL_APP_PATH", loader_menu)
        self.assertIn('strcmp(path, "Sub-GHz Remote") == 0', loader_menu)
        self.assertIn("path = MODULE_ONE_COCKPIT_APP_PATH", loader_menu)
        self.assertNotIn("loader_menu_esp32_marauder_callback", loader_menu)
        self.assertNotIn("loader_menu_external_app_available", loader_menu)
        self.assertNotIn(
            'EXT_PATH("apps/Module One/ESP32 Wi-Fi/esp32_wifi_marauder.fap")',
            loader_menu,
        )

    def test_desktop_menu_uses_direct_loader_launches(self) -> None:
        loader_menu = (
            REPO_ROOT / "applications/services/loader/loader_menu.c"
        ).read_text(encoding="utf-8")
        loader = (REPO_ROOT / "applications/services/loader/loader.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("loader_start_with_gui_error(loader, name, args)", loader_menu)
        self.assertNotIn("pending_launch", loader_menu)
        self.assertNotIn("view_dispatcher_stop(loader_menu->view_dispatcher)", loader_menu)
        self.assertNotIn("loader_menu_has_pending_launch", loader)

    def test_full_reopens_after_child_exit(self) -> None:
        start_scene = (
            REPO_ROOT / "applications_user/arf_subghz_full/arf_subghz_hub.c"
        ).read_text(encoding="utf-8")

        self.assertIn("Frequency Analyzer", start_scene)
        self.assertNotIn("ARF Analyzer", start_scene)
        self.assertEqual(start_scene.count("loader_enqueue_launch("), 2)
        self.assertIn("loader_get_application_launch_path", start_scene)
        self.assertIn("selected_item_arg", start_scene)
        self.assertIn("snprintf(selected_item_arg", start_scene)
        self.assertLess(
            start_scene.index("loader_clear_launch_queue(app->loader)"),
            start_scene.index("loader_enqueue_launch("),
        )

    def test_nested_hubs_clear_parent_deferred_launch_before_child_launch(self) -> None:
        arf_hub = (
            REPO_ROOT / "applications_user/arf_subghz_full/arf_subghz_hub.c"
        ).read_text(encoding="utf-8")
        cockpit = (
            REPO_ROOT / "applications_user/module_one_cockpit/module_one_cockpit.c"
        ).read_text(encoding="utf-8")

        self.assertIn("loader_clear_launch_queue(app->loader);", arf_hub)
        self.assertIn("loader_clear_launch_queue(app->loader);", cockpit)
        self.assertLess(
            cockpit.index("loader_clear_launch_queue(app->loader)"),
            cockpit.index("loader_enqueue_launch(app->loader, target->target"),
        )

    def test_loader_prearms_deferred_launch_loading_overlay(self) -> None:
        loader = (REPO_ROOT / "applications/services/loader/loader.c").read_text(
            encoding="utf-8"
        )

        deferred_launch = re.search(
            r"static bool loader_do_deferred_launch\(.*?\n}\n\n"
            r"static void loader_do_app_closed",
            loader,
            re.S,
        )
        self.assertIsNotNone(deferred_launch)
        self.assertIn("loading_get_view(loader->loading)", deferred_launch.group(0))
        self.assertIn("view_holder_send_to_front(loader->view_holder)", deferred_launch.group(0))
        self.assertIn(
            "if(!is_successful) view_holder_set_view(loader->view_holder, NULL)",
            deferred_launch.group(0),
        )
        self.assertNotIn(
            "\n    view_holder_set_view(loader->view_holder, NULL);\n"
            "    furi_string_free(error_message);",
            deferred_launch.group(0),
        )

        queue_empty = re.search(
            r"static void loader_do_emit_queue_empty_event\(.*?\n}\n\n"
            r"static bool loader_do_deferred_launch",
            loader,
            re.S,
        )
        self.assertIsNotNone(queue_empty)
        self.assertIn("view_holder_set_view(loader->view_holder, NULL)", queue_empty.group(0))

        enqueue_case = re.search(
            r"case LoaderMessageTypeEnqueueLaunch: \{(?P<body>.*?)\n\s*}\n"
            r"\s*case LoaderMessageTypeClearLaunchQueue",
            loader,
            re.S,
        )
        self.assertIsNotNone(enqueue_case)
        enqueue_body = enqueue_case.group("body")
        self.assertIn("queue_was_empty = loader->launch_queue.item_cnt == 0", enqueue_body)
        self.assertIn("loader_queue_push(&loader->launch_queue", enqueue_body)
        self.assertIn("queue_was_empty && loader_is_application_running(loader)", enqueue_body)
        self.assertIn("loading_get_view(loader->loading)", enqueue_body)
        self.assertIn("view_holder_send_to_front(loader->view_holder)", enqueue_body)

    def test_frequency_analyzer_fap_starts_directly(self) -> None:
        manifest = (
            REPO_ROOT / "applications_user/arf_subghz_full/application.fam"
        ).read_text(encoding="utf-8")
        app = (
            REPO_ROOT / "applications_user/arf_subghz_full/subghz.c"
        ).read_text(encoding="utf-8")
        hub = (
            REPO_ROOT / "applications_user/arf_subghz_full/arf_subghz_hub.c"
        ).read_text(encoding="utf-8")

        frequency_app = re.search(
            r'App\(\s+appid="arf_frequency_analyzer".*?\n\)', manifest, re.S
        )
        self.assertIsNotNone(frequency_app)
        self.assertNotIn("fap_dist_path", frequency_app.group(0))
        self.assertIn('fap_category="ARF Tools"', frequency_app.group(0))
        self.assertIn(
            "scene_manager_next_scene(subghz->scene_manager, SubGhzSceneFrequencyAnalyzer);",
            app,
        )
        self.assertIn('EXT_PATH("apps/ARF Tools/")', hub)
        self.assertIn(
            'ARF_TOOLS_PATH "arf_frequency_analyzer.fap"',
            hub,
        )

    def test_frequency_analyzer_notebook_has_short_tags(self) -> None:
        header = (
            REPO_ROOT
            / "applications_user/arf_subghz_full/views/subghz_frequency_analyzer.h"
        ).read_text(encoding="utf-8")
        view = (
            REPO_ROOT
            / "applications_user/arf_subghz_full/views/subghz_frequency_analyzer.c"
        ).read_text(encoding="utf-8")
        notebook = (
            REPO_ROOT
            / "applications_user/arf_subghz_full/helpers/subghz_frequency_notebook.c"
        ).read_text(encoding="utf-8")

        self.assertIn("SubGhzFrequencyAnalyzerNotebookTagField", header)
        self.assertIn("SubGhzFrequencyAnalyzerNotebookTagCount", header)
        for tag in ("field", "test", "noise", "other"):
            self.assertIn(f'return "{tag}";', notebook)

        self.assertIn("event->type == InputTypeLong && event->key == InputKeyDown", view)
        self.assertIn(
            "(instance->notebook_tag + 1) % SubGhzFrequencyAnalyzerNotebookTagCount",
            view,
        )
        self.assertIn('"Save"', view)
        self.assertIn('"RX"', view)
        self.assertIn("I_ButtonCenter_7x7", view)
        self.assertIn("const size_t button_height = 9", view)
        self.assertIn("const int32_t rx_right_margin = 14", view)
        self.assertIn("button_y + button_height / 2 + 1", view)
        self.assertIn("canvas_draw_box(canvas, save_x, button_y, save_width, button_height)", view)
        self.assertIn("canvas_draw_box(canvas, rx_x, button_y, rx_width, button_height)", view)
        self.assertIn("canvas_draw_str_aligned", view)
        self.assertNotIn("tag_x", view)
        self.assertNotIn('"OK Log"', view)
        self.assertIn("observation->notebook_tag = model->notebook_tag", view)
        self.assertIn("subghz_frequency_notebook_tag_name(observation)", notebook)
        self.assertNotIn("payload", notebook.lower())

    def test_frequency_analyzer_long_ok_keeps_receiver_shortcut(self) -> None:
        scene = (
            REPO_ROOT
            / "applications_user/arf_subghz_full/scenes/subghz_scene_frequency_analyzer.c"
        ).read_text(encoding="utf-8")
        view = (
            REPO_ROOT
            / "applications_user/arf_subghz_full/views/subghz_frequency_analyzer.c"
        ).read_text(encoding="utf-8")

        self.assertIn("SubGhzCustomEventViewFreqAnalOkLong", view)
        self.assertIn("SubGhzCustomEventViewFreqAnalOkLong", scene)
        self.assertIn("scene_manager_next_scene(subghz->scene_manager, SubGhzSceneReceiver)", scene)


if __name__ == "__main__":
    unittest.main()
