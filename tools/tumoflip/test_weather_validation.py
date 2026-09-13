"""Bounds for untrusted Weather Editor profiles and CC1101 preset blobs."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]


class WeatherValidationTests(unittest.TestCase):
    def test_profile_bounds_and_truncated_preset(self):
        header = ROOT / "applications_user/weather_editor/weather_editor_validation.h"
        run_c(f'#include "{header}"\n' + r'''
#include <assert.h>
int main(void) {
    assert(weather_editor_bits_valid("TX141THBv2", 41, 0, 0));
    assert(!weather_editor_bits_valid("TX141THBv2", 72, 0, 0));
    assert(weather_editor_bits_valid("TX8300", 72, 0, 0));
    assert(weather_editor_bits_valid("EMOS E601x", 24, 0, 120));
    assert(!weather_editor_bits_valid("EMOS E601x", 24, 0, 256));
    assert(!weather_editor_bits_valid("Oregon2", 32, 256, 0));
    assert(!weather_editor_bits_valid("TX8300", 328, 0, 0));
    uint8_t preset[]={2,0x0D,0,0,0,0,0,0,0,0,0,0};
    for(size_t n=0;n<sizeof(preset);n++) assert(!weather_editor_preset_valid(preset,n));
    assert(weather_editor_preset_valid(preset,sizeof(preset)));
    preset[2]=3;
    assert(!weather_editor_preset_valid(preset,4));
    preset[0]=0xFF;
    assert(!weather_editor_preset_valid(preset,sizeof(preset)));
    assert(!weather_editor_preset_valid(NULL,12));
    return 0;
}
''')

    def test_normal_exit_does_not_request_lab_file_deletion(self):
        source = (ROOT / "applications_user/weather_editor/weather_station_app.c").read_text()
        free = source.split("void weather_station_app_free(", 1)[1]
        self.assertNotIn("weather_lab_release(app, true)", free)


if __name__ == "__main__":
    unittest.main()
