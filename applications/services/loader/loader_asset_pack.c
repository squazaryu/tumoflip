#include "loader_asset_pack.h"

#include <core/dangerous_defines.h>
#include <furi.h>
#include <gui/icon_i.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

#define TUMOFLIP_ASSET_PACK_ACTIVE_PATH EXT_PATH("apps_data/tumoflip/asset_packs/active.txt")
#define TUMOFLIP_ASSET_PACK_ICON_PATH_FMT \
    EXT_PATH("apps_data/tumoflip/asset_packs/%s/Icons/%s")

#define LOADER_ASSET_PACK_NAME_MAX            32U
#define LOADER_ASSET_PACK_PATH_MAX            128U
#define LOADER_ASSET_PACK_ICON_WIDTH          14U
#define LOADER_ASSET_PACK_ICON_HEIGHT         14U
#define LOADER_ASSET_PACK_MAX_ICON_FRAME_SIZE 256U

typedef struct FURI_PACKED {
    uint32_t width;
    uint32_t height;
} LoaderAssetPackBmxHeader;

typedef struct {
    Icon icon;
    uint8_t* frames[1];
    uint8_t frame[];
} LoaderAssetPackStaticIcon;

struct LoaderAssetPack {
    LoaderAssetPackStaticIcon* module_one_icon;
    LoaderAssetPackStaticIcon* arf_tools_icon;
};

static bool loader_asset_pack_is_name_char(char c) {
    return ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) ||
           ((c >= '0') && (c <= '9')) || (c == '_') || (c == '-');
}

static void loader_asset_pack_trim_name(char* name) {
    size_t len = strlen(name);
    while((len > 0U) &&
          ((name[len - 1U] == '\r') || (name[len - 1U] == '\n') ||
           (name[len - 1U] == ' ') || (name[len - 1U] == '\t'))) {
        name[--len] = '\0';
    }

    char* start = name;
    while((*start == ' ') || (*start == '\t')) {
        start++;
    }

    if(start != name) {
        memmove(name, start, strlen(start) + 1U);
    }
}

static bool loader_asset_pack_validate_name(const char* name) {
    if(name[0] == '\0') {
        return false;
    }

    for(size_t i = 0; name[i] != '\0'; i++) {
        if(!loader_asset_pack_is_name_char(name[i])) {
            return false;
        }
    }

    return true;
}

static bool loader_asset_pack_read_active_name(Storage* storage, char* name, size_t name_size) {
    furi_assert(name_size > 1U);

    bool loaded = false;
    File* file = storage_file_alloc(storage);

    do {
        if(!storage_file_open(
               file, TUMOFLIP_ASSET_PACK_ACTIVE_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
            break;
        }

        const uint64_t file_size = storage_file_size(file);
        if((file_size == 0U) || (file_size > name_size)) {
            break;
        }

        const size_t max_read_size = name_size - 1U;
        const size_t read_size = (file_size < max_read_size) ? (size_t)file_size : max_read_size;
        if(storage_file_read(file, name, read_size) != read_size) {
            break;
        }

        name[read_size] = '\0';
        loader_asset_pack_trim_name(name);
        loaded = loader_asset_pack_validate_name(name);
    } while(false);

    storage_file_free(file);
    return loaded;
}

static LoaderAssetPackStaticIcon*
    loader_asset_pack_load_icon(Storage* storage, const char* pack_name, const char* icon_name) {
    char path[LOADER_ASSET_PACK_PATH_MAX];
    const int written =
        snprintf(path, sizeof(path), TUMOFLIP_ASSET_PACK_ICON_PATH_FMT, pack_name, icon_name);
    if((written < 0) || ((size_t)written >= sizeof(path))) {
        return NULL;
    }

    LoaderAssetPackStaticIcon* icon = NULL;
    File* file = storage_file_alloc(storage);

    do {
        if(!storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
            break;
        }

        const uint64_t file_size = storage_file_size(file);
        if((file_size <= sizeof(LoaderAssetPackBmxHeader)) ||
           (file_size > (sizeof(LoaderAssetPackBmxHeader) + LOADER_ASSET_PACK_MAX_ICON_FRAME_SIZE))) {
            break;
        }

        LoaderAssetPackBmxHeader header;
        if(storage_file_read(file, &header, sizeof(header)) != sizeof(header)) {
            break;
        }

        if((header.width != LOADER_ASSET_PACK_ICON_WIDTH) ||
           (header.height != LOADER_ASSET_PACK_ICON_HEIGHT)) {
            break;
        }

        const size_t frame_size = (size_t)file_size - sizeof(LoaderAssetPackBmxHeader);
        icon = malloc(sizeof(LoaderAssetPackStaticIcon) + frame_size);
        if(!icon) {
            break;
        }

        if(storage_file_read(file, icon->frame, frame_size) != frame_size) {
            free(icon);
            icon = NULL;
            break;
        }

        FURI_CONST_ASSIGN(icon->icon.width, header.width);
        FURI_CONST_ASSIGN(icon->icon.height, header.height);
        FURI_CONST_ASSIGN(icon->icon.frame_count, 1);
        FURI_CONST_ASSIGN(icon->icon.frame_rate, 1);
        FURI_CONST_ASSIGN_PTR(icon->icon.frames, icon->frames);
        icon->frames[0] = icon->frame;
    } while(false);

    storage_file_free(file);
    return icon;
}

LoaderAssetPack* loader_asset_pack_alloc(void) {
    LoaderAssetPack* pack = malloc(sizeof(LoaderAssetPack));
    if(!pack) {
        return NULL;
    }

    pack->module_one_icon = NULL;
    pack->arf_tools_icon = NULL;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    char pack_name[LOADER_ASSET_PACK_NAME_MAX + 1U];

    if(loader_asset_pack_read_active_name(storage, pack_name, sizeof(pack_name))) {
        pack->module_one_icon =
            loader_asset_pack_load_icon(storage, pack_name, "ModuleOne_14.bmx");
        pack->arf_tools_icon =
            loader_asset_pack_load_icon(storage, pack_name, "ARFTools_14.bmx");
    }

    furi_record_close(RECORD_STORAGE);
    return pack;
}

void loader_asset_pack_free(LoaderAssetPack* pack) {
    if(!pack) {
        return;
    }

    free(pack->module_one_icon);
    free(pack->arf_tools_icon);
    free(pack);
}

const Icon* loader_asset_pack_get_icon(
    LoaderAssetPack* pack,
    LoaderAssetPackIconId id,
    const Icon* fallback) {
    furi_assert(fallback);
    if(!pack) {
        return fallback;
    }

    LoaderAssetPackStaticIcon* icon = NULL;
    if(id == LoaderAssetPackIconModuleOne) {
        icon = pack->module_one_icon;
    } else if(id == LoaderAssetPackIconArfTools) {
        icon = pack->arf_tools_icon;
    }

    return icon ? &icon->icon : fallback;
}
