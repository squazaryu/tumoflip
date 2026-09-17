#include <furi.h>
#include <dialogs/dialogs.h>
#include <flipper_format/flipper_format.h>
#include <gui/gui.h>
#include <gui/modules/popup.h>
#include <gui/view_dispatcher.h>
#include <storage/storage.h>

#include "hitag2_seed.h"

#define TAG "RenaultSeedBF"
#define SEED_BF_EXT ".sub"
#define SEED_BF_ROOT EXT_PATH("subghz")
#define EVENT_DONE 1U
#define EVENT_BACK 2U
#define EVENT_PROGRESS 3U

typedef struct {
    Storage* storage;
    DialogsApp* dialogs;
    ViewDispatcher* dispatcher;
    Popup* popup;
    FuriString* path;
    FuriThread* thread;
    volatile bool cancel;
    bool ok;
    uint32_t seed;
    uint8_t frame[11];
    char body[32];
} RenaultSeedBf;

static void renault_seed_bf_send_back(void* context) {
    RenaultSeedBf* app = context;
    view_dispatcher_send_custom_event(app->dispatcher, EVENT_BACK);
}

static bool renault_seed_bf_progress(
    uint8_t progress,
    uint32_t candidates,
    void* context) {
    UNUSED(candidates);
    RenaultSeedBf* app = context;
    if(app->cancel) return false;
    view_dispatcher_send_custom_event(app->dispatcher, EVENT_PROGRESS | ((uint32_t)progress << 8));
    furi_delay_ms(1);
    return !app->cancel;
}

static int32_t renault_seed_bf_worker(void* context) {
    RenaultSeedBf* app = context;
    uint8_t iv[4] = {0};
    app->ok = hitag2_seed_recover_ex(app->frame, iv, renault_seed_bf_progress, app);
    if(app->ok) app->seed = hitag2_seed_from_iv(iv);
    view_dispatcher_send_custom_event(app->dispatcher, EVENT_DONE);
    return 0;
}

static bool renault_seed_bf_load_frame(RenaultSeedBf* app) {
    FlipperFormat* file = flipper_format_file_alloc(app->storage);
    FuriString* protocol = furi_string_alloc();
    uint8_t key[8] = {0};
    uint32_t key2 = 0;
    bool ok = false;

    do {
        if(!flipper_format_file_open_existing(file, furi_string_get_cstr(app->path))) break;
        flipper_format_rewind(file);
        if(!flipper_format_read_string(file, "Protocol", protocol) ||
           !furi_string_equal_str(protocol, "Renault V1")) {
            break;
        }
        flipper_format_rewind(file);
        if(!flipper_format_read_hex(file, "Key", key, sizeof(key))) break;
        flipper_format_rewind(file);
        if(!flipper_format_read_uint32(file, "Key2", &key2, 1)) break;

        memcpy(app->frame, key, sizeof(key));
        const uint32_t packed = key2 & 0x3FFFFU;
        app->frame[8] = (uint8_t)(packed >> 10U);
        app->frame[9] = (uint8_t)(packed >> 2U);
        app->frame[10] = (uint8_t)((packed & 0x03U) << 6U);
        app->frame[10] = (uint8_t)(hitag2_seed_frame_xor(app->frame) | (app->frame[10] & 0xC0U));
        ok = true;
    } while(false);

    furi_string_free(protocol);
    flipper_format_free(file);
    return ok;
}

static bool renault_seed_bf_save(RenaultSeedBf* app) {
    FlipperFormat* file = flipper_format_file_alloc(app->storage);
    uint8_t seed[4] = {
        (uint8_t)(app->seed >> 24U),
        (uint8_t)(app->seed >> 16U),
        (uint8_t)(app->seed >> 8U),
        (uint8_t)app->seed,
    };
    uint32_t recovered = 1U;
    bool ok = flipper_format_file_open_existing(file, furi_string_get_cstr(app->path)) &&
              flipper_format_insert_or_update_hex(file, "Seed", seed, sizeof(seed)) &&
              flipper_format_insert_or_update_uint32(file, "Recovered", &recovered, 1);
    flipper_format_free(file);
    return ok;
}

static void renault_seed_bf_popup(RenaultSeedBf* app, const char* header, const char* body) {
    popup_reset(app->popup);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, renault_seed_bf_send_back);
    popup_set_header(app->popup, header, 64, 8, AlignCenter, AlignTop);
    strncpy(app->body, body, sizeof(app->body) - 1U);
    app->body[sizeof(app->body) - 1U] = '\0';
    popup_set_text(app->popup, app->body, 64, 34, AlignCenter, AlignTop);
    popup_disable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->dispatcher, 0);
}

static bool renault_seed_bf_custom_event(void* context, uint32_t event) {
    RenaultSeedBf* app = context;
    const uint32_t kind = event & 0xFFU;
    if(kind == EVENT_PROGRESS) {
        snprintf(app->body, sizeof(app->body), "Running... %u%%", (unsigned)((event >> 8) & 0xFFU));
        popup_set_text(app->popup, app->body, 64, 34, AlignCenter, AlignTop);
        return true;
    }
    if(kind == EVENT_DONE) {
        if(app->thread) {
            furi_thread_join(app->thread);
            furi_thread_free(app->thread);
            app->thread = NULL;
        }
        if(app->ok && renault_seed_bf_save(app)) {
            char result[32];
            snprintf(result, sizeof(result), "Seed: %08lX", (unsigned long)app->seed);
            renault_seed_bf_popup(app, "Seed found", result);
        } else {
            renault_seed_bf_popup(app, "Seed BF", "Seed not found");
        }
        return true;
    }
    if(kind == EVENT_BACK) {
        app->cancel = true;
        view_dispatcher_stop(app->dispatcher);
        return true;
    }
    return false;
}

static bool renault_seed_bf_back(void* context) {
    RenaultSeedBf* app = context;
    app->cancel = true;
    view_dispatcher_stop(app->dispatcher);
    return true;
}

int32_t renault_seed_bf_app(void* p) {
    UNUSED(p);
    RenaultSeedBf* app = calloc(1, sizeof(RenaultSeedBf));
    furi_check(app);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->path = furi_string_alloc_set(SEED_BF_ROOT);

    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, SEED_BF_EXT, NULL);
    options.base_path = SEED_BF_ROOT;
    if(!dialog_file_browser_show(app->dialogs, app->path, app->path, &options) ||
       !renault_seed_bf_load_frame(app)) {
        furi_string_free(app->path);
        furi_record_close(RECORD_DIALOGS);
        furi_record_close(RECORD_STORAGE);
        free(app);
        return 0;
    }

    app->dispatcher = view_dispatcher_alloc();
    app->popup = popup_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, renault_seed_bf_custom_event);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, renault_seed_bf_back);
    view_dispatcher_add_view(app->dispatcher, 0, popup_get_view(app->popup));
    view_dispatcher_attach_to_gui(app->dispatcher, furi_record_open(RECORD_GUI), ViewDispatcherTypeFullscreen);
    renault_seed_bf_popup(app, "Seed BF", "Running... 0%");

    app->thread = furi_thread_alloc_ex("SeedBF", 4096, renault_seed_bf_worker, app);
    furi_thread_set_priority(app->thread, FuriThreadPriorityLow);
    furi_thread_start(app->thread);
    view_dispatcher_run(app->dispatcher);

    app->cancel = true;
    if(app->thread) {
        furi_thread_join(app->thread);
        furi_thread_free(app->thread);
    }
    furi_record_close(RECORD_GUI);
    view_dispatcher_remove_view(app->dispatcher, 0);
    popup_free(app->popup);
    view_dispatcher_free(app->dispatcher);
    furi_string_free(app->path);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
