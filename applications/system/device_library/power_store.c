#include "power_store.h"
#include "field_io.h"

#include <stdio.h>
#include <string.h>

#define POWER_STORE_ROOT LIBRARY_ROOT "/power"
#define POWER_STORE_MAGIC 0x50575231U

PowerStoreStatus power_store_load(Storage* storage, PowerHistory* history) {
    if(!storage || !history) return PowerStoreError;
    power_history_init(history);
    if(field_record_load(storage, POWER_STORE_ROOT, POWER_STORE_MAGIC, history, sizeof(*history)) &&
       power_history_valid(history)) {
        power_history_pause(history); // no inference over time when this FAP was closed
        return PowerStoreOk;
    }
    power_history_init(history);
    bool exists = false;
    for(unsigned i = 0; i < 4U; i++) {
        char path[128];
        snprintf(path, sizeof(path), "%s/%u.record", POWER_STORE_ROOT, i);
        const FS_Error status = storage_common_stat(storage, path, NULL);
        if(status == FSE_OK) exists = true;
        else if(status != FSE_NOT_EXIST) return PowerStoreError;
    }
    return exists ? PowerStoreError : PowerStoreEmpty;
}

bool power_store_save(Storage* storage, const PowerHistory* history) {
    return storage && power_history_valid(history) &&
           storage_simply_mkdir(storage, EXT_PATH("apps_data")) &&
           field_record_save(storage, POWER_STORE_ROOT, POWER_STORE_MAGIC, history, sizeof(*history));
}

static bool power_export_write(
    File* file, mbedtls_sha256_context* hash, const char* text, uint32_t* total) {
    const size_t size = strlen(text);
    if(size > UINT32_MAX - *total || storage_file_write(file, text, size) != size ||
       mbedtls_sha256_update(hash, (const uint8_t*)text, size)) return false;
    *total += size;
    return true;
}

bool power_store_export(Storage* storage, const PowerHistory* history, char* path, size_t capacity) {
    if(!storage || !path || !power_history_valid(history) || !history->count ||
       !storage_simply_mkdir(storage, EXT_PATH("apps_data")) ||
       !storage_simply_mkdir(storage, LIBRARY_ROOT) ||
       !storage_simply_mkdir(storage, POWER_STORE_ROOT)) {
        return false;
    }
    const PowerHistorySample* latest = power_history_at(history, history->count - 1U);
    char target[160];
    bool available = false;
    for(unsigned i = 0; i < 32U; i++) {
        snprintf(target, sizeof(target), "%s/power_%lu_%u.csv", POWER_STORE_ROOT,
                 (unsigned long)latest->timestamp, i);
        const FS_Error status = storage_common_stat(storage, target, NULL);
        if(status == FSE_NOT_EXIST) { available = true; break; }
        if(status != FSE_OK) return false;
    }
    if(!available || strlen(target) >= capacity) return false;
    File* file = storage_file_alloc(storage);
    const bool created = storage_file_open(file, target, FSAM_WRITE, FSOM_CREATE_NEW);
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    uint32_t written_size = 0;
    bool ok = created && !mbedtls_sha256_starts(&hash, 0) &&
              power_export_write(file, &hash,
                  "timestamp,soc,voltage_mv,current_ma,temp_tenths,heap_free,usb,charging,gauge_health\n",
                  &written_size);
    for(unsigned i = 0; ok && i < history->count; i++) {
        const PowerHistorySample* sample = power_history_at(history, i);
        char line[144];
        const int size = snprintf(line, sizeof(line), "%lu,%u,%u,%ld,%d,%lu,%u,%u,%u\n",
            (unsigned long)sample->timestamp, sample->soc, sample->voltage_mv,
            (long)sample->current_ma, sample->temperature_tenths,
            (unsigned long)sample->heap_free, sample->usb, sample->charging, sample->gauge_health);
        ok = size > 0 && (size_t)size < sizeof(line) &&
             power_export_write(file, &hash, line, &written_size);
    }
    uint8_t expected[32], actual[32];
    uint32_t read_size = 0;
    if(ok) ok = !mbedtls_sha256_finish(&hash, expected) && storage_file_sync(file);
    mbedtls_sha256_free(&hash);
    if(created) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    if(ok) {
        ok = field_file_hash(storage, target, &read_size, actual, NULL, NULL) &&
             read_size == written_size && !memcmp(actual, expected, sizeof(expected));
    }
    if(!ok) {
        if(created) storage_common_remove(storage, target); // only this new, incomplete export
        return false;
    }
    memcpy(path, target, strlen(target) + 1U);
    return true;
}
