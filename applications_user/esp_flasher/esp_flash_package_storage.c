#include "esp_flash_package_storage.h"

#include "crypto/sha256.h"
#include "lib/esp-serial-flasher/private_include/md5_hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool storage_error(char* error, size_t error_size, const char* message) {
    if(error && error_size) snprintf(error, error_size, "%s", message);
    return false;
}

static bool storage_join(
    char* output,
    size_t output_size,
    const char* directory,
    const char* name) {
    const int written = snprintf(output, output_size, "%s/%s", directory, name);
    return written > 0 && (size_t)written < output_size;
}

static bool storage_has_suffix(const char* text, const char* suffix) {
    const size_t text_length = strlen(text);
    const size_t suffix_length = strlen(suffix);
    return text_length >= suffix_length &&
           strcmp(text + text_length - suffix_length, suffix) == 0;
}

static bool storage_manifest_exists(Storage* storage, const char* directory_name) {
    char directory[ESP_FLASH_PACKAGE_PATH_MAX];
    char manifest[ESP_FLASH_PACKAGE_PATH_MAX];
    if(!storage_join(directory, sizeof(directory), ESP_FLASH_PACKAGE_ROOT, directory_name) ||
       !storage_join(manifest, sizeof(manifest), directory, ESP_FLASH_PACKAGE_MANIFEST)) {
        return false;
    }
    FileInfo info;
    return storage_common_stat(storage, manifest, &info) == FSE_OK && !file_info_is_dir(&info) &&
           info.size > 0U && info.size <= 16384U;
}

size_t esp_flash_package_list_directories(
    Storage* storage,
    char directories[][ESP_FLASH_PACKAGE_DIRECTORY_MAX],
    size_t capacity) {
    if(!storage || !directories || !capacity) return 0U;
    if(capacity > ESP_FLASH_PACKAGE_MAX_DIRECTORIES) {
        capacity = ESP_FLASH_PACKAGE_MAX_DIRECTORIES;
    }

    File* directory = storage_file_alloc(storage);
    FileInfo info;
    char name[256];
    size_t count = 0U;
    if(storage_dir_open(directory, ESP_FLASH_PACKAGE_ROOT)) {
        while(count < capacity && storage_dir_read(directory, &info, name, sizeof(name))) {
            const size_t name_length = strlen(name);
            if(!file_info_is_dir(&info) || name_length >= ESP_FLASH_PACKAGE_DIRECTORY_MAX ||
               !esp_flash_package_directory_name_allowed(name) ||
               !storage_manifest_exists(storage, name)) {
                continue;
            }
            memcpy(directories[count++], name, name_length + 1U);
        }
        storage_dir_close(directory);
    }
    storage_file_free(directory);

    for(size_t outer = 0U; outer < count; ++outer) {
        for(size_t inner = outer + 1U; inner < count; ++inner) {
            if(strcmp(directories[outer], directories[inner]) > 0) {
                char temporary[ESP_FLASH_PACKAGE_DIRECTORY_MAX];
                memcpy(temporary, directories[outer], sizeof(temporary));
                memcpy(directories[outer], directories[inner], sizeof(temporary));
                memcpy(directories[inner], temporary, sizeof(temporary));
            }
        }
    }
    return count;
}

static bool storage_read_manifest(
    Storage* storage,
    const char* path,
    char** json,
    size_t* json_size,
    char* error,
    size_t error_size) {
    FileInfo info;
    if(storage_common_stat(storage, path, &info) != FSE_OK || file_info_is_dir(&info) ||
       info.size == 0U || info.size > 16384U) {
        return storage_error(error, error_size, "Manifest missing or larger than 16 KiB");
    }

    char* buffer = malloc((size_t)info.size + 1U);
    if(!buffer) return storage_error(error, error_size, "Not enough memory for manifest");

    File* file = storage_file_alloc(storage);
    bool valid = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    size_t offset = 0U;
    while(valid && offset < info.size) {
        const size_t remaining = (size_t)info.size - offset;
        const size_t chunk = remaining < 1024U ? remaining : 1024U;
        const size_t read = storage_file_read(file, buffer + offset, chunk);
        if(read == 0U) {
            valid = false;
        } else {
            offset += read;
        }
    }
    storage_file_close(file);
    storage_file_free(file);

    if(!valid || offset != info.size) {
        free(buffer);
        return storage_error(error, error_size, "Manifest read failed");
    }
    buffer[offset] = '\0';
    *json = buffer;
    *json_size = offset;
    return true;
}

static bool storage_validate_directory_contents(
    Storage* storage,
    const char* package_path,
    const EspFlashPackagePlan* plan,
    char* error,
    size_t error_size) {
    char bin_names[ESP_FLASH_PACKAGE_MAX_SEGMENTS + 1U][ESP_FLASH_PACKAGE_NAME_MAX];
    const char* bin_name_pointers[ESP_FLASH_PACKAGE_MAX_SEGMENTS + 1U];
    size_t bin_count = 0U;
    File* directory = storage_file_alloc(storage);
    FileInfo info;
    char name[256];
    bool valid = storage_dir_open(directory, package_path);
    while(valid && storage_dir_read(directory, &info, name, sizeof(name))) {
        if(file_info_is_dir(&info)) {
            valid = storage_error(error, error_size, "Nested package folders are not allowed");
            break;
        }
        if(strcmp(name, ESP_FLASH_PACKAGE_MANIFEST) == 0) continue;
        if(!storage_has_suffix(name, ".bin")) continue;

        const size_t name_length = strlen(name);
        if(bin_count >= ESP_FLASH_PACKAGE_MAX_SEGMENTS + 1U ||
           name_length >= ESP_FLASH_PACKAGE_NAME_MAX) {
            valid = storage_error(error, error_size, "Extra or duplicate .bin file in package");
            break;
        }
        memcpy(bin_names[bin_count], name, name_length + 1U);
        bin_name_pointers[bin_count] = bin_names[bin_count];
        bin_count++;
    }
    storage_dir_close(directory);
    storage_file_free(directory);
    if(!valid ||
       !esp_flash_package_validate_bin_names(plan, bin_name_pointers, bin_count)) {
        return valid ? storage_error(error, error_size, "Package .bin set is incomplete") : false;
    }
    return true;
}

static bool storage_validate_segment(
    Storage* storage,
    const char* package_path,
    const EspFlashPackageSegment* segment,
    char* error,
    size_t error_size) {
    char path[ESP_FLASH_PACKAGE_PATH_MAX];
    if(!storage_join(path, sizeof(path), package_path, segment->file_name)) {
        return storage_error(error, error_size, "Segment path is too long");
    }

    FileInfo info;
    if(storage_common_stat(storage, path, &info) != FSE_OK || file_info_is_dir(&info) ||
       info.size != segment->size) {
        return storage_error(error, error_size, "Segment size does not match manifest");
    }

    struct MD5Context md5;
    ZfSha256Context sha256;
    MD5Init(&md5);
    zf_sha256_init(&sha256);

    File* file = storage_file_alloc(storage);
    bool valid = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    uint8_t buffer[1024];
    uint32_t remaining = segment->size;
    while(valid && remaining > 0U) {
        const size_t requested = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        const size_t read = storage_file_read(file, buffer, requested);
        if(read == 0U) {
            valid = false;
        } else {
            MD5Update(&md5, buffer, (unsigned)read);
            zf_sha256_update(&sha256, buffer, read);
            remaining -= (uint32_t)read;
        }
    }
    storage_file_close(file);
    storage_file_free(file);

    uint8_t actual_md5[16];
    uint8_t actual_sha256[32];
    MD5Final(actual_md5, &md5);
    zf_sha256_finish(&sha256, actual_sha256);
    if(!valid || remaining != 0U ||
       storage_common_stat(storage, path, &info) != FSE_OK || file_info_is_dir(&info) ||
       info.size != segment->size || memcmp(actual_md5, segment->md5, sizeof(actual_md5)) != 0 ||
       memcmp(actual_sha256, segment->sha256, sizeof(actual_sha256)) != 0) {
        return storage_error(error, error_size, "Segment digest does not match manifest");
    }
    return true;
}

bool esp_flash_package_load_verified(
    Storage* storage,
    const char* directory_name,
    EspFlashPackagePlan* plan,
    char* package_path,
    size_t package_path_size,
    char* error,
    size_t error_size) {
    if(error && error_size) error[0] = '\0';
    if(!storage || !directory_name || !plan || !package_path ||
       !esp_flash_package_directory_name_allowed(directory_name) ||
       !storage_join(package_path, package_path_size, ESP_FLASH_PACKAGE_ROOT, directory_name)) {
        return storage_error(error, error_size, "Unsafe package directory");
    }

    char manifest_path[ESP_FLASH_PACKAGE_PATH_MAX];
    if(!storage_join(
           manifest_path, sizeof(manifest_path), package_path, ESP_FLASH_PACKAGE_MANIFEST)) {
        return storage_error(error, error_size, "Manifest path is too long");
    }

    char* json = NULL;
    size_t json_size = 0U;
    if(!storage_read_manifest(
           storage, manifest_path, &json, &json_size, error, error_size)) {
        return false;
    }
    bool valid = esp_flash_package_parse_manifest(json, json_size, plan, error, error_size);
    free(json);
    if(!valid) return false;

    if(!storage_validate_directory_contents(storage, package_path, plan, error, error_size)) {
        return false;
    }
    for(size_t i = 0; i < plan->segment_count; ++i) {
        if(!storage_validate_segment(storage, package_path, &plan->segments[i], error, error_size)) {
            return false;
        }
    }
    return true;
}
