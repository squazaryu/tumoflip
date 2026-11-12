#include "archive_favorites.h"
#include "archive_files.h"
#include "archive_apps.h"
#include "archive_browser.h"

#include <dialogs/dialogs.h>
#include "archive_favorites_plugin.h"
#include <flipper_application/plugins/plugin_manager.h>
#include <loader/firmware_api/firmware_api.h>

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

                storage_file_seek(file, position, true);
                if(storage_file_get_error(file) != FSE_OK) {
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



// The foreground caller serializes edits. Never reuse an interrupted transaction's
// temp/backup and never delete the old list to make room for a rename (which is copy/delete).
static const char* archive_favorite_path_at(void* context, size_t index) {
    ArchiveFile_t* item = archive_get_file_at(context, index);
    return item ? furi_string_get_cstr(item->path) : NULL;
}

static bool archive_favorites_rewrite(
    FavoriteEdit edit, ArchiveBrowserView* browser, const char* src, const char* dst) {
    const ArchiveFavoriteRequest request = {
        .edit = edit, .source = src, .destination = dst,
        .context = browser, .count = browser ? archive_file_get_array_size(browser) : 0,
        .path_at = archive_favorite_path_at, .app_available = archive_app_is_available,
    };
    PluginManager* manager = plugin_manager_alloc(
        ARCHIVE_FAVORITES_PLUGIN_ID, ARCHIVE_FAVORITES_PLUGIN_VERSION, firmware_api_interface);
    const ArchiveFavoritesPlugin* api = NULL;
    if(plugin_manager_load_single(manager, ARCHIVE_FAVORITES_PLUGIN_PATH) == PluginManagerErrorNone)
        api = plugin_manager_get_ep(manager, 0);
    const bool ok = api && api->rewrite && api->rewrite(&request);
    plugin_manager_free(manager);
    if(!ok) {
        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
        dialog_message_show_storage_error(dialogs, "Favorites failed.\nList/backup kept.");
        furi_record_close(RECORD_DIALOGS);
    }
    return ok;
}

uint16_t archive_favorites_count(void* context) {
    furi_assert(context);

    Storage* fs_api = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(fs_api);

    FuriString* buffer;
    buffer = furi_string_alloc();

    bool result = storage_file_open(file, ARCHIVE_FAV_PATH, FSAM_READ, FSOM_OPEN_EXISTING);
    uint16_t lines = 0;

    if(result) {
        while(1) {
            if(!archive_favorites_read_line(file, buffer)) {
                break;
            }
            if(!furi_string_size(buffer)) {
                continue; // Skip empty lines
            }
            ++lines;
        }
    }

    storage_file_close(file);

    furi_string_free(buffer);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    return lines;
}

static bool archive_favourites_rescan(void) {
    return archive_favorites_rewrite(FavoritePrune, NULL, NULL, NULL);
}

bool archive_favorites_read(void* context) {
    furi_assert(context);

    ArchiveBrowserView* browser = context;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    FuriString* buffer;
    FileInfo file_info;
    buffer = furi_string_alloc();

    bool need_refresh = false;
    uint16_t file_count = 0;

    archive_file_array_rm_all(browser);

    bool result = storage_file_open(file, ARCHIVE_FAV_PATH, FSAM_READ, FSOM_OPEN_EXISTING);

    if(result) {
        while(1) {
            if(!archive_favorites_read_line(file, buffer)) {
                break;
            }
            if(!furi_string_size(buffer)) {
                continue;
            }

            if(furi_string_search(buffer, "/app:") == 0) {
                if(archive_app_is_available(browser, furi_string_get_cstr(buffer))) {
                    archive_add_app_item(browser, furi_string_get_cstr(buffer));
                    file_count++;
                } else {
                    need_refresh = true;
                }
            } else {
                if(storage_common_exists(storage, furi_string_get_cstr(buffer))) {
                    if(storage_common_stat(storage, furi_string_get_cstr(buffer), &file_info) == FSE_OK) {
                        archive_add_file_item(
                            browser, file_info_is_dir(&file_info), furi_string_get_cstr(buffer));
                        file_count++;
                    }
                } else {
                    need_refresh = true;
                }
            }

            furi_string_reset(buffer);
        }
    }
    storage_file_close(file);
    furi_string_free(buffer);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    archive_set_item_count(browser, file_count);

    if(need_refresh) { //-V547
        archive_favourites_rescan();
    }

    return result;
}

bool archive_favorites_delete(const char* format, ...) {
    va_list args;
    va_start(args, format);
    FuriString* path = furi_string_alloc_vprintf(format, args);
    va_end(args);
    bool ok = archive_favorites_rewrite(FavoriteDelete, NULL, furi_string_get_cstr(path), NULL);
    furi_string_free(path);
    return ok;
}

bool archive_is_favorite(const char* format, ...) {
    FuriString* buffer;
    FuriString* filename;
    va_list args;
    va_start(args, format);
    filename = furi_string_alloc_vprintf(format, args);
    va_end(args);

    buffer = furi_string_alloc();
    Storage* fs_api = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(fs_api);

    bool found = false;
    bool result = storage_file_open(file, ARCHIVE_FAV_PATH, FSAM_READ, FSOM_OPEN_EXISTING);

    if(result) {
        while(1) {
            if(!archive_favorites_read_line(file, buffer)) {
                break;
            }
            if(!furi_string_size(buffer)) {
                continue;
            }
            if(!furi_string_search(buffer, filename)) {
                found = true;
                break;
            }
        }
    }

    storage_file_close(file);
    furi_string_free(buffer);
    furi_string_free(filename);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);

    return found;
}

bool archive_favorites_rename(const char* src, const char* dst) {
    furi_assert(src);
    furi_assert(dst);
    return archive_favorites_rewrite(FavoriteRename, NULL, src, dst);
}

void archive_add_to_favorites(const char* file_path) {
    furi_assert(file_path);
    archive_favorites_rewrite(FavoriteAppend, NULL, file_path, NULL);
}

void archive_favorites_save(void* context) {
    furi_assert(context);
    archive_favorites_rewrite(FavoriteReorder, context, NULL, NULL);
}

void archive_favorites_handle_setting_pin_unpin(const char* app_name, const char* setting) {
    DialogMessage* message = dialog_message_alloc();

    FuriString* setting_path = furi_string_alloc_set_str(app_name);
    if(setting) {
        furi_string_push_back(setting_path, '/');
        furi_string_cat_str(setting_path, setting);
    }
    const char* setting_path_str = furi_string_get_cstr(setting_path);

    bool is_favorite = archive_is_favorite("/app:setting/%s", setting_path_str);
    dialog_message_set_header(
        message,
        is_favorite ? "Unpin This Setting?" : "Pin This Setting?",
        64,
        0,
        AlignCenter,
        AlignTop);
    dialog_message_set_text(
        message,
        is_favorite ? "It will no longer be\naccessible from the\nFavorites menu" :
                      "It will be accessible from the\nFavorites menu",
        64,
        32,
        AlignCenter,
        AlignCenter);
    dialog_message_set_buttons(
        message, is_favorite ? "Unpin" : "Go back", NULL, is_favorite ? "Keep pinned" : "Pin");

    DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
    DialogMessageButton button = dialog_message_show(dialogs, message);
    furi_record_close(RECORD_DIALOGS);

    if(is_favorite && button == DialogMessageButtonLeft) {
        archive_favorites_delete("/app:setting/%s", setting_path_str);
    } else if(!is_favorite && button == DialogMessageButtonRight) {
        FuriString* full_path = furi_string_alloc_printf("/app:setting/%s", setting_path_str);
        archive_add_to_favorites(furi_string_get_cstr(full_path));
        furi_string_free(full_path);
    }

    furi_string_free(setting_path);
    dialog_message_free(message);
}
