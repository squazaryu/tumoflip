#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <storage/storage.h>

#define DESKTOP_PROFILE_PATH        EXT_PATH("apps_data/tumoflip_customization/desktop_profile.txt")
#define DESKTOP_PROFILE_RELOAD_PATH EXT_PATH("apps_data/tumoflip_customization/reload.flag")
#define DESKTOP_PACK_MANIFEST_PATH  EXT_PATH("apps_data/tumoflip_customization/animation_packs.txt")

typedef enum {
    DesktopProfileOrderRandom,
    DesktopProfileOrderSequential,
} DesktopProfileOrder;

typedef enum {
    DesktopProfileTimingOriginal,
    DesktopProfileTimingCustom,
} DesktopProfileTiming;

typedef struct DesktopProfile DesktopProfile;

DesktopProfile* desktop_profile_load(void);
void desktop_profile_free(DesktopProfile* profile);

bool desktop_profile_equal(const DesktopProfile* left, const DesktopProfile* right);
bool desktop_profile_is_active(const DesktopProfile* profile);
bool desktop_profile_selects_all(const DesktopProfile* profile);
bool desktop_profile_contains(const DesktopProfile* profile, const char* animation_name);

DesktopProfileOrder desktop_profile_get_order(const DesktopProfile* profile);
DesktopProfileTiming desktop_profile_get_timing(const DesktopProfile* profile);
uint32_t desktop_profile_get_duration_seconds(const DesktopProfile* profile);
size_t desktop_profile_get_animation_count(const DesktopProfile* profile);
const char* desktop_profile_get_animation(const DesktopProfile* profile, size_t index);
