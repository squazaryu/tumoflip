#pragma once
#include "card_store.h"
#include <mbedtls/sha256.h>

#define FIELD_FILE_MAX (16UL * 1024UL * 1024UL)
typedef bool (*FieldProgress)(void* context, const char* path, uint32_t completed);

bool field_file_hash(Storage* storage, const char* path, uint32_t* size, uint8_t hash[32],
                     FieldProgress progress, void* context);
bool field_copy_checked(Storage* storage, File* source, File* destination, uint32_t size,
                        const uint8_t expected[32], FieldProgress progress, void* context);
bool field_record_load(Storage* storage, const char* root, uint32_t magic, void* data, size_t size);
bool field_record_save(Storage* storage, const char* root, uint32_t magic, const void* data, size_t size);
bool field_text_valid(const char* text, size_t capacity);
