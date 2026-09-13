#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MARAUDER_DEVICE_INDEX_MAX UINT32_C(2147483647)

/** Parse the non-negative decimal argument following an exact command prefix.
 *
 * The ESP32 command parser ultimately stores the index in a signed 32-bit value,
 * so values above INT32_MAX are rejected rather than allowed to wrap remotely.
 */
bool wifi_marauder_parse_device_index_command(
    const char* command,
    const char* command_prefix,
    uint32_t* index);

#ifdef __cplusplus
}
#endif
