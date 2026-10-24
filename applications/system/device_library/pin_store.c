#include "card_store.h"
#include <mbedtls/sha256.h>
#include <string.h>

typedef struct {
    uint32_t magic, generation;
    LibraryPins pins;
    uint8_t checksum[32];
} PinEnvelope;
#define PIN_MAGIC 0x31504C54U
typedef enum { PinAbsent, PinInvalid, PinValid, PinIoError } PinRead;

static PinRead pins_read(Storage* storage, unsigned slot, PinEnvelope* e) {
    FuriString* path = furi_string_alloc_printf(LIBRARY_PIN_DIR "/%u.pins", slot);
    FileInfo info;
    FS_Error stat = storage_common_stat(storage, furi_string_get_cstr(path), &info);
    PinRead result = stat == FSE_NOT_EXIST ? PinAbsent : PinIoError;
    if(stat == FSE_OK) {
        result = PinInvalid;
        if(info.size == sizeof(*e)) {
            File* file = storage_file_alloc(storage);
            bool ok = storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING);
            if(ok) { ok = storage_file_read(file, e, sizeof(*e)) == sizeof(*e); ok = storage_file_close(file) && ok; }
            storage_file_free(file);
            result = PinIoError;
            if(ok) {
                uint8_t hash[32];
                result = e->magic == PIN_MAGIC && e->generation && library_pins_valid(&e->pins) &&
                         !mbedtls_sha256((const uint8_t*)e, offsetof(PinEnvelope, checksum), hash, 0) &&
                         !memcmp(hash, e->checksum, sizeof(hash)) ? PinValid : PinInvalid;
            }
        }
    }
    furi_string_free(path);
    return result;
}

bool library_pins_load(Storage* storage, LibraryPins* pins) {
    if(!pins || storage_sd_status(storage) != FSE_OK) return false;
    PinEnvelope* e = malloc(sizeof(*e));
    uint32_t best = 0;
    bool seen = false, ok = true;
    memset(pins, 0, sizeof(*pins));
    for(unsigned i = 0; i < LIBRARY_SLOTS; i++) {
        PinRead status = pins_read(storage, i, e);
        if(status == PinIoError) { ok = false; break; }
        if(status != PinAbsent) seen = true;
        if(status == PinValid && e->generation > best) { best = e->generation; *pins = e->pins; }
    }
    free(e);
    return ok && (!seen || best) && storage_sd_status(storage) == FSE_OK;
}

bool library_pins_save(Storage* storage, const LibraryPins* pins) {
    if(!library_pins_valid(pins) || storage_sd_status(storage) != FSE_OK ||
       !storage_simply_mkdir(storage, LIBRARY_ROOT) || !storage_simply_mkdir(storage, LIBRARY_PIN_DIR)) return false;
    PinEnvelope* e = calloc(1, sizeof(*e));
    uint32_t gen[LIBRARY_SLOTS] = {0};
    bool ok = false, readable = true, seen = false;
    for(unsigned i = 0; i < LIBRARY_SLOTS; i++) {
        PinRead status = pins_read(storage, i, e);
        if(status == PinIoError) readable = false;
        if(status != PinAbsent) seen = true;
        if(status == PinValid) gen[i] = e->generation;
    }
    int newest = library_newest_slot(gen);
    if(readable && (!seen || newest >= 0) && (newest < 0 || gen[newest] != UINT32_MAX)) {
        unsigned slot = library_write_slot(gen);
        memset(e, 0, sizeof(*e));
        e->magic = PIN_MAGIC;
        e->generation = newest < 0 ? 1 : gen[newest] + 1;
        e->pins = *pins;
        if(!mbedtls_sha256((const uint8_t*)e, offsetof(PinEnvelope, checksum), e->checksum, 0)) {
            FuriString* path = furi_string_alloc_printf(LIBRARY_PIN_DIR "/%u.pins", slot);
            FS_Error removed = storage_common_remove(storage, furi_string_get_cstr(path));
            if(removed == FSE_OK || removed == FSE_NOT_EXIST) {
                File* file = storage_file_alloc(storage);
                bool created = storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_NEW);
                ok = created && storage_file_write(file, e, sizeof(*e)) == sizeof(*e) && storage_file_sync(file);
                if(created) ok = storage_file_close(file) && ok;
                storage_file_free(file);
                if(ok) ok = pins_read(storage, slot, e) == PinValid;
                if(!ok && created) storage_common_remove(storage, furi_string_get_cstr(path));
            }
            furi_string_free(path);
        }
    }
    free(e);
    return ok;
}
