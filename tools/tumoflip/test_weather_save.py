"""Fault injection against the production Weather Editor RAW exporter."""

from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]


class WeatherSaveTests(unittest.TestCase):
    def test_export_preserves_existing_files_and_reports_close_failure(self):
        source = (ROOT / "applications_user/weather_editor/weather_editor_engine.c").read_text()
        start = source.index("bool weather_editor_save_raw_sub(")
        end = source.index("\nstatic const char*", start)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define EXT_PATH(x) x
#define WEATHER_EDITOR_SUB_FOLDER "subghz/weather"
#define RECORD_STORAGE 1
typedef int Storage;
typedef int FlipperFormat;
typedef int FuriString;
typedef struct {FuriString* name; uint32_t frequency; uint8_t* data; size_t data_size;} SubGhzRadioPreset;
typedef struct {int32_t* values; size_t count;} WeatherEditorRaw;
static int object, writes, fail_write, removed, closes;
static bool exists, close_ok;
static void furi_string_reset(FuriString* s) {(void)s;}
static void furi_string_set(FuriString* s,const char* v) {(void)s;(void)v;}
static size_t furi_string_size(FuriString* s) {(void)s;return 0;}
static const char* furi_string_get_cstr(FuriString* s) {(void)s;return "AM650";}
static const char* weather_editor_sub_preset_name(const char* s) {return s;}
static Storage* furi_record_open(int r) {(void)r;return &object;}
static void furi_record_close(int r) {(void)r;}
static void storage_common_mkdir(Storage* s,const char* p) {(void)s;(void)p;}
static __attribute__((unused)) int storage_common_remove(Storage* s,const char* p) {(void)s;(void)p;removed++;return 0;}
static FlipperFormat* flipper_format_file_alloc(Storage* s) {(void)s;return &object;}
static void flipper_format_free(FlipperFormat* f) {(void)f;}
static bool flipper_format_file_open_new(FlipperFormat* f,const char* p) {(void)f;(void)p;return !exists;}
static bool flipper_format_file_close(FlipperFormat* f) {(void)f;closes++;return close_ok;}
static bool write_ok(void) {return ++writes != fail_write;}
#define flipper_format_write_header_cstr(...) write_ok()
#define flipper_format_write_uint32(...) write_ok()
#define flipper_format_write_string_cstr(...) write_ok()
#define flipper_format_write_hex(...) write_ok()
#define flipper_format_write_int32(...) write_ok()
''' + source[start:end] + r'''
int main(void) {
    int32_t samples[600]={1};
    WeatherEditorRaw raw={samples,600};
    SubGhzRadioPreset preset={&object,433920000,NULL,0};
    for(int mode=0;mode<9;mode++) {
        writes=removed=closes=0;
        exists=mode==1; close_ok=mode!=2;
        fail_write=mode>=3 ? mode-2 : 0;
        bool ok=weather_editor_save_raw_sub("capture.sub",&preset,&raw,NULL);
        assert(ok==(mode==0));
        assert(removed==((mode>=2)?1:0));
        assert(closes==(exists?0:1));
    }
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
