#pragma once
#include <furi.h>

#define ARCHIVE_FAVORITES_PLUGIN_ID "archive_favorites"
#define ARCHIVE_FAVORITES_PLUGIN_VERSION 1U
#define ARCHIVE_FAVORITES_PLUGIN_PATH EXT_PATH("apps_data/archive/plugins/archive_favorites.fal")

typedef enum { FavoritePrune, FavoriteDelete, FavoriteRename, FavoriteAppend, FavoriteReorder } FavoriteEdit;

/** Synchronous, borrowed callbacks and strings; the plugin retains no host pointers. */
typedef struct {
    FavoriteEdit edit;
    const char* source;
    const char* destination;
    void* context;
    size_t count;
    const char* (*path_at)(void* context, size_t index);
    bool (*app_available)(void* context, const char* path);
} ArchiveFavoriteRequest;

typedef struct {
    bool (*rewrite)(const ArchiveFavoriteRequest* request);
} ArchiveFavoritesPlugin;
