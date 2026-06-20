#pragma once

#include "../registry.h"

typedef struct SubGhzProtocolPackRegistry SubGhzProtocolPackRegistry;

typedef enum {
    SubGhzProtocolPackGroupCore,
    SubGhzProtocolPackGroupLegacy,
    SubGhzProtocolPackGroupKia,
    SubGhzProtocolPackGroupFord,
    SubGhzProtocolPackGroupEurope,
    SubGhzProtocolPackGroupAsiaUs,
    SubGhzProtocolPackGroupAlarm,
    SubGhzProtocolPackGroupCount,
} SubGhzProtocolPackGroup;

SubGhzProtocolPackRegistry* subghz_protocol_pack_registry_alloc(
    const SubGhzProtocolRegistry* base_registry,
    const char* plugin_path,
    SubGhzProtocolPackGroup group);

void subghz_protocol_pack_registry_free(SubGhzProtocolPackRegistry* instance);

const SubGhzProtocolRegistry*
    subghz_protocol_pack_registry_get(SubGhzProtocolPackRegistry* instance);
