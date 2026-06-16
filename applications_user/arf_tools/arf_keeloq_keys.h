#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <lib/subghz/subghz_keystore.h>

typedef struct ArfKeeloqKeys ArfKeeloqKeys;

ArfKeeloqKeys* arf_keeloq_keys_alloc(void);
void arf_keeloq_keys_free(ArfKeeloqKeys* keys);

size_t arf_keeloq_keys_count(ArfKeeloqKeys* keys);
size_t arf_keeloq_keys_user_count(ArfKeeloqKeys* keys);
SubGhzKey* arf_keeloq_keys_get(ArfKeeloqKeys* keys, size_t index);
