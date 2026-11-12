#include "../helpers/archive_favorites.h"
#include "../helpers/archive_favorites_plugin.h"
#include <flipper_application/flipper_application.h>
#include <signal_file/signal_file.h>

#define ARCHIVE_FAV_FILE_BUF_LEN 32

static bool archive_favorites_read_line(File* file, FuriString* str_result) {
    furi_string_reset(str_result);
    uint8_t buffer[ARCHIVE_FAV_FILE_BUF_LEN];
    bool result = false;

    do {
        size_t read_count = storage_file_read(file, buffer, ARCHIVE_FAV_FILE_BUF_LEN);
        if(storage_file_get_error(file) != FSE_OK) {
            return false;
        }

        for(size_t i = 0; i < read_count; i++) {
            if(buffer[i] == '\n') {
                uint32_t position = storage_file_tell(file);
                if(storage_file_get_error(file) != FSE_OK) {
                    return false;
                }

                position = position - read_count + i + 1;

                if(!storage_file_seek(file, position, true) || storage_file_get_error(file) != FSE_OK) {
                    return false;
                }

                result = true;
                break;
            } else {
                furi_string_push_back(str_result, buffer[i]);
            }
        }

        if(result || read_count == 0) {
            break;
        }
    } while(true);

    return result || furi_string_size(str_result) > 0;
}

static bool archive_favorite_write_line(File* file, const char* text) {
    const size_t size = strlen(text);
    return storage_file_write(file, text, size) == size &&
           storage_file_write(file, "\n", 1) == 1;
}

static bool archive_favorites_write(const ArchiveFavoriteRequest* request) {
    if(!request || (unsigned)request->edit > FavoriteReorder ||
       ((request->edit == FavoriteDelete || request->edit == FavoriteRename || request->edit == FavoriteAppend) && !request->source) ||
       (request->edit == FavoriteRename && !request->destination) ||
       (request->edit == FavoriteReorder && !request->path_at) ||
       (request->edit == FavoritePrune && !request->app_available)) return false;
    const FavoriteEdit edit = request->edit;
    const char* src = request->source;
    const char* dst = request->destination;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* input = storage_file_alloc(storage);
    File* output = storage_file_alloc(storage);
    FuriString* line = furi_string_alloc();
    bool input_open = false;
    bool output_open = false;
    bool ok = true;
    if(edit != FavoriteReorder) {
        input_open = storage_file_open(input, ARCHIVE_FAV_PATH, FSAM_READ, FSOM_OPEN_EXISTING);
        ok = input_open || (edit == FavoriteAppend && storage_file_get_error(input) == FSE_NOT_EXIST);
    }
    if(ok) {
        output_open = storage_file_open(output, ARCHIVE_FAV_TEMP_PATH, FSAM_WRITE, FSOM_CREATE_NEW);
        ok = output_open;
    }
    if(edit == FavoriteReorder) {
        for(size_t i = 0; ok && i < request->count; i++) {
            const char* path = request->path_at(request->context, i);
            ok = path && archive_favorite_write_line(output, path);
        }
    } else if(input_open) {
        while(ok && archive_favorites_read_line(input, line)) {
            if(!furi_string_size(line)) continue;
            const char* path = furi_string_get_cstr(line);
            bool keep = true;
            if(edit == FavoritePrune) {
                if(furi_string_search(line, "/app:") == 0) {
                    keep = request->app_available(request->context, path);
                } else {
                    const FS_Error status = storage_common_stat(storage, path, NULL);
                    ok = status == FSE_OK || status == FSE_NOT_EXIST;
                    keep = status == FSE_OK;
                }
            } else if((edit == FavoriteDelete || edit == FavoriteRename) && strcmp(path, src) == 0) {
                keep = edit == FavoriteRename;
                path = dst;
            }
            if(ok && keep) ok = archive_favorite_write_line(output, path);
        }
        ok = ok && storage_file_get_error(input) == FSE_OK;
    }
    if(ok && edit == FavoriteAppend) ok = archive_favorite_write_line(output, src);
    if(input_open) ok = storage_file_close(input) && ok;
    if(output_open) {
        ok = ok && storage_file_sync(output);
        ok = storage_file_close(output) && ok;
    }
    if(ok) {
        ok = signal_file_replace(storage, ARCHIVE_FAV_TEMP_PATH, ARCHIVE_FAV_PATH);
        if(ok && storage_common_remove(storage, ARCHIVE_FAV_TEMP_PATH) != FSE_OK) {
            // The destination was already read-back verified; a cleanup error is
            // not a failed save. Keep the temp rather than pretend the edit failed.
            FURI_LOG_W("Favorites", "Saved; temporary list retained");
        }
    } else if(output_open) {
        storage_common_remove(storage, ARCHIVE_FAV_TEMP_PATH); // only our incomplete temp
    }
    storage_file_free(output);
    storage_file_free(input);
    furi_string_free(line);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

static const ArchiveFavoritesPlugin api = {.rewrite = archive_favorites_write};
static const FlipperAppPluginDescriptor descriptor = {
    .appid = ARCHIVE_FAVORITES_PLUGIN_ID,
    .ep_api_version = ARCHIVE_FAVORITES_PLUGIN_VERSION,
    .entry_point = &api,
};
const FlipperAppPluginDescriptor* archive_favorites_ep(void) { return &descriptor; }
