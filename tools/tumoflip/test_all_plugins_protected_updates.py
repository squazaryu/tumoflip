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

    def test_marauder_0_7_10_exposes_only_the_selected_cli_delta(self):
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
        text_input = (
            REPO_ROOT
            / "applications_user/esp32_wifi_marauder/scenes/wifi_marauder_scene_text_input.c"
        ).read_text()
        console = (
            REPO_ROOT
            / "applications_user/esp32_wifi_marauder/scenes/wifi_marauder_scene_console_output.c"
        ).read_text()

        self.assertIn("fap_version=(7, 10)", manifest)
        self.assertIn('WIFI_MARAUDER_APP_VERSION "v0.7.10"', header)
        self.assertIn("#define NUM_MENU_ITEMS (34)", internal)
        self.assertIn('"Airtag"', menu)
        self.assertIn('{"spoof", "sound"}', menu)
        self.assertIn('{"spoofat -t", "findmy -t"}', menu)
        self.assertIn('"gps -g accuracy"', menu)
        self.assertIn('"gps -g text"', menu)
        self.assertIn('"gps -g nmea"', menu)

        gps_start = menu.index('{"GPS Data"')
        gps_end = menu.index('{"GPS Tracker"', gps_start)
        self.assertIn("SHOW_STOPSCAN_TIP", menu[gps_start:gps_end])
        self.assertIn('"Press BACK to send stopscan\\n"', console)
        self.assertIn('"stopscan\\n"', console)

        self.assertIn('"Enter Airtag device index"', text_input)
        self.assertIn('"Enter FindMy device index"', text_input)
        self.assertIn("validator_is_device_index_callback", text_input)

        self.assertIn('"gpstracker -c start"', menu)
        self.assertIn('"gpstracker -c stop"', menu)
        self.assertIn('{"NMEA Stream", {""}, 1, {"nmea"}', menu)
        self.assertIn('{"Signal Monitor", {""}, 1, {"sigmon"}', menu)
        self.assertNotIn('"wardrive -s"', menu)
        self.assertNotIn('"wardrive -f"', menu)
        self.assertNotIn('"list -b"', menu)
        self.assertNotIn('"upload -d ', menu)
        self.assertNotIn('"foxhunt -', menu)


if __name__ == "__main__":
    unittest.main()
