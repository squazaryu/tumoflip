#include <gui/view_stack.h>
#include <stdint.h>
#include <furi.h>
#include <furi_hal.h>
#include <dolphin/dolphin.h>
#include <power/power_service/power.h>
#include <storage/storage.h>
#include <assets_icons.h>

#include "views/bubble_animation_view.h"
#include "views/one_shot_animation_view.h"
#include "desktop_profile.h"
#include "animation_storage.h"
#include "animation_manager.h"

#define TAG "AnimationManager"

#define PROFILE_RELOAD_DEBOUNCE_MS 100U

#define HARDCODED_ANIMATION_NAME   "L1_Tv_128x47"
#define NO_SD_ANIMATION_NAME       "L1_NoSd_128x49"
#define BAD_BATTERY_ANIMATION_NAME "L1_BadBattery_128x47"

#define NO_DB_ANIMATION_NAME    "L0_NoDb_128x51"
#define BAD_SD_ANIMATION_NAME   "L0_SdBad_128x51"
#define SD_OK_ANIMATION_NAME    "L0_SdOk_128x51"
#define URL_ANIMATION_NAME      "L0_Url_128x51"
#define NEW_MAIL_ANIMATION_NAME "L0_NewMail_128x51"

typedef enum {
    AnimationManagerStateIdle,
    AnimationManagerStateBlocked,
    AnimationManagerStateFreezedIdle,
    AnimationManagerStateFreezedBlocked,
} AnimationManagerState;

struct AnimationManager {
    AnimationManagerState state;
    FuriPubSubSubscription* pubsub_subscription_storage;
    FuriPubSubSubscription* pubsub_subscription_dolphin;
    BubbleAnimationView* animation_view;
    OneShotView* one_shot_view;
    FuriTimer* idle_animation_timer;
    FuriTimer* profile_reload_timer;
    StorageAnimation* current_animation;
    AnimationManagerInteractCallback interact_callback;
    AnimationManagerSetNewIdleAnimationCallback new_idle_callback;
    AnimationManagerSetNewIdleAnimationCallback check_blocking_callback;
    void* context;
    FuriString* freezed_animation_name;
    int32_t freezed_animation_time_left;
    ViewStack* view_stack;
    DesktopProfile* desktop_profile;
    size_t profile_sequential_index;

    bool blocking_shown_url       : 1;
    bool blocking_shown_sd_bad    : 1;
    bool blocking_shown_no_db     : 1;
    bool blocking_shown_sd_ok     : 1;
    bool levelup_pending          : 1;
    bool levelup_active           : 1;
    bool profile_read_in_progress : 1;
    bool profile_reload_pending   : 1;
    bool profile_reload_forced    : 1;
};

static StorageAnimation*
    animation_manager_select_idle_animation(AnimationManager* animation_manager);
static void animation_manager_replace_current_animation(
    AnimationManager* animation_manager,
    StorageAnimation* storage_animation);
static void animation_manager_start_new_idle(AnimationManager* animation_manager);
static bool animation_manager_check_blocking(AnimationManager* animation_manager);
static bool animation_manager_is_valid_idle_animation(
    const StorageAnimationManifestInfo* info,
    const DolphinStats* stats);
static bool animation_manager_is_idle_candidate(
    const AnimationManager* animation_manager,
    const StorageAnimationManifestInfo* info,
    const DolphinStats* stats);
static bool animation_manager_reload_profile(AnimationManager* animation_manager);
static bool animation_manager_profile_allows(
    const AnimationManager* animation_manager,
    const char* animation_name);
static uint32_t animation_manager_get_idle_duration(
    const AnimationManager* animation_manager,
    const BubbleAnimation* animation);
static void animation_manager_switch_to_one_shot_view(AnimationManager* animation_manager);
static void animation_manager_switch_to_animation_view(AnimationManager* animation_manager);

void animation_manager_set_context(AnimationManager* animation_manager, void* context) {
    furi_assert(animation_manager);
    animation_manager->context = context;
}

void animation_manager_set_new_idle_callback(
    AnimationManager* animation_manager,
    AnimationManagerSetNewIdleAnimationCallback callback) {
    furi_assert(animation_manager);
    animation_manager->new_idle_callback = callback;
}

void animation_manager_set_check_callback(
    AnimationManager* animation_manager,
    AnimationManagerCheckBlockingCallback callback) {
    furi_assert(animation_manager);
    animation_manager->check_blocking_callback = callback;
}

void animation_manager_set_interact_callback(
    AnimationManager* animation_manager,
    AnimationManagerInteractCallback callback) {
    furi_assert(animation_manager);
    animation_manager->interact_callback = callback;
}

static void animation_manager_storage_callback(const void* message, void* context) {
    furi_assert(context);
    const StorageEvent* storage_event = message;
    AnimationManager* animation_manager = context;

    switch(storage_event->type) {
    case StorageEventTypeCardMount:
    case StorageEventTypeCardUnmount:
    case StorageEventTypeCardMountError:
        animation_manager->profile_reload_pending = true;
        animation_manager->profile_reload_forced = true;
        if(animation_manager->check_blocking_callback) {
            animation_manager->check_blocking_callback(animation_manager->context);
        }
        break;

    case StorageEventTypeFileClose:
        if(animation_manager->profile_read_in_progress) {
            break;
        }
        animation_manager->profile_reload_pending = true;
        furi_timer_start(
            animation_manager->profile_reload_timer, furi_ms_to_ticks(PROFILE_RELOAD_DEBOUNCE_MS));
        break;

    default:
        break;
    }
}

static void animation_manager_profile_reload_timer_callback(void* context) {
    furi_assert(context);
    AnimationManager* animation_manager = context;
    if(animation_manager->check_blocking_callback) {
        animation_manager->check_blocking_callback(animation_manager->context);
    }
}

static void animation_manager_dolphin_callback(const void* message, void* context) {
    const DolphinPubsubEvent* dolphin_event = message;

    switch(*dolphin_event) {
    case DolphinPubsubEventUpdate:
        furi_assert(context);
        AnimationManager* animation_manager = context;
        if(animation_manager->check_blocking_callback) {
            animation_manager->check_blocking_callback(animation_manager->context);
        }
        break;
    default:
        break;
    }
}

static void animation_manager_timer_callback(void* context) {
    furi_assert(context);
    AnimationManager* animation_manager = context;
    if(animation_manager->new_idle_callback) {
        animation_manager->new_idle_callback(animation_manager->context);
    }
}

static void animation_manager_interact_callback(void* context) {
    furi_assert(context);
    AnimationManager* animation_manager = context;
    if(animation_manager->interact_callback) {
        animation_manager->interact_callback(animation_manager->context);
    }
}

/* reaction to animation_manager->check_blocking_callback() */
void animation_manager_check_blocking_process(AnimationManager* animation_manager) {
    furi_assert(animation_manager);

    const bool profile_changed = animation_manager_reload_profile(animation_manager);

    if(animation_manager->state == AnimationManagerStateIdle) {
        bool blocked = animation_manager_check_blocking(animation_manager);

        if(!blocked && profile_changed) {
            animation_manager_start_new_idle(animation_manager);
        } else if(!blocked) {
            Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
            DolphinStats stats = dolphin_stats(dolphin);
            furi_record_close(RECORD_DOLPHIN);

            const StorageAnimationManifestInfo* manifest_info =
                animation_storage_get_meta(animation_manager->current_animation);
            if(!animation_manager_is_idle_candidate(animation_manager, manifest_info, &stats)) {
                animation_manager_start_new_idle(animation_manager);
            }
        }
    }
}

/* reaction to animation_manager->new_idle_callback() */
void animation_manager_new_idle_process(AnimationManager* animation_manager) {
    furi_assert(animation_manager);

    if(animation_manager->state == AnimationManagerStateIdle) {
        animation_manager_reload_profile(animation_manager);
        animation_manager_start_new_idle(animation_manager);
    }
}

/* reaction to animation_manager->interact_callback() */
bool animation_manager_interact_process(AnimationManager* animation_manager) {
    furi_assert(animation_manager);
    bool consumed = true;

    if(animation_manager->levelup_pending) {
        animation_manager->levelup_pending = false;
        animation_manager->levelup_active = true;
        animation_manager_switch_to_one_shot_view(animation_manager);
        Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
        dolphin_upgrade_level(dolphin);
        furi_record_close(RECORD_DOLPHIN);
    } else if(animation_manager->levelup_active) {
        animation_manager->levelup_active = false;
        animation_manager_start_new_idle(animation_manager);
        animation_manager_switch_to_animation_view(animation_manager);
    } else if(animation_manager->state == AnimationManagerStateBlocked) {
        bool blocked = animation_manager_check_blocking(animation_manager);

        if(!blocked) {
            animation_manager_start_new_idle(animation_manager);
        }
    } else {
        consumed = false;
    }

    return consumed;
}

static void animation_manager_start_new_idle(AnimationManager* animation_manager) {
    furi_assert(animation_manager);

    StorageAnimation* new_animation = animation_manager_select_idle_animation(animation_manager);
    animation_manager_replace_current_animation(animation_manager, new_animation);
    const BubbleAnimation* bubble_animation =
        animation_storage_get_bubble_animation(animation_manager->current_animation);
    animation_manager->state = AnimationManagerStateIdle;
    furi_timer_start(
        animation_manager->idle_animation_timer,
        animation_manager_get_idle_duration(animation_manager, bubble_animation) * 1000);
}

static bool animation_manager_check_blocking(AnimationManager* animation_manager) {
    furi_assert(animation_manager);

    StorageAnimation* blocking_animation = NULL;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FS_Error sd_status = storage_sd_status(storage);

    if(sd_status == FSE_INTERNAL) {
        if(!animation_manager->blocking_shown_sd_bad) {
            blocking_animation = animation_storage_find_animation(BAD_SD_ANIMATION_NAME);
            furi_assert(blocking_animation);
            animation_manager->blocking_shown_sd_bad = true;
        }
    } else if(sd_status == FSE_NOT_READY) {
        animation_manager->blocking_shown_sd_bad = false;
        animation_manager->blocking_shown_sd_ok = false;
        animation_manager->blocking_shown_no_db = false;
    } else if(sd_status == FSE_OK) {
        if(!animation_manager->blocking_shown_sd_ok) {
            blocking_animation = animation_storage_find_animation(SD_OK_ANIMATION_NAME);
            furi_assert(blocking_animation);
            animation_manager->blocking_shown_sd_ok = true;
        } else if(!animation_manager->blocking_shown_no_db) {
            if(!storage_file_exists(storage, EXT_PATH("Manifest"))) {
                blocking_animation = animation_storage_find_animation(NO_DB_ANIMATION_NAME);
                furi_assert(blocking_animation);
                animation_manager->blocking_shown_no_db = true;
                animation_manager->blocking_shown_url = true;
            }
        } else if(animation_manager->blocking_shown_url) {
            blocking_animation = animation_storage_find_animation(URL_ANIMATION_NAME);
            furi_assert(blocking_animation);
            animation_manager->blocking_shown_url = false;
        }
    }

    Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
    DolphinStats stats = dolphin_stats(dolphin);
    furi_record_close(RECORD_DOLPHIN);
    if(!blocking_animation && stats.level_up_is_pending) {
        blocking_animation = animation_storage_find_animation(NEW_MAIL_ANIMATION_NAME);
        furi_check(blocking_animation);
        animation_manager->levelup_pending = true;
    }

    if(blocking_animation) {
        furi_timer_stop(animation_manager->idle_animation_timer);
        animation_manager_replace_current_animation(animation_manager, blocking_animation);
        /* no timer starting because this is blocking animation */
        animation_manager->state = AnimationManagerStateBlocked;
    }

    furi_record_close(RECORD_STORAGE);

    return !!blocking_animation;
}

static void animation_manager_replace_current_animation(
    AnimationManager* animation_manager,
    StorageAnimation* storage_animation) {
    furi_assert(storage_animation);
    StorageAnimation* previous_animation = animation_manager->current_animation;

    const BubbleAnimation* animation = animation_storage_get_bubble_animation(storage_animation);
    bubble_animation_view_set_animation(animation_manager->animation_view, animation);
    const char* new_name = animation_storage_get_meta(storage_animation)->name;
    FURI_LOG_I(TAG, "Select \'%s\' animation", new_name);
    animation_manager->current_animation = storage_animation;

    if(previous_animation) {
        animation_storage_free_storage_animation(&previous_animation);
    }
}

AnimationManager* animation_manager_alloc(void) {
    AnimationManager* animation_manager = malloc(sizeof(AnimationManager));
    animation_manager->desktop_profile = desktop_profile_load();
    animation_manager->profile_sequential_index = 0;
    animation_manager->profile_read_in_progress = false;
    animation_manager->profile_reload_pending = false;
    animation_manager->profile_reload_forced = false;
    animation_manager->animation_view = bubble_animation_view_alloc();
    animation_manager->view_stack = view_stack_alloc();
    View* animation_view = bubble_animation_get_view(animation_manager->animation_view);
    view_stack_add_view(animation_manager->view_stack, animation_view);
    animation_manager->freezed_animation_name = furi_string_alloc();

    animation_manager->idle_animation_timer =
        furi_timer_alloc(animation_manager_timer_callback, FuriTimerTypeOnce, animation_manager);
    animation_manager->profile_reload_timer = furi_timer_alloc(
        animation_manager_profile_reload_timer_callback, FuriTimerTypeOnce, animation_manager);
    bubble_animation_view_set_interact_callback(
        animation_manager->animation_view, animation_manager_interact_callback, animation_manager);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    animation_manager->pubsub_subscription_storage = furi_pubsub_subscribe(
        storage_get_pubsub(storage), animation_manager_storage_callback, animation_manager);
    furi_record_close(RECORD_STORAGE);

    Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
    animation_manager->pubsub_subscription_dolphin = furi_pubsub_subscribe(
        dolphin_get_pubsub(dolphin), animation_manager_dolphin_callback, animation_manager);
    furi_record_close(RECORD_DOLPHIN);

    animation_manager->blocking_shown_sd_ok = true;
    if(!animation_manager_check_blocking(animation_manager)) {
        animation_manager_start_new_idle(animation_manager);
    }

    return animation_manager;
}

void animation_manager_free(AnimationManager* animation_manager) {
    furi_assert(animation_manager);

    Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
    furi_pubsub_unsubscribe(
        dolphin_get_pubsub(dolphin), animation_manager->pubsub_subscription_dolphin);
    furi_record_close(RECORD_DOLPHIN);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    furi_pubsub_unsubscribe(
        storage_get_pubsub(storage), animation_manager->pubsub_subscription_storage);
    furi_record_close(RECORD_STORAGE);

    furi_string_free(animation_manager->freezed_animation_name);
    desktop_profile_free(animation_manager->desktop_profile);
    View* animation_view = bubble_animation_get_view(animation_manager->animation_view);
    view_stack_remove_view(animation_manager->view_stack, animation_view);
    bubble_animation_view_free(animation_manager->animation_view);
    furi_timer_free(animation_manager->profile_reload_timer);
    furi_timer_free(animation_manager->idle_animation_timer);
}

View* animation_manager_get_animation_view(AnimationManager* animation_manager) {
    furi_assert(animation_manager);

    return view_stack_get_view(animation_manager->view_stack);
}

static bool animation_manager_is_valid_idle_animation(
    const StorageAnimationManifestInfo* info,
    const DolphinStats* stats) {
    furi_assert(info);
    furi_assert(info->name);

    bool result = true;

    if(!strcmp(info->name, BAD_BATTERY_ANIMATION_NAME)) {
        Power* power = furi_record_open(RECORD_POWER);
        bool battery_is_well = power_is_battery_healthy(power);
        furi_record_close(RECORD_POWER);

        result = !battery_is_well;
    }
    if(!strcmp(info->name, NO_SD_ANIMATION_NAME)) {
        Storage* storage = furi_record_open(RECORD_STORAGE);
        FS_Error sd_status = storage_sd_status(storage);
        furi_record_close(RECORD_STORAGE);

        result = (sd_status == FSE_NOT_READY);
    }
    if((stats->butthurt < info->min_butthurt) || (stats->butthurt > info->max_butthurt)) {
        result = false;
    }
    // Tumoflip unlocks default idle animations regardless of Dolphin level.

    return result;
}

static bool animation_manager_profile_allows(
    const AnimationManager* animation_manager,
    const char* animation_name) {
    furi_assert(animation_manager);
    furi_assert(animation_name);

    return !desktop_profile_is_active(animation_manager->desktop_profile) ||
           desktop_profile_contains(animation_manager->desktop_profile, animation_name);
}

static bool animation_manager_is_idle_candidate(
    const AnimationManager* animation_manager,
    const StorageAnimationManifestInfo* info,
    const DolphinStats* stats) {
    if(!animation_manager_profile_allows(animation_manager, info->name)) return false;

    if(desktop_profile_is_active(animation_manager->desktop_profile) &&
       strcmp(info->name, BAD_BATTERY_ANIMATION_NAME) != 0 &&
       strcmp(info->name, NO_SD_ANIMATION_NAME) != 0) {
        return true;
    }

    return animation_manager_is_valid_idle_animation(info, stats);
}

static bool animation_manager_reload_profile(AnimationManager* animation_manager) {
    furi_assert(animation_manager);

    if(!animation_manager->profile_reload_pending) return false;

    const bool force_reload = animation_manager->profile_reload_forced;
    animation_manager->profile_reload_pending = false;
    animation_manager->profile_reload_forced = false;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    const bool marker_exists = storage_file_exists(storage, DESKTOP_PROFILE_RELOAD_PATH);
    if(marker_exists) {
        storage_common_remove(storage, DESKTOP_PROFILE_RELOAD_PATH);
    }
    furi_record_close(RECORD_STORAGE);

    if(!force_reload && !marker_exists) return false;

    animation_manager->profile_read_in_progress = true;
    DesktopProfile* loaded = desktop_profile_load();
    animation_manager->profile_read_in_progress = false;

    if(desktop_profile_equal(animation_manager->desktop_profile, loaded)) {
        desktop_profile_free(loaded);
        return false;
    }

    desktop_profile_free(animation_manager->desktop_profile);
    animation_manager->desktop_profile = loaded;
    animation_manager->profile_sequential_index = 0;
    FURI_LOG_I(
        TAG,
        "Desktop profile %s (%u animations)",
        desktop_profile_is_active(loaded) ? "enabled" : "disabled",
        (unsigned int)desktop_profile_get_animation_count(loaded));
    return true;
}

static uint32_t animation_manager_get_idle_duration(
    const AnimationManager* animation_manager,
    const BubbleAnimation* animation) {
    furi_assert(animation_manager);
    furi_assert(animation);

    if(desktop_profile_is_active(animation_manager->desktop_profile) &&
       (desktop_profile_get_timing(animation_manager->desktop_profile) ==
        DesktopProfileTimingCustom)) {
        return desktop_profile_get_duration_seconds(animation_manager->desktop_profile);
    }
    return animation->duration;
}

static void animation_manager_release_list(StorageAnimationList_t* animation_list) {
    for
        M_EACH(item, *animation_list, StorageAnimationList_t) {
            animation_storage_free_storage_animation(item);
        }
    StorageAnimationList_clear(*animation_list);
}

static uint32_t animation_manager_filter_animation_list(
    AnimationManager* animation_manager,
    StorageAnimationList_t* animation_list,
    const DolphinStats* stats,
    bool use_profile) {
    uint32_t whole_weight = 0;

    StorageAnimationList_it_t it;
    for(StorageAnimationList_it(it, *animation_list); !StorageAnimationList_end_p(it);) {
        StorageAnimation* storage_animation = *StorageAnimationList_ref(it);
        const StorageAnimationManifestInfo* manifest_info =
            animation_storage_get_meta(storage_animation);
        const bool valid =
            use_profile ?
                animation_manager_is_idle_candidate(animation_manager, manifest_info, stats) :
                animation_manager_is_valid_idle_animation(manifest_info, stats);

        if(valid) {
            whole_weight += manifest_info->weight ? manifest_info->weight : 1U;
            StorageAnimationList_next(it);
        } else {
            animation_storage_free_storage_animation(&storage_animation);
            StorageAnimationList_remove(*animation_list, it);
        }
    }

    return whole_weight;
}

static StorageAnimation* animation_manager_select_sequential(
    AnimationManager* animation_manager,
    StorageAnimationList_t* animation_list) {
    if(desktop_profile_selects_all(animation_manager->desktop_profile)) {
        const size_t animation_count = StorageAnimationList_size(*animation_list);
        if(animation_count == 0U) return NULL;

        const size_t selected_index =
            animation_manager->profile_sequential_index % animation_count;
        size_t index = 0U;
        for
            M_EACH(item, *animation_list, StorageAnimationList_t) {
                if(index++ == selected_index) {
                    animation_manager->profile_sequential_index =
                        (selected_index + 1U) % animation_count;
                    return *item;
                }
            }
        return NULL;
    }

    const size_t profile_count =
        desktop_profile_get_animation_count(animation_manager->desktop_profile);
    if(profile_count == 0U) return NULL;

    for(size_t offset = 0; offset < profile_count; ++offset) {
        const size_t profile_index =
            (animation_manager->profile_sequential_index + offset) % profile_count;
        const char* requested =
            desktop_profile_get_animation(animation_manager->desktop_profile, profile_index);

        for
            M_EACH(item, *animation_list, StorageAnimationList_t) {
                if(strcmp(animation_storage_get_meta(*item)->name, requested) == 0) {
                    animation_manager->profile_sequential_index =
                        (profile_index + 1U) % profile_count;
                    return *item;
                }
            }
    }

    return NULL;
}

static StorageAnimation* animation_manager_select_weighted(
    StorageAnimationList_t* animation_list,
    uint32_t whole_weight) {
    if(whole_weight == 0U) return NULL;

    const uint32_t lucky_number = furi_hal_random_get() % whole_weight;
    uint32_t accumulated_weight = 0;

    for
        M_EACH(item, *animation_list, StorageAnimationList_t) {
            const uint8_t configured_weight = animation_storage_get_meta(*item)->weight;
            accumulated_weight += configured_weight ? configured_weight : 1U;
            if(lucky_number < accumulated_weight) return *item;
        }

    return NULL;
}

static StorageAnimation*
    animation_manager_select_idle_animation(AnimationManager* animation_manager) {
    StorageAnimationList_t animation_list;
    StorageAnimationList_init(animation_list);
    animation_storage_fill_animation_list(&animation_list);

    Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
    DolphinStats stats = dolphin_stats(dolphin);
    furi_record_close(RECORD_DOLPHIN);
    const bool profile_active = desktop_profile_is_active(animation_manager->desktop_profile);
    uint32_t whole_weight = animation_manager_filter_animation_list(
        animation_manager, &animation_list, &stats, profile_active);

    bool using_profile = profile_active;
    if((whole_weight == 0U) && profile_active) {
        FURI_LOG_W(TAG, "Profile has no available animations; using manifest");
        animation_manager_release_list(&animation_list);
        animation_storage_fill_animation_list(&animation_list);
        whole_weight = animation_manager_filter_animation_list(
            animation_manager, &animation_list, &stats, false);
        using_profile = false;
    }

    StorageAnimation* selected = NULL;
    if(using_profile && (desktop_profile_get_order(animation_manager->desktop_profile) ==
                         DesktopProfileOrderSequential)) {
        selected = animation_manager_select_sequential(animation_manager, &animation_list);
    }
    if(!selected) {
        selected = animation_manager_select_weighted(&animation_list, whole_weight);
    }

    for
        M_EACH(item, animation_list, StorageAnimationList_t) {
            if(*item != selected) {
                animation_storage_free_storage_animation(item);
            }
        }

    StorageAnimationList_clear(animation_list);

    if(!selected) {
        selected = animation_storage_find_animation(HARDCODED_ANIMATION_NAME);
    }

    /* cache animation, if failed - choose reliable animation */
    if(!animation_storage_get_bubble_animation(selected)) {
        const char* name = animation_storage_get_meta(selected)->name;
        FURI_LOG_E(TAG, "Can't upload animation described in manifest: \'%s\'", name);
        animation_storage_free_storage_animation(&selected);
        selected = animation_storage_find_animation(HARDCODED_ANIMATION_NAME);
    }

    furi_assert(selected);
    return selected;
}

bool animation_manager_is_animation_loaded(AnimationManager* animation_manager) {
    furi_assert(animation_manager);
    return animation_manager->current_animation;
}

void animation_manager_unload_and_stall_animation(AnimationManager* animation_manager) {
    furi_assert(animation_manager);
    furi_assert(animation_manager->current_animation);
    furi_assert(!furi_string_size(animation_manager->freezed_animation_name));
    furi_assert(
        (animation_manager->state == AnimationManagerStateIdle) ||
        (animation_manager->state == AnimationManagerStateBlocked));

    if(animation_manager->state == AnimationManagerStateBlocked) {
        animation_manager->state = AnimationManagerStateFreezedBlocked;
    } else if(animation_manager->state == AnimationManagerStateIdle) { //-V547
        animation_manager->state = AnimationManagerStateFreezedIdle;

        animation_manager->freezed_animation_time_left =
            furi_timer_get_expire_time(animation_manager->idle_animation_timer) - furi_get_tick();
        if(animation_manager->freezed_animation_time_left < 0) {
            animation_manager->freezed_animation_time_left = 0;
        }
        furi_timer_stop(animation_manager->idle_animation_timer);
    } else {
        furi_crash();
    }

    FURI_LOG_I(
        TAG,
        "Unload animation \'%s\'",
        animation_storage_get_meta(animation_manager->current_animation)->name);

    StorageAnimationManifestInfo* meta =
        animation_storage_get_meta(animation_manager->current_animation);
    /* copy str, not move, because it can be internal animation */
    furi_string_set(animation_manager->freezed_animation_name, meta->name);

    bubble_animation_freeze(animation_manager->animation_view);
    animation_storage_free_storage_animation(&animation_manager->current_animation);
}

void animation_manager_load_and_continue_animation(AnimationManager* animation_manager) {
    furi_assert(animation_manager);
    furi_assert(!animation_manager->current_animation);
    furi_assert(furi_string_size(animation_manager->freezed_animation_name));
    furi_assert(
        (animation_manager->state == AnimationManagerStateFreezedIdle) ||
        (animation_manager->state == AnimationManagerStateFreezedBlocked));

    const bool profile_changed = animation_manager_reload_profile(animation_manager);

    if(animation_manager->state == AnimationManagerStateFreezedBlocked) {
        StorageAnimation* restore_animation = animation_storage_find_animation(
            furi_string_get_cstr(animation_manager->freezed_animation_name));
        /* all blocked animations must be in flipper -> we can
         * always find blocking animation */
        furi_assert(restore_animation);
        animation_manager_replace_current_animation(animation_manager, restore_animation);
        animation_manager->state = AnimationManagerStateBlocked;
    } else if(animation_manager->state == AnimationManagerStateFreezedIdle) { //-V547
        /* check if we missed some system notifications, and set current_animation */
        bool blocked = animation_manager_check_blocking(animation_manager);
        if(!blocked && !profile_changed) {
            /* if no blocking - try restore last one idle */
            StorageAnimation* restore_animation = animation_storage_find_animation(
                furi_string_get_cstr(animation_manager->freezed_animation_name));
            if(restore_animation) {
                Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
                DolphinStats stats = dolphin_stats(dolphin);
                furi_record_close(RECORD_DOLPHIN);
                const StorageAnimationManifestInfo* manifest_info =
                    animation_storage_get_meta(restore_animation);
                bool valid =
                    animation_manager_is_idle_candidate(animation_manager, manifest_info, &stats);
                if(valid) {
                    animation_manager_replace_current_animation(
                        animation_manager, restore_animation);
                    animation_manager->state = AnimationManagerStateIdle;

                    if(animation_manager->freezed_animation_time_left) {
                        furi_timer_start(
                            animation_manager->idle_animation_timer,
                            animation_manager->freezed_animation_time_left);
                    } else {
                        const BubbleAnimation* animation = animation_storage_get_bubble_animation(
                            animation_manager->current_animation);
                        furi_timer_start(
                            animation_manager->idle_animation_timer,
                            animation_manager_get_idle_duration(animation_manager, animation) *
                                1000);
                    }
                }
            } else {
                FURI_LOG_E(
                    TAG,
                    "Failed to restore \'%s\'",
                    furi_string_get_cstr(animation_manager->freezed_animation_name));
            }
        }
    } else {
        /* Unknown state is an error. But not in release version.*/
        furi_crash();
    }

    /* if can't restore previous animation - select new */
    if(!animation_manager->current_animation) {
        animation_manager_start_new_idle(animation_manager);
    }
    FURI_LOG_I(
        TAG,
        "Load animation \'%s\'",
        animation_storage_get_meta(animation_manager->current_animation)->name);

    bubble_animation_unfreeze(animation_manager->animation_view);
    furi_string_reset(animation_manager->freezed_animation_name);
    furi_assert(animation_manager->current_animation);
}

static void animation_manager_switch_to_one_shot_view(AnimationManager* animation_manager) {
    furi_assert(animation_manager);
    furi_assert(!animation_manager->one_shot_view);
    Dolphin* dolphin = furi_record_open(RECORD_DOLPHIN);
    DolphinStats stats = dolphin_stats(dolphin);
    furi_record_close(RECORD_DOLPHIN);

    animation_manager->one_shot_view = one_shot_view_alloc();
    one_shot_view_set_interact_callback(
        animation_manager->one_shot_view, animation_manager_interact_callback, animation_manager);
    View* prev_view = bubble_animation_get_view(animation_manager->animation_view);
    View* next_view = one_shot_view_get_view(animation_manager->one_shot_view);
    view_stack_remove_view(animation_manager->view_stack, prev_view);
    view_stack_add_view(animation_manager->view_stack, next_view);
    if(stats.level == 1) {
        one_shot_view_start_animation(animation_manager->one_shot_view, &A_Levelup1_128x64);
    } else if((stats.level > 1) && (stats.level < DOLPHIN_LEVEL_MAX)) {
        one_shot_view_start_animation(animation_manager->one_shot_view, &A_Levelup2_128x64);
    } else {
        furi_crash();
    }
}

static void animation_manager_switch_to_animation_view(AnimationManager* animation_manager) {
    furi_assert(animation_manager);
    furi_assert(animation_manager->one_shot_view);

    View* prev_view = one_shot_view_get_view(animation_manager->one_shot_view);
    View* next_view = bubble_animation_get_view(animation_manager->animation_view);
    view_stack_remove_view(animation_manager->view_stack, prev_view);
    view_stack_add_view(animation_manager->view_stack, next_view);
    one_shot_view_free(animation_manager->one_shot_view);
    animation_manager->one_shot_view = NULL;
}
