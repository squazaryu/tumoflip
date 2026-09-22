#include "card_store.h"
#include <mbedtls/sha256.h>
#include <string.h>
typedef struct {
    uint32_t magic;
    uint32_t generation;
    DeviceCard card;
    uint8_t checksum[32];
} CardEnvelope;
#define CARD_MAGIC 0x31434454U
static bool card_read(Storage* storage, const char* name, unsigned slot, CardEnvelope* e) {
    FuriString* path = furi_string_alloc_printf(LIBRARY_CARDS "/%s/%u.card", name, slot);
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING) &&
              storage_file_size(file) == sizeof(*e) &&
              storage_file_read(file, e, sizeof(*e)) == sizeof(*e) && e->magic == CARD_MAGIC &&
              e->generation && library_card_valid(&e->card) && !strcmp(name, e->card.name);
    uint8_t hash[32];
    ok = ok && !mbedtls_sha256((const uint8_t*)e, offsetof(CardEnvelope, checksum), hash, 0) &&
         !memcmp(hash, e->checksum, 32);
    storage_file_free(file);
    furi_string_free(path);
    return ok;
}
bool library_card_load(Storage* storage, const char* name, DeviceCard* card) {
    if(!library_name_valid(name)) return false;
    CardEnvelope* e = malloc(sizeof(*e));
    uint32_t best = 0;
    for(unsigned i = 0; i < LIBRARY_SLOTS; i++)
        if(card_read(storage, name, i, e) && e->generation > best) {
            *card = e->card;
            best = e->generation;
        }
    free(e);
    return best != 0 && storage_sd_status(storage) == FSE_OK;
}
bool library_card_save(Storage* storage, const DeviceCard* card) {
    if(!library_card_valid(card) || !storage_simply_mkdir(storage, LIBRARY_ROOT) ||
       !storage_simply_mkdir(storage, LIBRARY_CARDS))
        return false;
    FuriString* path = furi_string_alloc_printf(LIBRARY_CARDS "/%s", card->name);
    CardEnvelope* e = calloc(1, sizeof(*e));
    bool ok = false;
    do {
        if(!storage_simply_mkdir(storage, furi_string_get_cstr(path))) break;
        uint32_t gen[4] = {0};
        for(unsigned i = 0; i < 4; i++)
            if(card_read(storage, card->name, i, e)) gen[i] = e->generation;
        int newest = library_newest_slot(gen);
        if(newest >= 0 && gen[newest] == UINT32_MAX) break;
        unsigned slot = library_write_slot(gen);
        memset(e, 0, sizeof(*e));
        e->magic = CARD_MAGIC;
        e->generation = newest < 0 ? 1 : gen[newest] + 1;
        e->card = *card;
        if(mbedtls_sha256((const uint8_t*)e, offsetof(CardEnvelope, checksum), e->checksum, 0))
            break;
        furi_string_printf(path, LIBRARY_CARDS "/%s/%u.card", card->name, slot);
        FS_Error removed = storage_common_remove(storage, furi_string_get_cstr(path));
        if(removed != FSE_OK && removed != FSE_NOT_EXIST) break;
        File* file = storage_file_alloc(storage);
        bool created =
            storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_NEW);
        ok = created && storage_file_write(file, e, sizeof(*e)) == sizeof(*e) &&
             storage_file_sync(file);
        if(created) ok = storage_file_close(file) && ok;
        storage_file_free(file);
        if(ok) ok = card_read(storage, card->name, slot, e);
    } while(false);
    free(e);
    furi_string_free(path);
    return ok;
}
