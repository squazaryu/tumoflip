"""Fault injection against the production Weather Editor RAW exporter."""

from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]


class WeatherSaveTests(unittest.TestCase):
    def test_ws_writer_reports_short_write_and_close_failure(self):
        source = (ROOT / "applications_user/weather_editor/weather_editor_engine.c").read_text()
        start = source.index("bool weather_editor_save_key_sub(")
        start = source.index("    bool ok = false;", start)
        end = source.index("\nbool weather_editor_save_profile(", start)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FSAM_WRITE 1
#define FSOM_CREATE_NEW 2
#define RECORD_STORAGE 1
static bool exists,close_ok,short_write;
static int removed,closed;
static bool file_stream_open(int s,const char* p,int a,int m) {(void)s;(void)p;(void)a;assert(m==2);return !exists;}
static size_t furi_string_size(int s) {(void)s;return 10;}
static const char* furi_string_get_cstr(int s) {(void)s;return "1234567890";}
static size_t stream_write(int s,const uint8_t* b,size_t n) {(void)s;(void)b;return short_write?n-1:n;}
static bool file_stream_close(int s) {(void)s;closed++;return close_ok;}
static void stream_free(int s) {(void)s;}
static void furi_string_free(int s) {(void)s;}
static void furi_string_set(int s,const char* v) {(void)s;(void)v;}
static void furi_record_close(int s) {(void)s;}
static __attribute__((unused)) int storage_common_remove(int s,const char* p) {(void)s;(void)p;removed++;return 0;}
static bool save(void) {
    int stream=1,text=2,status=0;
    int storage __attribute__((unused))=3;
    const char* path="file.ws";
''' + source[start:end] + r'''
int main(void) {
    for(int mode=0;mode<4;mode++) {
        exists=mode==1; close_ok=mode!=2; short_write=mode==3;
        removed=closed=0;
        assert(save()==(mode==0));
        assert(removed==(mode>=2?1:0));
        assert(closed==(exists?0:1));
    }
    return 0;
}
''')

    def test_export_preserves_existing_files_and_reports_close_failure(self):
        self.check_save("weather_editor_save_raw_sub", "\nstatic const char*", 9)

    def test_profile_preserves_existing_files_and_reports_close_failure(self):
        self.check_save("weather_editor_save_profile", "\n\nstatic uint64_t", 23)

    def check_save(self, function, end_marker, cases):
        source = (ROOT / "applications_user/weather_editor/weather_editor_engine.c").read_text()
        start = source.index("bool " + function + "(")
        end = source.index(end_marker, start)
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define EXT_PATH(x) x
#define WEATHER_EDITOR_SUB_FOLDER "subghz/weather"
#define WEATHER_EDITOR_PROFILE_FOLDER "profiles"
#define WEATHER_EDITOR_RX_PROFILE_FOLDER "rx"
#define WEATHER_EDITOR_EDITED_PROFILE_FOLDER "edited"
#define RECORD_STORAGE 1
typedef int Storage;
typedef int FlipperFormat;
typedef int FuriString;
typedef struct {FuriString* name; uint32_t frequency; uint8_t* data; size_t data_size;} SubGhzRadioPreset;
typedef struct {int32_t* values; size_t count;} WeatherEditorRaw;
typedef struct {
    FuriString* protocol_name;
    uint32_t id,bit_count,channel,battery_kind,button,var_bits,frame_bits;
    int32_t temperature_tenths,humidity,battery;
    bool has_temperature,has_humidity,has_channel,encoder_available,has_button;
    uint64_t original_data,edited_data,var_data,frame_upper,frame_lower;
} WeatherEditorState;
static int object, writes, fail_write, removed, closes;
static bool exists, close_ok;
static void furi_string_reset(FuriString* s) {(void)s;}
static void furi_string_set(FuriString* s,const char* v) {(void)s;(void)v;}
static size_t furi_string_size(FuriString* s) {(void)s;return 0;}
static __attribute__((unused)) const char* furi_string_get_cstr(FuriString* s) {(void)s;return "AM650";}
static __attribute__((unused)) const char* weather_editor_sub_preset_name(const char* s) {return s;}
static __attribute__((unused)) void weather_editor_u64_to_bytes(uint64_t value,uint8_t* out) {(void)value;(void)out;}
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
static bool flipper_format_write_uint32(FlipperFormat* f,const char* k,const uint32_t* v,size_t n) {
    (void)f;(void)k;(void)v;(void)n;return write_ok();
}
#define flipper_format_write_string_cstr(...) write_ok()
static __attribute__((unused)) bool flipper_format_write_hex(FlipperFormat* f,const char* k,const uint8_t* v,size_t n) {
    (void)f;(void)k;(void)v;(void)n;return write_ok();
}
static bool flipper_format_write_int32(FlipperFormat* f,const char* k,const int32_t* v,size_t n) {
    (void)f;(void)k;(void)v;(void)n;return write_ok();
}
''' + source[start:end] + r'''
int main(void) {
    INPUT_DECLARATION
    SubGhzRadioPreset preset={&object,433920000,NULL,0};
    for(int mode=0;mode<CASES;mode++) {
        writes=removed=closes=0;
        exists=mode==1; close_ok=mode!=2;
        fail_write=mode>=3 ? mode-2 : 0;
        bool ok=FUNCTION("capture.sub",&preset,&input,NULL);
        assert(ok==(mode==0));
        assert(removed==((mode>=2)?1:0));
        assert(closes==(exists?0:1));
    }
    return 0;
}
'''.replace("FUNCTION", function).replace("CASES", str(cases)).replace(
            "INPUT_DECLARATION",
            "int32_t samples[600]={1}; WeatherEditorRaw input={samples,600};"
            if function.endswith("raw_sub") else
            "WeatherEditorState input={.protocol_name=&object};"))


if __name__ == "__main__":
    unittest.main()
