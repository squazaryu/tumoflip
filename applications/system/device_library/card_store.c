#include "card_store.h"
#include <mbedtls/sha256.h>
#include <string.h>
typedef struct {
    char name[32], notes[128], tags[64], checked_at[24];
    char links[LIBRARY_LINKS][256];
    uint32_t link_count;
} LegacyDeviceCard;
typedef struct {
    uint32_t magic, generation;
    LegacyDeviceCard card;
    uint8_t checksum[32];
} LegacyCardEnvelope;
typedef struct {
    uint32_t magic;
    uint32_t generation;
    DeviceCard card;
    uint8_t checksum[32];
} CardEnvelope;
#define CARD_MAGIC 0x32434454U
#define CARD_MAGIC_LEGACY 0x31434454U
_Static_assert(sizeof(LegacyDeviceCard) == 2300, "Deployed card layout changed");
typedef enum { CardAbsent, CardInvalid, CardValid, CardIoError } CardRead;

static CardRead card_read_status(Storage* storage, const char* name, unsigned slot, CardEnvelope* out) {
    FuriString* path = furi_string_alloc_printf(LIBRARY_CARDS "/%s/%u.card", name, slot);
    FileInfo info;
    FS_Error stat = storage_common_stat(storage, furi_string_get_cstr(path), &info);
    CardRead result = stat == FSE_NOT_EXIST ? CardAbsent : CardIoError;
    if(stat == FSE_OK) {
        result = CardInvalid;
        if(info.size == sizeof(CardEnvelope) || info.size == sizeof(LegacyCardEnvelope)) {
            union { CardEnvelope current; LegacyCardEnvelope old; }* data = malloc(sizeof(*data));
            File* file = storage_file_alloc(storage);
            bool ok = storage_file_open(file, furi_string_get_cstr(path), FSAM_READ, FSOM_OPEN_EXISTING);
            if(ok) { ok = storage_file_read(file, data, info.size) == info.size; ok = storage_file_close(file) && ok; }
            storage_file_free(file);
            result = CardIoError;
            if(ok) {
                uint8_t hash[32];
                const size_t count = info.size - sizeof(hash);
                const uint8_t* expected = (const uint8_t*)data + count;
                result = CardInvalid;
                if(!mbedtls_sha256((const uint8_t*)data, count, hash, 0) && !memcmp(hash, expected, 32)) {
                    memset(out, 0, sizeof(*out));
                    if(info.size == sizeof(LegacyCardEnvelope) && data->old.magic == CARD_MAGIC_LEGACY) {
                        memcpy(&out->card, &data->old.card, sizeof(data->old.card));
                        out->generation = data->old.generation;
                        out->magic = CARD_MAGIC;
                    } else if(info.size == sizeof(CardEnvelope) && data->current.magic == CARD_MAGIC) {
                        *out = data->current;
                    }
                    if(out->magic == CARD_MAGIC && out->generation && library_card_valid(&out->card) && !strcmp(name, out->card.name)) result = CardValid;
                }
            }
            free(data);
        }
    }
    furi_string_free(path);
    return result;
}
static bool card_read(Storage* storage, const char* name, unsigned slot, CardEnvelope* e) {
    return card_read_status(storage, name, slot, e) == CardValid;
}
bool library_card_load(Storage* storage, const char* name, DeviceCard* card) {
    if(!library_name_valid(name) || !card) return false;
    CardEnvelope* e = malloc(sizeof(*e));
    uint32_t best = 0;
    bool ok = true;
    for(unsigned i = 0; i < LIBRARY_SLOTS; i++) {
        CardRead status = card_read_status(storage, name, i, e);
        if(status == CardIoError) { ok = false; break; }
        if(status == CardValid && e->generation > best) {
            *card = e->card;
            best = e->generation;
        }
    }
    free(e);
    return ok && best != 0 && storage_sd_status(storage) == FSE_OK;
}
bool library_card_save(Storage* storage, const DeviceCard* card) {
    if(!library_card_valid(card) || storage_sd_status(storage) != FSE_OK || !storage_simply_mkdir(storage, LIBRARY_ROOT) ||
       !storage_simply_mkdir(storage, LIBRARY_CARDS))
        return false;
    FuriString* path = furi_string_alloc_printf(LIBRARY_CARDS "/%s", card->name);
    CardEnvelope* e = calloc(1, sizeof(*e));
    bool ok = false;
    do {
        if(!storage_simply_mkdir(storage, furi_string_get_cstr(path))) break;
        uint32_t gen[4] = {0};
        bool readable = true;
        for(unsigned i = 0; i < 4; i++) {
            CardRead status = card_read_status(storage, card->name, i, e);
            if(status == CardIoError) readable = false;
            if(status == CardValid) gen[i] = e->generation;
        }
        if(!readable) break;
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
        if(!ok && created) storage_common_remove(storage, furi_string_get_cstr(path));
    } while(false);
    free(e);
    furi_string_free(path);
    return ok;
}
