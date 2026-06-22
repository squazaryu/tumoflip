#pragma once

#include "plugin_registry.h"

typedef enum {
    SubGhzProtocolPackStatusLoaded,
    SubGhzProtocolPackStatusInvalidFile,
    SubGhzProtocolPackStatusNotEnoughMemory,
    SubGhzProtocolPackStatusInvalidManifest,
    SubGhzProtocolPackStatusFirmwareApiTooOld,
    SubGhzProtocolPackStatusFirmwareApiTooNew,
    SubGhzProtocolPackStatusTargetMismatch,
    SubGhzProtocolPackStatusNotPlugin,
    SubGhzProtocolPackStatusLoadError,
    SubGhzProtocolPackStatusMissingImports,
    SubGhzProtocolPackStatusMissingDescriptor,
    SubGhzProtocolPackStatusApplicationIdMismatch,
    SubGhzProtocolPackStatusPluginApiMismatch,
    SubGhzProtocolPackStatusInvalidProtocol,
    SubGhzProtocolPackStatusDuplicateProtocol,
} SubGhzProtocolPackStatus;

typedef struct {
    const char* filename;
    const char* protocol_name;
    SubGhzProtocolPackStatus status;
} SubGhzProtocolPackReportEntry;

typedef struct {
    SubGhzProtocolPackGroup group;
    size_t base_protocol_count;
    size_t expected_plugin_count;
    size_t loaded_plugin_count;
    size_t registered_protocol_count;
    size_t heap_used;
    uint16_t firmware_api_major;
    uint16_t firmware_api_minor;
    uint32_t plugin_api_version;
    const SubGhzProtocolPackReportEntry* entries;
} SubGhzProtocolPackReport;

const char* subghz_protocol_pack_group_get_name(SubGhzProtocolPackGroup group);
const char* subghz_protocol_pack_status_get_name(SubGhzProtocolPackStatus status);

const SubGhzProtocolPackReport*
    subghz_protocol_pack_registry_get_report(const SubGhzProtocolPackRegistry* instance);
