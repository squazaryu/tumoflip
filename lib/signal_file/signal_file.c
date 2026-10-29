#include "signal_file.h"
#include <furi.h>
#include <limits.h>
#include <string.h>

#define TAG "SignalSave"

static bool signal_file_copy_existing(
    Storage* storage,
    const char* source,
    const char* destination,
    FS_OpenMode mode,
    bool* created) {
    File* input = storage_file_alloc(storage);
    File* output = storage_file_alloc(storage);
    bool result = false;
    *created = false;
    if(storage_file_open(input, source, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t size = storage_file_size(input);
        if(storage_file_get_error(input) == FSE_OK && size <= SIZE_MAX &&
           storage_file_open(output, destination, FSAM_WRITE, mode)) {
            *created = true;
            result = storage_file_copy_to_file(input, output, (size_t)size) &&
                     storage_file_truncate(output) && storage_file_sync(output);
        }
    }
    bool output_closed = storage_file_close(output);
    bool input_closed = storage_file_close(input);
    storage_file_free(output);
    storage_file_free(input);
    return result && output_closed && input_closed;
}

static bool signal_file_contents_equal(Storage* storage, const char* first, const char* second) {
    File* a = storage_file_alloc(storage);
    File* b = storage_file_alloc(storage);
    bool result = false;
    if(storage_file_open(a, first, FSAM_READ, FSOM_OPEN_EXISTING) &&
       storage_file_open(b, second, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint64_t remaining = storage_file_size(a);
        uint64_t second_size = storage_file_size(b);
        result = storage_file_get_error(a) == FSE_OK && storage_file_get_error(b) == FSE_OK &&
                 remaining == second_size;
        uint8_t left[128], right[128];
        while(result && remaining) {
            size_t count = MIN(remaining, sizeof(left));
            result = storage_file_read(a, left, count) == count &&
                     storage_file_read(b, right, count) == count && !memcmp(left, right, count);
            remaining -= count;
        }
    }
    bool a_closed = storage_file_close(a);
    bool b_closed = storage_file_close(b);
    storage_file_free(a);
    storage_file_free(b);
    return result && a_closed && b_closed;
}

static bool signal_file_write_stream(Storage* storage, Stream* source, const char* path) {
    File* file = storage_file_alloc(storage);
    bool result = false;
    size_t size = stream_size(source);
    if(stream_seek(source, 0, StreamOffsetFromStart) &&
       storage_file_open(file, path, FSAM_WRITE, FSOM_OPEN_EXISTING)) {
        uint8_t data[128];
        size_t remaining = size;
        result = true;
        while(result && remaining) {
            size_t count = MIN(remaining, sizeof(data));
            result = stream_read(source, data, count) == count &&
                     storage_file_write(file, data, count) == count;
            remaining -= count;
        }
        result = result && storage_file_truncate(file) && storage_file_sync(file);
    }
    bool closed = storage_file_close(file);
    result = result && closed;
    // Read the closed file back before reporting success.
    if(result && stream_seek(source, 0, StreamOffsetFromStart) &&
       storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t expected[128], actual[128];
        result = storage_file_size(file) == size && storage_file_get_error(file) == FSE_OK;
        size_t remaining = size;
        while(result && remaining) {
            size_t count = MIN(remaining, sizeof(actual));
            result = stream_read(source, expected, count) == count &&
                     storage_file_read(file, actual, count) == count &&
                     !memcmp(expected, actual, count);
            remaining -= count;
        }
    } else {
        result = false;
    }
    closed = storage_file_close(file);
    storage_file_free(file);
    return result && closed;
}

bool signal_file_replace(Storage* storage, const char* source, const char* path) {
    furi_assert(storage);
    furi_assert(source);
    furi_assert(path);
    if(strcmp(source, path) == 0) return false;
    bool created = false;
    const FS_Error status = storage_common_stat(storage, path, NULL);
    if(status == FSE_NOT_EXIST) {
        bool ok = signal_file_copy_existing(storage, source, path, FSOM_CREATE_NEW, &created) &&
                  signal_file_contents_equal(storage, source, path);
        if(!ok && created) storage_simply_remove(storage, path);
        return ok;
    }
    if(status != FSE_OK) return false;
    FuriString* backup = furi_string_alloc_printf("%s.tumo-bak", path);
    const char* backup_path = furi_string_get_cstr(backup);
    bool ok = false;
    if(signal_file_copy_existing(storage, path, backup_path, FSOM_CREATE_NEW, &created) &&
       signal_file_contents_equal(storage, path, backup_path)) {
        bool opened = false;
        ok = signal_file_copy_existing(storage, source, path, FSOM_OPEN_EXISTING, &opened) &&
             signal_file_contents_equal(storage, source, path);
        if(ok) {
            storage_simply_remove(storage, backup_path);
        } else {
            // Keep the verified original even when a second error prevents rollback.
            signal_file_copy_existing(storage, backup_path, path, FSOM_OPEN_EXISTING, &opened);
        }
    } else if(created) {
        storage_simply_remove(storage, backup_path);
    }
    furi_string_free(backup);
    return ok;
}

bool signal_file_update(Storage* storage, Stream* source, const char* path) {
    furi_assert(storage);
    furi_assert(source);
    furi_assert(path);
    size_t position = stream_tell(source);
    if(position > INT32_MAX || stream_size(source) == 0 || stream_size(source) > INT32_MAX)
        return false;
    FuriString* backup = furi_string_alloc_printf("%s.tumo-bak", path);
    const char* backup_path = furi_string_get_cstr(backup);
    bool created = false;
    bool result = false;
    if(signal_file_copy_existing(storage, path, backup_path, FSOM_CREATE_NEW, &created) &&
       signal_file_contents_equal(storage, path, backup_path)) {
        result = signal_file_write_stream(storage, source, path);
        bool position_restored = stream_seek(source, position, StreamOffsetFromStart);
        result = result && position_restored;
        if(result) {
            if(!storage_simply_remove(storage, backup_path)) {
                FURI_LOG_W(TAG, "Updated signal; backup retained: %s", backup_path);
            }
        } else {
            bool opened = false;
            bool restored =
                signal_file_copy_existing(storage, backup_path, path, FSOM_OPEN_EXISTING, &opened);
            FURI_LOG_E(
                TAG,
                "Save failed; rollback %s; backup: %s",
                restored ? "OK" : "failed",
                backup_path);
            // Retain the verified original backup on every failed update.
        }
    } else if(created) {
        // This backup is ours but unverified; the original was never modified.
        storage_simply_remove(storage, backup_path);
    }
    if(!result) stream_seek(source, position, StreamOffsetFromStart);
    furi_string_free(backup);
    return result;
}
