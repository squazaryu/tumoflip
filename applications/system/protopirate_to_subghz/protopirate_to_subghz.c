#include <furi.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <toolbox/dir_walk.h>
#include <string.h>
#include <strings.h>

#include "converter.h"
#include "paths.h"

#define P2S_WORKER_FLAG_STOP (1U << 0)

typedef enum {
    P2sViewMenu,
    P2sViewProgress,
    P2sViewSummary
} P2sView;
typedef enum {
    P2sToSub,
    P2sToPsf,
    P2sAbout
} P2sAction;
typedef struct {
    uint32_t total;
    uint32_t current;
    uint32_t converted;
    uint32_t skipped;
    uint32_t errors;
    bool scanning;
    bool cancelled;
} P2sProgress;

typedef struct {
    ViewDispatcher* dispatcher;
    Submenu* menu;
    View* progress;
    Widget* summary;
    FuriThread* worker;
    P2sAction direction;
} P2sApp;

static uint32_t p2s_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static uint32_t p2s_menu(void* context) {
    UNUSED(context);
    return P2sViewMenu;
}

static bool p2s_cancelled(void* context) {
    UNUSED(context);
    return (furi_thread_flags_get() & P2S_WORKER_FLAG_STOP) != 0;
}

static bool p2s_matches(const char* path, const char* extension) {
    const size_t size = strlen(path);
    const size_t ext_size = strlen(extension);
    return size >= ext_size && strcasecmp(path + size - ext_size, extension) == 0;
}

static void p2s_draw(Canvas* canvas, void* model) {
    P2sProgress* m = model;
    char text[40];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(
        canvas,
        2,
        12,
        m->cancelled ? "Stopping..." : (m->scanning ? "Scanning..." : "Copying + verifying"));
    canvas_set_font(canvas, FontSecondary);
    snprintf(text, sizeof(text), "%lu / %lu", (unsigned long)m->current, (unsigned long)m->total);
    canvas_draw_str(canvas, 2, 28, text);
    snprintf(
        text,
        sizeof(text),
        "OK:%lu Skip:%lu Err:%lu",
        (unsigned long)m->converted,
        (unsigned long)m->skipped,
        (unsigned long)m->errors);
    canvas_draw_str(canvas, 2, 44, text);
    canvas_draw_str(canvas, 2, 60, "Back: cancel");
}

static bool p2s_input(InputEvent* event, void* context) {
    P2sApp* app = context;
    if(event->key != InputKeyBack || event->type != InputTypeShort) return false;
    if(app->worker && furi_thread_get_state(app->worker) != FuriThreadStateStopped) {
        furi_thread_flags_set(furi_thread_get_id(app->worker), P2S_WORKER_FLAG_STOP);
        with_view_model(app->progress, P2sProgress * m, { m->cancelled = true; }, true);
    }
    return true;
}

static void p2s_walk(P2sApp* app, Storage* storage, bool count_only) {
    const char* root = app->direction == P2sToSub ? PP_SAVED_DIR : SUBGHZ_DIR;
    const char* ext = app->direction == P2sToSub ? PP_EXTENSION : SUB_EXTENSION;
    DirWalk* walk = dir_walk_alloc(storage);
    dir_walk_set_recursive(walk, true);
    FuriString* path = furi_string_alloc();
    bool error = !dir_walk_open(walk, root);
    if(!error) {
        FileInfo info;
        // No extension filter in DirWalk: cancellation is checked for every entry,
        // even when a large directory contains no matching captures.
        while(!p2s_cancelled(NULL)) {
            const DirWalkResult state = dir_walk_read(walk, path, &info);
            if(state == DirWalkError) {
                error = true;
                break;
            }
            if(state != DirWalkOK) break;
            if(file_info_is_dir(&info) || !p2s_matches(furi_string_get_cstr(path), ext)) continue;
            if(count_only) {
                with_view_model(app->progress, P2sProgress * m, { m->total++; }, false);
                continue;
            }
            const P2sResult result =
                app->direction == P2sToSub ?
                    p2s_convert_psf_to_sub(furi_string_get_cstr(path), p2s_cancelled, NULL) :
                    p2s_convert_sub_to_psf(furi_string_get_cstr(path), p2s_cancelled, NULL);
            if(result == P2sResultCancelled) break;
            with_view_model(
                app->progress,
                P2sProgress * m,
                {
                    m->current++;
                    if(result == P2sResultOk)
                        m->converted++;
                    else if(result == P2sResultSkipped)
                        m->skipped++;
                    else
                        m->errors++;
                },
                false);
        }
    }
    if(error) {
        with_view_model(app->progress, P2sProgress * m, { m->errors++; }, false);
    }
    furi_string_free(path);
    dir_walk_close(walk);
    dir_walk_free(walk);
}

static int32_t p2s_worker(void* context) {
    P2sApp* app = context;
    Storage* storage = furi_record_open(RECORD_STORAGE);
    p2s_walk(app, storage, true);
    bool scan_ok = false;
    with_view_model(
        app->progress,
        P2sProgress * m,
        {
            m->scanning = false;
            scan_ok = m->errors == 0;
        },
        false);
    if(scan_ok && !p2s_cancelled(NULL)) p2s_walk(app, storage, false);
    with_view_model(
        app->progress,
        P2sProgress * m,
        { m->cancelled = m->cancelled || p2s_cancelled(NULL); },
        false);
    furi_record_close(RECORD_STORAGE);
    return 0;
}

static void p2s_join(P2sApp* app) {
    if(!app->worker) return;
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);
    app->worker = NULL;
}

// UI polls worker state. No blocking event sends can outlive the dispatcher.
static void p2s_tick(void* context) {
    P2sApp* app = context;
    if(!app->worker) return;
    if(furi_thread_get_state(app->worker) != FuriThreadStateStopped) {
        with_view_model(app->progress, P2sProgress * m, { UNUSED(m); }, true);
        return;
    }
    p2s_join(app);
    P2sProgress result;
    with_view_model(app->progress, P2sProgress * m, { result = *m; }, false);
    char text[160];
    snprintf(
        text,
        sizeof(text),
        "%s\nCopied: %lu\nSkipped: %lu\nErrors: %lu\nOriginals unchanged",
        result.cancelled ? "Cancelled" : (result.errors ? "Finished with errors" : "Done"),
        (unsigned long)result.converted,
        (unsigned long)result.skipped,
        (unsigned long)result.errors);
    widget_reset(app->summary);
    widget_add_text_scroll_element(app->summary, 0, 0, 128, 64, text);
    view_dispatcher_switch_to_view(app->dispatcher, P2sViewSummary);
}

static void p2s_select(void* context, uint32_t action) {
    P2sApp* app = context;
    if(app->worker) return;
    if(action == P2sAbout) {
        widget_reset(app->summary);
        widget_add_text_scroll_element(
            app->summary,
            0,
            0,
            128,
            64,
            "Capture converter\n\n"
            "Copies saved .psf/.sub\nkey captures unchanged.\n"
            "No decoding or radio use.\n\n"
            ".sub output: /subghz/imported\n"
            ".psf output: ProtoPirate/saved\n\n"
            "Select the original protocol\npack to open the copy.\n"
            "RAW recordings and invalid\nheaders are skipped.\n"
            "Back cancels safely.");
        view_dispatcher_switch_to_view(app->dispatcher, P2sViewSummary);
        return;
    }
    if(action != P2sToSub && action != P2sToPsf) return;
    app->direction = action;
    with_view_model(
        app->progress,
        P2sProgress * m,
        {
            memset(m, 0, sizeof(*m));
            m->scanning = true;
        },
        true);
    app->worker = furi_thread_alloc_ex("CaptureCopy", 4096, p2s_worker, app);
    furi_thread_set_priority(app->worker, FuriThreadPriorityLow);
    // Start before switching view: Back always sees a valid worker thread.
    furi_thread_start(app->worker);
    view_dispatcher_switch_to_view(app->dispatcher, P2sViewProgress);
}

int32_t protopirate_to_subghz_app(void* context) {
    UNUSED(context);
    P2sApp* app = calloc(1, sizeof(P2sApp));
    app->dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->progress = view_alloc();
    app->summary = widget_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_tick_event_callback(app->dispatcher, p2s_tick, 150);
    view_dispatcher_attach_to_gui(
        app->dispatcher, furi_record_open(RECORD_GUI), ViewDispatcherTypeFullscreen);

    submenu_add_item(app->menu, "PSF -> SUB", P2sToSub, p2s_select, app);
    submenu_add_item(app->menu, "SUB -> PSF", P2sToPsf, p2s_select, app);
    submenu_add_item(app->menu, "About / paths", P2sAbout, p2s_select, app);
    view_set_previous_callback(submenu_get_view(app->menu), p2s_exit);
    view_dispatcher_add_view(app->dispatcher, P2sViewMenu, submenu_get_view(app->menu));

    view_set_context(app->progress, app);
    view_set_draw_callback(app->progress, p2s_draw);
    view_set_input_callback(app->progress, p2s_input);
    view_allocate_model(app->progress, ViewModelTypeLocking, sizeof(P2sProgress));
    view_dispatcher_add_view(app->dispatcher, P2sViewProgress, app->progress);
    view_set_previous_callback(widget_get_view(app->summary), p2s_menu);
    view_dispatcher_add_view(app->dispatcher, P2sViewSummary, widget_get_view(app->summary));
    view_dispatcher_switch_to_view(app->dispatcher, P2sViewMenu);
    view_dispatcher_run(app->dispatcher);

    if(app->worker && furi_thread_get_state(app->worker) != FuriThreadStateStopped)
        furi_thread_flags_set(furi_thread_get_id(app->worker), P2S_WORKER_FLAG_STOP);
    p2s_join(app);
    view_dispatcher_remove_view(app->dispatcher, P2sViewSummary);
    view_dispatcher_remove_view(app->dispatcher, P2sViewProgress);
    view_dispatcher_remove_view(app->dispatcher, P2sViewMenu);
    widget_free(app->summary);
    view_free(app->progress);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
