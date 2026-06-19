#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lib/subghz/subghz_keystore.h>

typedef struct ArfSubGhzKeeloqKeys ArfSubGhzKeeloqKeys;

ArfSubGhzKeeloqKeys* arf_subghz_keeloq_keys_alloc(void);
void arf_subghz_keeloq_keys_free(ArfSubGhzKeeloqKeys* instance);

size_t arf_subghz_keeloq_keys_count(ArfSubGhzKeeloqKeys* instance);
size_t arf_subghz_keeloq_keys_user_count(ArfSubGhzKeeloqKeys* instance);
SubGhzKey* arf_subghz_keeloq_keys_get(ArfSubGhzKeeloqKeys* instance, size_t index);

void arf_subghz_keeloq_keys_add(
    ArfSubGhzKeeloqKeys* instance,
    uint64_t key,
    uint16_t type,
    const char* name);

void arf_subghz_keeloq_keys_set(
    ArfSubGhzKeeloqKeys* instance,
    size_t index,
    uint64_t key,
    uint16_t type,
    const char* name);

void arf_subghz_keeloq_keys_delete(ArfSubGhzKeeloqKeys* instance, size_t index);
bool arf_subghz_keeloq_keys_save(ArfSubGhzKeeloqKeys* instance);
