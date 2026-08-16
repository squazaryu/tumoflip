#include "host_string_compat.h"

#if defined(__linux__)

#include <string.h>

size_t esp_flasher_test_strlcpy(char* destination, const char* source, size_t size) {
    const size_t source_length = strlen(source);
    if(size > 0U) {
        const size_t copy_length = source_length < (size - 1U) ? source_length : (size - 1U);
        memcpy(destination, source, copy_length);
        destination[copy_length] = '\0';
    }
    return source_length;
}

size_t esp_flasher_test_strlcat(char* destination, const char* source, size_t size) {
    size_t destination_length = 0U;
    while(destination_length < size && destination[destination_length] != '\0') {
        destination_length++;
    }

    const size_t source_length = strlen(source);
    if(destination_length == size) return size + source_length;

    const size_t available = size - destination_length - 1U;
    const size_t copy_length = source_length < available ? source_length : available;
    memcpy(destination + destination_length, source, copy_length);
    destination[destination_length + copy_length] = '\0';
    return destination_length + source_length;
}

#endif
