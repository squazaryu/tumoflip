#!/usr/bin/env python3
"""Lifecycle contracts for the labelled CUID dictionary loading view."""

from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]


def source(relative_path: str) -> str:
    return (REPO_ROOT / relative_path).read_text(encoding="utf-8")


class NfcCuidLoadingTest(unittest.TestCase):
    def test_loading_label_has_balanced_app_lifecycle(self) -> None:
        app = source("applications/main/nfc/nfc_app.c")
        allocation = app.index("instance->loading_label = loading_label_alloc();")
        registration = app.index("NfcViewLoadingLabel", allocation)
        removal = app.index(
            "view_dispatcher_remove_view(instance->view_dispatcher, "
            "NfcViewLoadingLabel);"
        )
        release = app.index("loading_label_free(instance->loading_label);")

        self.assertLess(allocation, registration)
        self.assertLess(registration, removal)
        self.assertLess(removal, release)

    def test_cuid_scan_shows_and_hides_label_around_prepare(self) -> None:
        scene = source(
            "applications/main/nfc/helpers/protocol_support/mf_classic/"
            "mf_classic_extra_scenes.c"
        )
        show = scene.index(
            'nfc_show_loading_label_popup(instance, "CUID dictionary\\n'
            'is loading", true);'
        )
        prepare = scene.index(
            "mf_classic_scene_dict_attack_prepare_view(instance);",
            show,
        )
        hide = scene.index(
            "nfc_show_loading_label_popup(instance, NULL, false);",
            prepare,
        )

        self.assertLess(show, prepare)
        self.assertLess(prepare, hide)
        self.assertIn("keys_dict_check_presence", scene[:show])

    def test_loading_label_restores_timer_priority(self) -> None:
        app = source("applications/main/nfc/nfc_app.c")
        helper = app[app.index("static void nfc_show_loading_view") :]
        self.assertIn("FuriTimerThreadPriorityElevated", helper)
        self.assertIn("FuriTimerThreadPriorityNormal", helper)


if __name__ == "__main__":
    unittest.main()
