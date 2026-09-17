#include "copy_file.h"
#include <string.h>

P2sResult p2s_copy_verified(
    Storage* storage,
    const char* source,
    const char* destination,
    P2sCancelCallback cancel,
    void* context) {
    File* input = storage_file_alloc(storage);
    File* output = storage_file_alloc(storage);
    bool created = false;
    P2sResult result = P2sResultError;
    uint8_t data[512];
    uint8_t check[512];

    do {
        if(cancel && cancel(context)) {
            result = P2sResultCancelled;
            break;
        }
        if(!storage_file_open(input, source, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        const uint64_t size = storage_file_size(input);
        if(!size || size > P2S_MAX_FILE_BYTES) break;
        // Never replace either the original or an existing destination.
        if(!storage_file_open(output, destination, FSAM_WRITE, FSOM_CREATE_NEW)) break;
        created = true;
        uint64_t remaining = size;
        while(remaining) {
            if(cancel && cancel(context)) {
                result = P2sResultCancelled;
                break;
            }
            const size_t chunk = MIN(remaining, sizeof(data));
            if(storage_file_read(input, data, chunk) != chunk) break;
            if(storage_file_write(output, data, chunk) != chunk) break;
            remaining -= chunk;
        }
        if(remaining) break;
        if(!storage_file_sync(output)) break;
        if(!storage_file_close(output)) break;
        if(!storage_file_open(output, destination, FSAM_READ, FSOM_OPEN_EXISTING)) break;
        if(storage_file_size(output) != size || storage_file_size(input) != size) break;
        if(!storage_file_seek(input, 0, true)) break;

        // Reopen and compare all bytes, not just presence or length.
        remaining = size;
        while(remaining) {
            if(cancel && cancel(context)) {
                result = P2sResultCancelled;
                break;
            }
            const size_t chunk = MIN(remaining, sizeof(data));
            if(storage_file_read(input, data, chunk) != chunk ||
               storage_file_read(output, check, chunk) != chunk ||
               memcmp(data, check, chunk) != 0) break;
            remaining -= chunk;
        }
        if(remaining) break;
        result = P2sResultOk;
    } while(false);

    const bool output_closed = storage_file_close(output);
    const bool input_closed = storage_file_close(input);
    storage_file_free(output);
    storage_file_free(input);
    if(result == P2sResultOk && (!output_closed || !input_closed)) result = P2sResultError;
    if(created && result != P2sResultOk) {
        if(storage_common_remove(storage, destination) != FSE_OK) result = P2sResultError;
    }
    return result;
}
