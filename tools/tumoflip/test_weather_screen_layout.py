"""Guard reachable Weather Editor labels against Flipper 128px clipping."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


class WeatherScreenLayoutTests(unittest.TestCase):
    def test_submenu_headers_fit_primary_font_width(self):
        simulation = (ROOT / "applications_user/weather_editor/scenes/weather_station_scene_simulation.c").read_text()
        saved = (ROOT / "applications_user/weather_editor/scenes/weather_station_scene_saved_profiles.c").read_text()
        self.assertIn('submenu_set_header(app->submenu, "Simulation");', simulation)
        self.assertNotIn("Simulation - protocol", simulation)
        self.assertIn('snprintf(empty_header, sizeof(empty_header), "%s empty", source_label);', saved)
        self.assertNotIn("no saved files", saved)

    def test_variable_item_labels_stay_within_the_left_column(self):
        source = (ROOT / "applications_user/weather_editor/scenes/weather_station_scene_actions.c").read_text()
        labels = re.findall(r'(?:variable_item_list_add|weather_editor_add_readonly_item)\(\s*\n?\s*list,\s*"([^"]+)"', source)
        self.assertTrue(labels)
        self.assertLessEqual(max(map(len, labels)), 16)


if __name__ == "__main__":
    unittest.main()
