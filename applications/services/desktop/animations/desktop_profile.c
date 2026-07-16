#include "desktop_profile.h"

#include <stdlib.h>
#include <string.h>
#include <flipper_format/flipper_format.h>
#include <furi.h>
#include <storage/storage.h>

#define TAG "DesktopProfile"

#define DESKTOP_PROFILE_FILETYPE             "Tumoflip Desktop Profile"
#define DESKTOP_PROFILE_VERSION_LEGACY       1U
#define DESKTOP_PROFILE_VERSION              2U
#define DESKTOP_PROFILE_MAX_ANIMATIONS       320U
#define DESKTOP_PROFILE_MAX_ANIMATION_NAME   96U
#define DESKTOP_PROFILE_MAX_COLLECTION_BYTES 64U
#define DESKTOP_PROFILE_MIN_DURATION_SECONDS 5U
#define DESKTOP_PROFILE_MAX_DURATION_SECONDS 86399U
#define DESKTOP_PROFILE_DEFAULT_DURATION     60U

struct DesktopProfile {
    bool enabled;
    bool selection_all;
    DesktopProfileOrder order;
    DesktopProfileTiming timing;
    uint32_t duration_seconds;
    char* collection;
    size_t animation_count;
    size_t animation_capacity;
    char** animations;
};

static DesktopProfile* desktop_profile_alloc(void) {
    DesktopProfile* profile = malloc(sizeof(DesktopProfile));
    memset(profile, 0, sizeof(DesktopProfile));
    profile->order = DesktopProfileOrderRandom;
    profile->timing = DesktopProfileTimingOriginal;
    profile->duration_seconds = DESKTOP_PROFILE_DEFAULT_DURATION;
    profile->collection = strdup("");
    return profile;
}

void desktop_profile_free(DesktopProfile* profile) {
    if(!profile) return;

    free(profile->collection);
    for(size_t i = 0; i < profile->animation_count; ++i) {
        free(profile->animations[i]);
    }
    free(profile->animations);
    free(profile);
}

static bool desktop_profile_name_is_valid(const char* name) {
    const size_t length = strlen(name);
    if((length == 0U) || (length > DESKTOP_PROFILE_MAX_ANIMATION_NAME)) return false;

    for(size_t i = 0; i < length; ++i) {
        const unsigned char character = name[i];
        if(!(((character >= '0') && (character <= '9')) ||
             ((character >= 'A') && (character <= 'Z')) ||
             ((character >= 'a') && (character <= 'z')) || (character == '_') ||
             (character == '-'))) {
            return false;
        }
    }
    return true;
}

static bool desktop_profile_collection_is_valid(const char* name) {
    const size_t length = strlen(name);
    if((length == 0U) || (length > DESKTOP_PROFILE_MAX_COLLECTION_BYTES)) return false;

    for(size_t i = 0; i < length; ++i) {
        const unsigned char character = name[i];
        if((character < 0x20U) || (character == 0x7FU)) return false;
    }
    return true;
}

static bool desktop_profile_add_animation(DesktopProfile* profile, const char* name) {
    if((profile->animation_count >= DESKTOP_PROFILE_MAX_ANIMATIONS) ||
       !desktop_profile_name_is_valid(name)) {
        return false;
    }

    if(desktop_profile_contains(profile, name)) return false;

    if(profile->animation_count == profile->animation_capacity) {
        size_t next_capacity = profile->animation_capacity * 2U;
        if(next_capacity == 0U) next_capacity = 16U;
        const size_t bounded_capacity =
            MIN(next_capacity, (size_t)DESKTOP_PROFILE_MAX_ANIMATIONS);
        char** animations = realloc(profile->animations, bounded_capacity * sizeof(char*));
        if(!animations) return false;
        profile->animations = animations;
        profile->animation_capacity = bounded_capacity;
    }

    profile->animations[profile->animation_count++] = strdup(name);
    return true;
}

static bool desktop_profile_parse(DesktopProfile* profile, FlipperFormat* file) {
    FuriString* value = furi_string_alloc();
    uint32_t version = 0;
    bool valid = false;

    do {
        if(!flipper_format_read_header(file, value, &version)) break;
        if(furi_string_cmp_str(value, DESKTOP_PROFILE_FILETYPE) != 0) break;
        if((version != DESKTOP_PROFILE_VERSION_LEGACY) &&
           (version != DESKTOP_PROFILE_VERSION)) {
            break;
        }

        flipper_format_set_strict_mode(file, false);

        if(!flipper_format_rewind(file) ||
           !flipper_format_read_bool(file, "Enabled", &profile->enabled, 1)) {
            break;
        }

        if(!flipper_format_rewind(file) ||
           !flipper_format_read_string(file, "Collection", value)) {
            break;
        }
        free(profile->collection);
        profile->collection = strdup(furi_string_get_cstr(value));
        if(!desktop_profile_collection_is_valid(profile->collection)) break;

        if(!flipper_format_rewind(file) || !flipper_format_read_string(file, "Order", value)) {
            break;
        }
        if(furi_string_cmp_str(value, "Random") == 0) {
            profile->order = DesktopProfileOrderRandom;
        } else if(furi_string_cmp_str(value, "Sequential") == 0) {
            profile->order = DesktopProfileOrderSequential;
        } else {
            break;
        }

        if(!flipper_format_rewind(file) || !flipper_format_read_string(file, "Timing", value)) {
            break;
        }
        if(furi_string_cmp_str(value, "Original") == 0) {
            profile->timing = DesktopProfileTimingOriginal;
        } else if(furi_string_cmp_str(value, "Custom") == 0) {
            profile->timing = DesktopProfileTimingCustom;
        } else {
            break;
        }

        if(!flipper_format_rewind(file) ||
           !flipper_format_read_uint32(file, "Duration", &profile->duration_seconds, 1)) {
            break;
        }
        if((profile->duration_seconds < DESKTOP_PROFILE_MIN_DURATION_SECONDS) ||
           (profile->duration_seconds > DESKTOP_PROFILE_MAX_DURATION_SECONDS)) {
            break;
        }

        if(version == DESKTOP_PROFILE_VERSION) {
            if(!flipper_format_rewind(file) ||
               !flipper_format_read_string(file, "Selection", value)) {
                break;
            }
            if(furi_string_cmp_str(value, "Explicit") == 0) {
                profile->selection_all = false;
            } else if(furi_string_cmp_str(value, "All") == 0) {
                profile->selection_all = true;
            } else {
                break;
            }
        }

        bool animations_valid = true;
        if(!flipper_format_rewind(file)) break;
        while(flipper_format_read_string(file, "Animation", value)) {
            if(!desktop_profile_add_animation(profile, furi_string_get_cstr(value))) {
                animations_valid = false;
                break;
            }
        }
        if(!animations_valid) break;

        if(profile->selection_all && (profile->animation_count != 0U)) break;

        if(profile->enabled &&
           ((!profile->selection_all && (profile->animation_count == 0U)) ||
            (strlen(profile->collection) == 0U))) {
            break;
        }

        valid = true;
    } while(false);

    furi_string_free(value);
    return valid;
}

DesktopProfile* desktop_profile_load(void) {
    DesktopProfile* profile = desktop_profile_alloc();
    Storage* storage = furi_record_open(RECORD_STORAGE);

    if(storage_sd_status(storage) != FSE_OK ||
       !storage_file_exists(storage, DESKTOP_PROFILE_PATH)) {
        furi_record_close(RECORD_STORAGE);
        return profile;
    }

    FlipperFormat* file = flipper_format_file_alloc(storage);
    bool valid = false;
    if(flipper_format_file_open_existing(file, DESKTOP_PROFILE_PATH)) {
        valid = desktop_profile_parse(profile, file);
    }
    flipper_format_free(file);
    furi_record_close(RECORD_STORAGE);

    if(!valid) {
        FURI_LOG_W(TAG, "Ignoring invalid profile");
        desktop_profile_free(profile);
        profile = desktop_profile_alloc();
    }

    return profile;
}

bool desktop_profile_equal(const DesktopProfile* left, const DesktopProfile* right) {
    furi_assert(left);
    furi_assert(right);

    if((left->enabled != right->enabled) || (left->selection_all != right->selection_all) ||
       (left->order != right->order) ||
       (left->timing != right->timing) || (left->duration_seconds != right->duration_seconds) ||
       (left->animation_count != right->animation_count) ||
       (strcmp(left->collection, right->collection) != 0)) {
        return false;
    }

    for(size_t i = 0; i < left->animation_count; ++i) {
        if(strcmp(left->animations[i], right->animations[i]) != 0) return false;
    }
    return true;
}

bool desktop_profile_is_active(const DesktopProfile* profile) {
    furi_assert(profile);
    return profile->enabled && (profile->selection_all || (profile->animation_count > 0U));
}

bool desktop_profile_contains(const DesktopProfile* profile, const char* animation_name) {
    furi_assert(profile);
    furi_assert(animation_name);

    if(profile->selection_all) return true;

    for(size_t i = 0; i < profile->animation_count; ++i) {
        if(strcmp(profile->animations[i], animation_name) == 0) return true;
    }
    return false;
}

bool desktop_profile_selects_all(const DesktopProfile* profile) {
    furi_assert(profile);
    return profile->selection_all;
}

DesktopProfileOrder desktop_profile_get_order(const DesktopProfile* profile) {
    furi_assert(profile);
    return profile->order;
}

DesktopProfileTiming desktop_profile_get_timing(const DesktopProfile* profile) {
    furi_assert(profile);
    return profile->timing;
}

uint32_t desktop_profile_get_duration_seconds(const DesktopProfile* profile) {
    furi_assert(profile);
    return profile->duration_seconds;
}

size_t desktop_profile_get_animation_count(const DesktopProfile* profile) {
    furi_assert(profile);
    return profile->animation_count;
}

const char* desktop_profile_get_animation(const DesktopProfile* profile, size_t index) {
    furi_assert(profile);
    furi_assert(index < profile->animation_count);
    return profile->animations[index];
}
