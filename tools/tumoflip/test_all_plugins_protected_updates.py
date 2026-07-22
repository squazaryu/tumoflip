import pathlib
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]


class ProtectedAppUpdateTests(unittest.TestCase):
    def test_quac_releases_popup(self):
        source = (REPO_ROOT / "applications_user/quac/quac.c").read_text()
        self.assertIn("popup_free(app->popup);", source)

    def test_xremote_releases_transient_contexts(self):
        edit = (REPO_ROOT / "applications_user/flipper_xremote/xremote_edit.c").read_text()
        settings = (
            REPO_ROOT / "applications_user/flipper_xremote/xremote_settings.c"
        ).read_text()
        self.assertIn("variable_item_list_free(ctx->item_list);\n    free(ctx);", edit)
        self.assertIn("variable_item_list_free(ctx->item_list);\n    free(ctx);", settings)

    def test_marauder_matches_module_one_1_13_cli(self):
        manifest = (
            REPO_ROOT / "applications_user/esp32_wifi_marauder/application.fam"
        ).read_text()
        header = (
            REPO_ROOT / "applications_user/esp32_wifi_marauder/wifi_marauder_app.h"
        ).read_text()
        internal = (
            REPO_ROOT / "applications_user/esp32_wifi_marauder/wifi_marauder_app_i.h"
        ).read_text()
        menu = (
            REPO_ROOT
            / "applications_user/esp32_wifi_marauder/scenes/wifi_marauder_scene_start.c"
        ).read_text()

        self.assertIn("fap_version=(7, 9)", manifest)
        self.assertIn('WIFI_MARAUDER_APP_VERSION "v0.7.9"', header)
        self.assertIn("#define NUM_MENU_ITEMS (34)", internal)
        self.assertIn('"gpstracker -c start"', menu)
        self.assertIn('"gpstracker -c stop"', menu)
        self.assertIn('{"NMEA Stream", {""}, 1, {"nmea"}', menu)
        self.assertNotIn('"wardrive -s"', menu)
        self.assertNotIn('"wardrive -f"', menu)
        self.assertNotIn('"list -b"', menu)
        self.assertNotIn('"upload -d ', menu)
        self.assertNotIn('"foxhunt -', menu)


if __name__ == "__main__":
    unittest.main()
