#include "field_inventory.h"
#include <stdio.h>
#include <string.h>

#define FIELD_INVENTORY_MAX 512U
#define FIELD_INVENTORY_DEPTH 6U
static const char* const field_inventory_roots[] = {
    "/ext/subghz", "/ext/infrared", "/ext/nfc", "/ext/lfrfid", "/ext/ibutton",
};
typedef struct { uint32_t magic, version, entry_size, reserved; } FieldInventoryHeader;
typedef struct { char path[256]; uint32_t size; uint8_t hash[32]; } FieldInventoryEntry;
typedef struct { uint32_t magic, count; uint8_t hash[32]; } FieldInventoryFooter;
typedef struct FieldInventoryWalk FieldInventoryWalk;
struct FieldInventoryWalk {
    Storage* storage;
    File* manifest;
    uint32_t count, directories, baseline_count;
    FieldProgress progress;
    FieldInventoryVisit visit;
    void* context;
    bool cancelled;
    mbedtls_sha256_context* sha;
    FieldInventoryStats* stats;
};

static bool field_inventory_path(const char* path) {
    if(!memchr(path, 0, 256) || !library_path_valid(path) || library_file_kind(path) == LibraryFileReference)
        return false;
    for(unsigned i = 0; i < sizeof(field_inventory_roots) / sizeof(field_inventory_roots[0]); i++) {
        size_t n = strlen(field_inventory_roots[i]);
        if(!strncmp(path, field_inventory_roots[i], n) && path[n] == '/') return true;
    }
    return false;
}
static bool field_inventory_progress(void* context, const char* path, uint32_t count) {
    FieldInventoryWalk* walk = context;
    if(walk->progress && !walk->progress(walk->context, path, count)) walk->cancelled = true;
    return !walk->cancelled;
}
static bool field_inventory_validate(File* file, uint32_t* count, FieldInventoryWalk* walk) {
    FieldInventoryHeader header;
    uint64_t size = storage_file_size(file);
    if(size < sizeof(header) + sizeof(FieldInventoryFooter) ||
       (size - sizeof(header) - sizeof(FieldInventoryFooter)) % sizeof(FieldInventoryEntry) ||
       size > sizeof(header) + sizeof(FieldInventoryFooter) + FIELD_INVENTORY_MAX * sizeof(FieldInventoryEntry) ||
       !storage_file_seek(file, 0, true) || storage_file_read(file, &header, sizeof(header)) != sizeof(header) ||
       header.magic != 0x31564954UL || header.version != 1 || header.entry_size != sizeof(FieldInventoryEntry) ||
       header.reserved) return false;
    *count = (uint32_t)((size - sizeof(header) - sizeof(FieldInventoryFooter)) / sizeof(FieldInventoryEntry));
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool ok = !mbedtls_sha256_starts(&sha, 0) && !mbedtls_sha256_update(&sha, (const uint8_t*)&header, sizeof(header));
    FieldInventoryEntry entry;
    for(unsigned i = 0; ok && i < *count; i++) {
        ok = field_inventory_progress(walk, NULL, i) &&
             storage_file_read(file, &entry, sizeof(entry)) == sizeof(entry) &&
             field_inventory_path(entry.path) && entry.size <= FIELD_FILE_MAX &&
             !mbedtls_sha256_update(&sha, (const uint8_t*)&entry, sizeof(entry));
    }
    FieldInventoryFooter footer;
    uint8_t hash[32];
    if(ok) ok = storage_file_read(file, &footer, sizeof(footer)) == sizeof(footer) &&
                footer.magic == 0x444E4554UL && footer.count == *count &&
                !mbedtls_sha256_finish(&sha, hash) && !memcmp(hash, footer.hash, 32);
    mbedtls_sha256_free(&sha);
    return ok;
}
static bool field_inventory_contains(FieldInventoryWalk* walk, const char* path) {
    if(!storage_file_seek(walk->manifest, sizeof(FieldInventoryHeader), true)) return false;
    FieldInventoryEntry entry;
    for(unsigned i = 0; i < walk->baseline_count; i++) {
        if(storage_file_read(walk->manifest, &entry, sizeof(entry)) != sizeof(entry)) return false;
        if(!strcmp(entry.path, path)) return true;
    }
    return false;
}
static bool field_inventory_scan_file(FieldInventoryWalk* walk, const char* path) {
    if(++walk->count > FIELD_INVENTORY_MAX || !field_inventory_progress(walk, path, walk->count)) return false;
    if(walk->stats) {
        if(!field_inventory_contains(walk, path)) {
            if(storage_file_get_error(walk->manifest) != FSE_OK) return false;
            walk->stats->added++;
            if(walk->visit) walk->visit(walk->context, path, FieldInventoryAdded);
        }
        return true;
    }
    FieldInventoryEntry entry = {0};
    memcpy(entry.path, path, strlen(path));
    return field_file_hash(walk->storage, path, &entry.size, entry.hash, field_inventory_progress, walk) &&
           storage_file_write(walk->manifest, &entry, sizeof(entry)) == sizeof(entry) &&
           !mbedtls_sha256_update(walk->sha, (const uint8_t*)&entry, sizeof(entry));
}
static bool field_inventory_scan_dir(FieldInventoryWalk* walk, const char* path, unsigned depth) {
    if(depth > FIELD_INVENTORY_DEPTH || ++walk->directories > 1024 ||
       !field_inventory_progress(walk, path, walk->count)) return false;
    File* directory = storage_file_alloc(walk->storage);
    bool ok = storage_dir_open(directory, path);
    FileInfo info;
    char name[256], child[256];
    while(ok && storage_dir_read(directory, &info, name, sizeof(name))) {
        if(!strcmp(name, ".") || !strcmp(name, "..")) continue;
        int n = snprintf(child, sizeof(child), "%s/%s", path, name);
        if(n < 0 || (size_t)n >= sizeof(child) || !library_path_valid(child)) { ok = false; break; }
        if(file_info_is_dir(&info)) ok = field_inventory_scan_dir(walk, child, depth + 1);
        else if(library_file_kind(child) != LibraryFileReference) ok = field_inventory_scan_file(walk, child);
    }
    FS_Error error = storage_file_get_error(directory);
    if(error != FSE_OK && error != FSE_NOT_EXIST) ok = false;
    storage_file_free(directory);
    return ok;
}
static bool field_inventory_walk_roots(FieldInventoryWalk* walk) {
    for(unsigned i = 0; i < sizeof(field_inventory_roots) / sizeof(field_inventory_roots[0]); i++) {
        FileInfo info;
        FS_Error error = storage_common_stat(walk->storage, field_inventory_roots[i], &info);
        if(error == FSE_NOT_EXIST) continue;
        if(error != FSE_OK || !file_info_is_dir(&info) ||
           !field_inventory_scan_dir(walk, field_inventory_roots[i], 0)) return false;
    }
    return storage_sd_status(walk->storage) == FSE_OK;
}
bool field_inventory_create(Storage* storage, const char* path, FieldProgress progress, void* context) {
    if(!storage || !library_path_valid(path)) return false;
    File* file = storage_file_alloc(storage);
    FieldInventoryHeader header = {.magic = 0x31564954UL, .version = 1, .entry_size = sizeof(FieldInventoryEntry)};
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool created = (!progress || progress(context, path, 0)) &&
                   storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_NEW);
    bool ok = created && storage_file_write(file, &header, sizeof(header)) == sizeof(header) &&
              !mbedtls_sha256_starts(&sha, 0) && !mbedtls_sha256_update(&sha, (const uint8_t*)&header, sizeof(header));
    FieldInventoryWalk walk = {.storage = storage, .manifest = file, .sha = &sha, .progress = progress, .context = context};
    if(ok) ok = field_inventory_walk_roots(&walk);
    FieldInventoryFooter footer = {.magic = 0x444E4554UL, .count = walk.count};
    if(ok) ok = !mbedtls_sha256_finish(&sha, footer.hash) &&
                storage_file_write(file, &footer, sizeof(footer)) == sizeof(footer) && storage_file_sync(file);
    if(created) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    mbedtls_sha256_free(&sha);
    if(ok) {
        file = storage_file_alloc(storage);
        bool opened = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
        uint32_t count = 0;
        ok = opened && field_inventory_validate(file, &count, &walk) && count == walk.count;
        if(opened) ok = storage_file_close(file) && ok;
        storage_file_free(file);
    }
    if(!ok && created) storage_common_remove(storage, path);
    return ok;
}
bool field_inventory_compare(Storage* storage, const char* path, FieldInventoryStats* stats,
                              FieldInventoryVisit visit, FieldProgress progress, void* context) {
    if(!storage || !library_path_valid(path) || !stats) return false;
    memset(stats, 0, sizeof(*stats));
    File* file = storage_file_alloc(storage);
    bool opened = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    FieldInventoryWalk walk = {.storage = storage, .manifest = file, .stats = stats,
                               .visit = visit, .progress = progress, .context = context};
    bool ok = opened && field_inventory_validate(file, &walk.baseline_count, &walk);
    FieldInventoryEntry entry;
    for(unsigned i = 0; ok && i < walk.baseline_count; i++) {
        ok = field_inventory_progress(&walk, NULL, i) &&
             storage_file_seek(file, sizeof(FieldInventoryHeader) + i * sizeof(entry), true) &&
             storage_file_read(file, &entry, sizeof(entry)) == sizeof(entry);
        if(!ok) break;
        FileInfo info;
        FS_Error status = storage_common_stat(storage, entry.path, &info);
        FieldInventoryState state = FieldInventoryUnreadable;
        if(status == FSE_NOT_EXIST) state = FieldInventoryMissing;
        else if(status == FSE_OK && !file_info_is_dir(&info)) {
            uint32_t size;
            uint8_t hash[32];
            if(field_file_hash(storage, entry.path, &size, hash, field_inventory_progress, &walk))
                state = size == entry.size && !memcmp(hash, entry.hash, 32) ? FieldInventoryUnchanged : FieldInventoryChanged;
            if(walk.cancelled) { ok = false; break; }
        }
        switch(state) {
        case FieldInventoryUnchanged: stats->unchanged++; break;
        case FieldInventoryChanged: stats->changed++; break;
        case FieldInventoryMissing: stats->missing++; break;
        default: stats->unreadable++; break;
        }
        if(visit) visit(context, entry.path, state);
    }
    if(ok) ok = field_inventory_walk_roots(&walk);
    if(opened) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    if(!ok) memset(stats, 0, sizeof(*stats));
    return ok;
}
