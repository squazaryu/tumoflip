#include "wifi_marauder_index.h"

#include <stddef.h>
#include <string.h>

bool wifi_marauder_parse_device_index_command(
    const char* command,
    const char* command_prefix,
    uint32_t* index) {
    if(!command || !command_prefix || !index) return false;

    const size_t prefix_length = strlen(command_prefix);
    if(prefix_length == 0 || strncmp(command, command_prefix, prefix_length) != 0 ||
       command[prefix_length] != ' ') {
        return false;
    }

    const char* cursor = command + prefix_length + 1U;
    if(*cursor == '\0') return false;

    uint32_t value = 0;
    do {
        if(*cursor < '0' || *cursor > '9') return false;

        const uint32_t digit = (uint32_t)(*cursor - '0');
        if(value > (WIFI_MARAUDER_DEVICE_INDEX_MAX - digit) / 10U) return false;
        value = value * 10U + digit;
        cursor++;
    } while(*cursor != '\0');

    *index = value;
    return true;
}
