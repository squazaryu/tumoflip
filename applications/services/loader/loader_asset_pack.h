#pragma once

#include <gui/icon.h>

typedef struct LoaderAssetPack LoaderAssetPack;

typedef enum {
    LoaderAssetPackIconModuleOne,
} LoaderAssetPackIconId;

LoaderAssetPack* loader_asset_pack_alloc(void);

void loader_asset_pack_free(LoaderAssetPack* pack);

const Icon* loader_asset_pack_get_icon(
    LoaderAssetPack* pack,
    LoaderAssetPackIconId id,
    const Icon* fallback);
