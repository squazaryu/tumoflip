#include "converter.h"
#include "copy_file.h"
#include "paths.h"

#include <flipper_format/flipper_format.h>
#include <flipper_format/flipper_format_i.h>
#include <toolbox/stream/file_stream.h>
#include <toolbox/path.h>

// Tumoflip shares ProtoPirate decoders with Standard. Both use the same key-file
// format. Preserve protocol names and ALL data, including unknown metadata,
// comments, signed values and repeated lines.
static P2sResult p2s_validate_capture(Storage* storage, const char* path) {
    FileInfo info;
    if(storage_common_stat(storage, path, &info) != FSE_OK) return P2sResultError;
    if(file_info_is_dir(&info) || !info.size || info.size > P2S_MAX_FILE_BYTES)
        return P2sResultSkipped;

    FlipperFormat* file = flipper_format_file_alloc(storage);
    FuriString* value = furi_string_alloc();
    Stream* stream = flipper_format_get_raw_stream(file);
    P2sResult result = P2sResultError;
    do {
        if(!file_stream_open(stream, path, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        result = P2sResultSkipped;
        uint32_t version = 0;
        uint32_t frequency = 0;
        if(!flipper_format_read_header(file, value, &version) ||
           !furi_string_equal_str(value, P2S_FILETYPE) || version != P2S_FILE_VERSION) break;
        if(!flipper_format_rewind(file) ||
           !flipper_format_read_uint32(file, "Frequency", &frequency, 1) || !frequency) break;
        if(!flipper_format_rewind(file) || !flipper_format_read_string(file, "Preset", value) ||
           !furi_string_start_with_str(value, "FuriHalSubGhzPreset")) break;
        if(!flipper_format_rewind(file) || !flipper_format_read_string(file, "Protocol", value) ||
           furi_string_empty(value)) break;
        if(!flipper_format_rewind(file) || !flipper_format_key_exist(file, "Key")) break;
        result = P2sResultOk;
    } while(false);
    if(file_stream_get_error(stream) != FSE_OK) result = P2sResultError;
    if(!flipper_format_file_close(file)) result = P2sResultError;
    flipper_format_free(file);
    furi_string_free(value);
    return result;
}

static P2sResult p2s_convert(
    const char* path,
    const char* directory,
    const char* extension,
    P2sCancelCallback cancel,
    void* context) {
    if(cancel && cancel(context)) return P2sResultCancelled;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    P2sResult result = p2s_validate_capture(storage, path);
    if(result == P2sResultOk) {
        FuriString* source = furi_string_alloc_set(path);
        FuriString* base = furi_string_alloc();
        FuriString* name = furi_string_alloc();
        FuriString* destination = furi_string_alloc();
        result = P2sResultError;
        if(storage_simply_mkdir(storage, directory)) {
            path_extract_filename(source, base, true);
            storage_get_next_filename(
                storage, directory, furi_string_get_cstr(base), extension, name, 120);
            furi_string_printf(
                destination, "%s/%s%s", directory, furi_string_get_cstr(name), extension);
            result = p2s_copy_verified(
                storage, path, furi_string_get_cstr(destination), cancel, context);
        }
        furi_string_free(destination);
        furi_string_free(name);
        furi_string_free(base);
        furi_string_free(source);
    }
    furi_record_close(RECORD_STORAGE);
    return result;
}

P2sResult p2s_convert_psf_to_sub(const char* path, P2sCancelCallback cancel, void* context) {
    return p2s_convert(path, IMPORTED_DIR, SUB_EXTENSION, cancel, context);
}

P2sResult p2s_convert_sub_to_psf(const char* path, P2sCancelCallback cancel, void* context) {
    return p2s_convert(path, PP_SAVED_DIR, PP_EXTENSION, cancel, context);
}
