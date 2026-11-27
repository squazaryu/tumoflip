"""Run the actual Storage CLI loops with a bounded host input stream."""
from pathlib import Path
import unittest

from tools.tumoflip.native_optional_memory import native
from tools.tumoflip.test_hotplug_assets import function

SOURCE = Path(__file__).resolve().parents[2] / "applications/services/storage/storage_cli.c"

HARNESS = r"""
#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void PipeSide;
typedef char FuriString;
typedef char Storage;
typedef char File;
enum { RECORD_STORAGE, FSAM_WRITE, FSAM_READ, FSOM_OPEN_APPEND, FSOM_OPEN_EXISTING,
       StrintParseNoError, CliKeyETX = 3 };
#define UNUSED(x) (void)(x)
static int keys[520];
static size_t key_count, next_key, setup_calls, cleanup_calls;
static size_t written, read_count, sent;
static uint8_t output[530], input_file[8] = {1,2,3,4,5,6,7,8};
static int bounded_getchar(void) {
    if(next_key == key_count) {
        fputs("Storage CLI requested more input after EOF or Ctrl+C\n", stderr);
        exit(42);
    }
    return keys[next_key++];
}
static int quiet_printf(const char* format, ...) { (void)format; return 0; }
static int send_byte(int byte) {
    assert(sent < sizeof(input_file) && byte == input_file[sent]);
    ++sent;
    return byte;
}
#define getchar bounded_getchar
#define printf quiet_printf
#define putchar send_byte
static Storage* furi_record_open(int id) { assert(id == RECORD_STORAGE); ++setup_calls; return "s"; }
static void furi_record_close(int id) { assert(id == RECORD_STORAGE); ++cleanup_calls; }
static File* storage_file_alloc(Storage* storage) { assert(storage); ++setup_calls; return "f"; }
static void storage_file_free(File* file) { assert(file); ++cleanup_calls; }
static bool storage_file_open(File* f, const char* p, int mode, int open) {
    assert(f && p);
    assert((mode == FSAM_WRITE && open == FSOM_OPEN_APPEND) ||
           (mode == FSAM_READ && open == FSOM_OPEN_EXISTING));
    ++setup_calls;
    return true;
}
static void storage_file_close(File* f) { assert(f); ++cleanup_calls; }
static const char* furi_string_get_cstr(FuriString* s) { return s; }
static int strint_to_uint32(const char* s, void* end, uint32_t* dst, int base) {
    assert(s && !end && base == 10);
    *dst = 3;
    return StrintParseNoError;
}
static int storage_file_get_error(File* f) { assert(f); return 0; }
static void storage_cli_print_error(int error) { (void)error; abort(); }
static void storage_cli_print_usage(void) { abort(); }
static size_t storage_file_write(File* f, const void* data, size_t size) {
    assert(f && size && size <= sizeof(output) - written);
    memcpy(output + written, data, size);
    written += size;
    return size;
}
static uint64_t storage_file_size(File* f) { assert(f); return sizeof(input_file); }
static size_t storage_file_read(File* f, void* data, size_t size) {
    assert(f && size == 3 && read_count < sizeof(input_file));
    size_t left = sizeof(input_file) - read_count;
    if(size > left) size = left;
    memcpy(data, input_file + read_count, size);
    read_count += size;
    return size;
}
"""


def production():
    text = SOURCE.read_text()
    return HARNESS + "\n".join(function(text, signature) for signature in (
        "static void storage_cli_write(",
        "static void storage_cli_read_chunks(",
    ))


class StorageCliDisconnectTests(unittest.TestCase):
    def test_write_closes_on_eof_or_ctrl_c_and_keeps_exact_input(self):
        native(production() + r"""
int main(void) {
    (void)storage_cli_read_chunks;
    const int stops[] = {EOF, CliKeyETX};
    const size_t lengths[] = {0, 1, 511, 512, 513};
    for(size_t reason = 0; reason < sizeof(stops)/sizeof(*stops); ++reason) {
        for(size_t case_index = 0; case_index < sizeof(lengths)/sizeof(*lengths); ++case_index) {
            size_t len = lengths[case_index];
            key_count = len + 1;
            next_key = setup_calls = cleanup_calls = written = 0;
            for(size_t i = 0; i < len; ++i) keys[i] = i % 2 ? 0x41 : 0xff;
            keys[len] = stops[reason];
            storage_cli_write(NULL, "file", "");
            assert(next_key == key_count && written == len);
            for(size_t i = 0; i < len; ++i) assert(output[i] == keys[i]);
            assert(setup_calls == 3 && cleanup_calls == 3);
        }
    }
    return 0;
}
""")

    def test_read_chunks_never_reads_after_disconnect_at_ready(self):
        native(production() + r"""
int main(void) {
    (void)storage_cli_write;
    for(size_t acknowledged = 0; acknowledged <= 3; ++acknowledged) {
        next_key = setup_calls = cleanup_calls = read_count = sent = 0;
        key_count = acknowledged + (acknowledged < 3);
        for(size_t i = 0; i < acknowledged; ++i) keys[i] = '\n';
        if(acknowledged < 3) keys[acknowledged] = EOF;
        storage_cli_read_chunks(NULL, "file", "3");
        size_t expected = acknowledged == 3 ? sizeof(input_file) : 3 * acknowledged;
        assert(next_key == key_count && read_count == expected && sent == expected);
        assert(setup_calls == 3 && cleanup_calls == 3);
    }
    return 0;
}
""")
