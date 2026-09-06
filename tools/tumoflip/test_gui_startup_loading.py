#!/usr/bin/env python3
"""Regression contracts for app-owned startup loading views."""

from pathlib import Path
import re
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
DISPATCHER = REPO_ROOT / "applications/services/gui/view_dispatcher.c"
DISPATCHER_HEADER = REPO_ROOT / "applications/services/gui/view_dispatcher.h"
DISPATCHER_INTERNAL = REPO_ROOT / "applications/services/gui/view_dispatcher_i.h"
LOADING = REPO_ROOT / "applications/services/gui/modules/loading.c"
ICON_ANIMATION = REPO_ROOT / "applications/services/gui/icon_animation.c"
ARCHIVE = REPO_ROOT / "applications/main/archive/archive.c"
DESKTOP_SETTINGS = (
    REPO_ROOT
    / "applications/settings/desktop_settings/desktop_settings_app.c"
)
DESKTOP_SETTINGS_HEADER = (
    REPO_ROOT
    / "applications/settings/desktop_settings/desktop_settings_app.h"
)


def function_body(source: str, signature: str) -> str:
    match = re.search(
        rf"{re.escape(signature)}.*?^}}",
        source,
        flags=re.DOTALL | re.MULTILINE,
    )
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    return match.group(0)


class GuiStartupLoadingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.dispatcher = DISPATCHER.read_text(encoding="utf-8")
        cls.dispatcher_header = DISPATCHER_HEADER.read_text(encoding="utf-8")
        cls.dispatcher_internal = DISPATCHER_INTERNAL.read_text(encoding="utf-8")
        cls.loading = LOADING.read_text(encoding="utf-8")
        cls.icon_animation = ICON_ANIMATION.read_text(encoding="utf-8")
        cls.archive = ARCHIVE.read_text(encoding="utf-8")
        cls.desktop_settings = DESKTOP_SETTINGS.read_text(encoding="utf-8")
        cls.desktop_settings_header = DESKTOP_SETTINGS_HEADER.read_text(encoding="utf-8")

    def test_public_loading_api_and_private_state_exist(self) -> None:
        self.assertIn("void view_dispatcher_show_loading(ViewDispatcher* view_dispatcher);", self.dispatcher_header)
        self.assertIn("struct Loading* loading;", self.dispatcher_internal)

    def test_dispatcher_initializes_loading_state(self) -> None:
        alloc = function_body(self.dispatcher, "ViewDispatcher* view_dispatcher_alloc_ex(FuriEventLoop* loop)")
        self.assertIn("view_dispatcher->loading = NULL;", alloc)

    def test_loading_is_lazy_and_owned_by_dispatcher(self) -> None:
        show = function_body(self.dispatcher, "void view_dispatcher_show_loading(ViewDispatcher* view_dispatcher)")
        self.assertIn("if(!view_dispatcher->loading)", show)
        self.assertIn("view_dispatcher->loading = loading_alloc();", show)
        self.assertIn("view_set_update_callback(view, view_dispatcher_update);", show)
        self.assertIn("view_set_update_callback_context(view, view_dispatcher);", show)
        self.assertIn("view_dispatcher_set_current_view(view_dispatcher, loading_view);", show)
        self.assertNotIn("ViewDict_set", show)

    def test_loading_cleanup_detaches_before_freeing(self) -> None:
        free = function_body(self.dispatcher, "void view_dispatcher_free(ViewDispatcher* view_dispatcher)")
        detach = free.index("gui_remove_view_port")
        cleanup = free.index("if(view_dispatcher->loading)")
        self.assertLess(detach, cleanup)
        self.assertIn("loading_get_view(view_dispatcher->loading)", free)
        self.assertIn("loading_free(view_dispatcher->loading);", free)

    def test_switching_away_clears_only_loading_queue(self) -> None:
        switch = function_body(self.dispatcher, "void view_dispatcher_set_current_view(ViewDispatcher* view_dispatcher, View* view)")
        self.assertIn("furi_message_queue_reset(view_dispatcher->input_queue);", switch)
        self.assertIn("!view_dispatcher->ongoing_input", switch)
        self.assertIn("view_dispatcher->current_view == loading_view", switch)
        self.assertIn("view != loading_view", switch)

    def test_loading_animation_loops_and_stops_on_view_exit(self) -> None:
        self.assertIn(
            "instance->frame = (instance->frame + 1) % instance->icon->frame_count;",
            self.icon_animation,
        )
        self.assertIn("icon_animation_start(model->icon);", self.loading)
        self.assertIn("icon_animation_stop(model->icon);", self.loading)
        self.assertIn("instance->frame = 0;", self.icon_animation)

    def test_archive_claims_screen_before_building_first_scene(self) -> None:
        start = function_body(self.archive, "int32_t archive_app(void* p)")
        attach = start.index("view_dispatcher_attach_to_gui")
        loading = start.index("view_dispatcher_show_loading")
        scene = start.index("scene_manager_next_scene")
        self.assertLess(attach, loading)
        self.assertLess(loading, scene)
        self.assertIn("view_stack_add_view(view_stack, loading_get_view(loading));", self.archive)

    def test_desktop_settings_claims_screen_without_a_loading_view_id(self) -> None:
        alloc = function_body(self.desktop_settings, "DesktopSettingsApp* desktop_settings_app_alloc(void)")
        attach = alloc.index("view_dispatcher_attach_to_gui")
        loading = alloc.index("view_dispatcher_show_loading")
        first_view = alloc.index("view_dispatcher_add_view")
        self.assertLess(attach, loading)
        self.assertLess(loading, first_view)
        self.assertNotIn("DesktopSettingsAppViewLoading", self.desktop_settings_header)
        self.assertNotIn("Loading* loading", self.desktop_settings_header)


if __name__ == "__main__":
    unittest.main()
