#include "field_bundle.h"
#include <stdio.h>
#include <string.h>

#define FIELD_BUNDLE_MAGIC 0x31424F54UL
#define FIELD_BUNDLE_MAX (32UL * 1024UL * 1024UL)
typedef struct { uint32_t size; uint8_t hash[32]; } FieldBundleFile;
typedef struct {
    uint32_t magic, version, header_size;
    DeviceCard card;
    FieldBundleFile files[LIBRARY_LINKS];
    uint8_t hash[32];
} FieldBundleHeader;
_Static_assert(offsetof(FieldBundleHeader, hash) + 32 == sizeof(FieldBundleHeader), "Bundle wire padding");
_Static_assert(sizeof(DeviceCard) == 2556, "Portable card wire layout changed");

static const char* field_bundle_extension(LibraryFileKind kind) {
    switch(kind) {
    case LibraryFileSub: return ".sub";
    case LibraryFileIr: return ".ir";
    case LibraryFileNfc: return ".nfc";
    case LibraryFileRfid: return ".rfid";
    case LibraryFileIbutton: return ".ibtn";
    default: return NULL;
    }
}
static bool field_bundle_card_valid(const DeviceCard* card) {
    if(!library_card_valid(card)) return false;
    for(unsigned i = 0; i < card->link_count; i++)
        if(!field_bundle_extension(library_file_kind(card->links[i]))) return false;
    return true;
}
static bool field_bundle_header_read(File* file, FieldBundleHeader* header) {
    uint8_t hash[32];
    if(storage_file_read(file, header, sizeof(*header)) != sizeof(*header) ||
       header->magic != FIELD_BUNDLE_MAGIC || header->version != 1 ||
       header->header_size != sizeof(*header) || !field_bundle_card_valid(&header->card) ||
       mbedtls_sha256((const uint8_t*)header, offsetof(FieldBundleHeader, hash), hash, 0) ||
       memcmp(hash, header->hash, 32)) return false;
    uint64_t total = sizeof(*header);
    for(unsigned i = 0; i < header->card.link_count; i++) {
        if(header->files[i].size > FIELD_FILE_MAX) return false;
        total += header->files[i].size;
    }
    return total <= FIELD_BUNDLE_MAX && storage_file_size(file) == total;
}
static bool field_bundle_payload_read(File* file, const FieldBundleFile* entry,
                                       const char* path, FieldProgress progress, void* context) {
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    uint8_t buffer[512], hash[32];
    bool ok = !mbedtls_sha256_starts(&sha, 0) && (!progress || progress(context, path, 0));
    for(uint32_t offset = 0; ok && offset < entry->size;) {
        size_t count = entry->size - offset < sizeof(buffer) ? entry->size - offset : sizeof(buffer);
        ok = (!progress || progress(context, path, offset)) &&
             storage_file_read(file, buffer, count) == count &&
             !mbedtls_sha256_update(&sha, buffer, count);
        offset += count;
    }
    if(ok) ok = !mbedtls_sha256_finish(&sha, hash) && !memcmp(hash, entry->hash, 32);
    mbedtls_sha256_free(&sha);
    return ok;
}
bool field_bundle_inspect(Storage* storage, const char* path, DeviceCard* card,
                           FieldProgress progress, void* context) {
    if(!storage || !library_path_valid(path) || !card) return false;
    FieldBundleHeader* header = malloc(sizeof(*header));
    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    bool ok = opened && field_bundle_header_read(file, header) &&
              (!progress || progress(context, path, 0));
    for(unsigned i = 0; ok && i < header->card.link_count; i++)
        ok = field_bundle_payload_read(file, &header->files[i], header->card.links[i], progress, context);
    if(opened) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    if(ok) *card = header->card;
    free(header);
    return ok;
}
bool field_bundle_export(Storage* storage, const DeviceCard* card, const char* path,
                          FieldProgress progress, void* context) {
    if(!storage || !field_bundle_card_valid(card) || !library_path_valid(path) ||
       (progress && !progress(context, path, 0))) return false;
    FieldBundleHeader* header = calloc(1, sizeof(*header));
    header->magic = FIELD_BUNDLE_MAGIC;
    header->version = 1;
    header->header_size = sizeof(*header);
    header->card = *card;
    uint64_t total = sizeof(*header);
    bool ok = true;
    for(unsigned i = 0; ok && i < card->link_count; i++) {
        ok = field_file_hash(storage, card->links[i], &header->files[i].size,
                             header->files[i].hash, progress, context);
        total += header->files[i].size;
        if(total > FIELD_BUNDLE_MAX) ok = false;
    }
    if(ok) ok = !mbedtls_sha256((const uint8_t*)header, offsetof(FieldBundleHeader, hash), header->hash, 0);
    File* output = storage_file_alloc(storage);
    bool created = ok && storage_file_open(output, path, FSAM_WRITE, FSOM_CREATE_NEW);
    ok = created && storage_file_write(output, header, sizeof(*header)) == sizeof(*header);
    for(unsigned i = 0; ok && i < card->link_count; i++) {
        File* input = storage_file_alloc(storage);
        bool opened = storage_file_open(input, card->links[i], FSAM_READ, FSOM_OPEN_EXISTING);
        ok = opened && storage_file_size(input) == header->files[i].size &&
             field_copy_checked(storage, input, output, header->files[i].size,
                                header->files[i].hash, progress, context);
        if(opened) ok = storage_file_close(input) && ok;
        storage_file_free(input);
    }
    if(ok) ok = storage_file_sync(output);
    if(created) ok = storage_file_close(output) && ok;
    storage_file_free(output);
    uint8_t expected[32];
    memcpy(expected, header->hash, sizeof(expected));
    // Full readback verifies both the manifest and each copied payload.
    if(ok) ok = field_bundle_inspect(storage, path, &header->card, progress, context) &&
                !memcmp(card, &header->card, sizeof(*card));
    free(header);
    if(!ok && created) storage_common_remove(storage, path);
    return ok;
}
bool field_bundle_import(Storage* storage, const char* path, const char* new_name,
                          FieldProgress progress, void* context) {
    if(!storage || !library_name_valid(new_name) || !library_path_valid(path)) return false;
    FieldBundleHeader* header = malloc(sizeof(*header));
    // Validate the complete input before creating anything on the destination SD.
    bool ok = field_bundle_inspect(storage, path, &header->card, progress, context);
    FuriString* directory = furi_string_alloc_printf(LIBRARY_CARDS "/%s", new_name);
    if(ok) ok = storage_common_stat(storage, furi_string_get_cstr(directory), NULL) == FSE_NOT_EXIST &&
                storage_simply_mkdir(storage, LIBRARY_ROOT) &&
                storage_simply_mkdir(storage, LIBRARY_CARDS) &&
                storage_simply_mkdir(storage, LIBRARY_ROOT "/imports");
    furi_string_printf(directory, LIBRARY_ROOT "/imports/%s", new_name);
    if(ok) ok = storage_common_stat(storage, furi_string_get_cstr(directory), NULL) == FSE_NOT_EXIST;
    File* input = storage_file_alloc(storage);
    bool opened = ok && storage_file_open(input, path, FSAM_READ, FSOM_OPEN_EXISTING);
    ok = opened && field_bundle_header_read(input, header);
    if(ok) ok = storage_simply_mkdir(storage, furi_string_get_cstr(directory));
    for(unsigned i = 0; ok && i < header->card.link_count; i++) {
        const char* extension = field_bundle_extension(library_file_kind(header->card.links[i]));
        // The original paths in the package never become destination instructions.
        int n = snprintf(header->card.links[i], sizeof(header->card.links[i]),
                         "%s/%u%s", furi_string_get_cstr(directory), i, extension);
        if(n < 0 || (size_t)n >= sizeof(header->card.links[i])) { ok = false; break; }
        File* output = storage_file_alloc(storage);
        bool created = storage_file_open(output, header->card.links[i], FSAM_WRITE, FSOM_CREATE_NEW);
        ok = created && field_copy_checked(storage, input, output, header->files[i].size,
                                           header->files[i].hash, progress, context);
        if(ok) ok = storage_file_sync(output);
        if(created) ok = storage_file_close(output) && ok;
        storage_file_free(output);
        uint32_t actual_size;
        uint8_t actual_hash[32];
        if(ok) ok = field_file_hash(storage, header->card.links[i], &actual_size, actual_hash, progress, context) &&
                    actual_size == header->files[i].size && !memcmp(actual_hash, header->files[i].hash, 32);
    }
    if(opened) ok = storage_file_close(input) && ok;
    storage_file_free(input);
    if(ok) {
        memset(header->card.name, 0, sizeof(header->card.name));
        memcpy(header->card.name, new_name, strlen(new_name));
        // Publish the card only after every new payload has verified successfully.
        furi_string_printf(directory, LIBRARY_CARDS "/%s", new_name);
        ok = storage_common_stat(storage, furi_string_get_cstr(directory), NULL) == FSE_NOT_EXIST &&
             library_card_save(storage, &header->card);
    }
    furi_string_free(directory);
    free(header);
    // Failed imports intentionally keep their private partial copies for inspection;
    // they do not publish a card or remove/overwrite any existing capture.
    return ok;
}
