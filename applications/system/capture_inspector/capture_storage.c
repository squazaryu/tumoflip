#include "capture_storage.h"
#include <string.h>
#include <strings.h>

bool ci_capture_path_valid(const char* path) {
    if(!path || strncmp(path, "/ext/", 5)) return false;
    size_t size = strlen(path);
    if(size < 9 || size > 255) return false;
    if(strcasecmp(path + size - 4, ".sub") && strcasecmp(path + size - 4, ".psf")) return false;
    for(const char* p = path; *p; p++) {
        if((unsigned char)*p < 32 || *p == 127) return false;
        if(*p == '/' && p[1] == '.' &&
           (p[2] == '/' || (p[2] == '.' && (p[3] == '/' || p[3] == 0))))
            return false;
    }
    return true;
}

CiLoadStatus ci_capture_load(
    Storage* storage,
    const char* path,
    CiSnapshot* snapshot,
    CiCancelCallback cancel,
    void* context) {
    ci_snapshot_reset(snapshot);
    if(!ci_capture_path_valid(path)) return CiLoadBadPath;
    File* file = storage_file_alloc(storage);
    CiLoadStatus result = CiLoadIoError;
    uint8_t buffer[256];
    do {
        if(cancel && cancel(context)) {
            result = CiLoadCancelled;
            break;
        }
        if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        uint64_t size = storage_file_size(file);
        if(!size || size > CI_FILE_CAP) {
            snapshot->status = CiParseLimit;
            result = CiLoadParseError;
            break;
        }
        uint64_t remaining = size;
        while(remaining) {
            if(cancel && cancel(context)) {
                result = CiLoadCancelled;
                break;
            }
            const size_t chunk = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            if(storage_file_read(file, buffer, chunk) != chunk) break;
            if(ci_snapshot_feed(snapshot, buffer, chunk) != CiParseOk) {
                result = CiLoadParseError;
                break;
            }
            remaining -= chunk;
        }
        if(remaining || storage_file_size(file) != size) break;
        result = ci_snapshot_finish(snapshot) == CiParseOk ? CiLoadOk : CiLoadParseError;
    } while(false);
    if(!storage_file_close(file)) result = CiLoadIoError;
    storage_file_free(file);
    if(result != CiLoadOk) snapshot->finished = false;
    return result;
}
