#pragma once

#include "../registry.h"

typedef struct SubGhzProtocolPackRegistry SubGhzProtocolPackRegistry;

SubGhzProtocolPackRegistry* subghz_protocol_pack_registry_alloc(
    const SubGhzProtocolRegistry* base_registry,
    const char* plugin_path);

void subghz_protocol_pack_registry_free(SubGhzProtocolPackRegistry* instance);

const SubGhzProtocolRegistry*
    subghz_protocol_pack_registry_get(SubGhzProtocolPackRegistry* instance);
