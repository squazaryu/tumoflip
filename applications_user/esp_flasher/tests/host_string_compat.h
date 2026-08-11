#pragma once

#if defined(__linux__)

#include <stddef.h>

size_t esp_flasher_test_strlcpy(char* destination, const char* source, size_t size);
size_t esp_flasher_test_strlcat(char* destination, const char* source, size_t size);

#define strlcpy esp_flasher_test_strlcpy
#define strlcat esp_flasher_test_strlcat

#endif
