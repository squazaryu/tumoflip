#include "nfc_field_capture.h"
#include "nfc_field_decoder.h"
#include <furi.h>
#include <furi_hal_rtc.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/view_dispatcher.h>
#include <storage/storage.h>
#include <stdio.h>
#include <string.h>

enum {
    NfcFieldMenuView,
    NfcFieldTextView
};
enum {
    NfcFieldStart = 1,
    NfcFieldView,
    NfcFieldSave,
    NfcFieldAbout,
    NfcFieldFinished = 100
};
typedef struct {
    Storage* storage;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    TextBox* text;
    FuriString* message;
    FuriThread* worker;
    NfcFieldCaptureResult result;
    bool ready;
    bool showing_text;
    char trace[4096];
} NfcFieldApp;

static void nfc_field_menu(NfcFieldApp* app);
static void nfc_field_action(void* context, uint32_t action) {
    view_dispatcher_send_custom_event(((NfcFieldApp*)context)->dispatcher, action);
}
static void nfc_field_message(NfcFieldApp* app, const char* text) {
    app->showing_text = true;
    if(text != furi_string_get_cstr(app->message)) furi_string_set(app->message, text);
    text_box_reset(app->text);
    text_box_set_text(app->text, furi_string_get_cstr(app->message));
    view_dispatcher_switch_to_view(app->dispatcher, NfcFieldTextView);
}
static bool nfc_field_stop(void* context) {
    UNUSED(context);
    return furi_thread_flags_get() & 1U;
}
static int32_t nfc_field_worker(void* context) {
    NfcFieldApp* app = context;
    app->result = nfc_field_capture(2000, nfc_field_stop, app);
    view_dispatcher_send_custom_event(app->dispatcher, NfcFieldFinished);
    return 0;
}
static void nfc_field_menu(NfcFieldApp* app) {
    app->showing_text = false;
    submenu_reset(app->menu);
    submenu_set_header(app->menu, app->worker ? "Capturing..." : "Passive NFC");
    if(app->worker) {
        submenu_add_item(app->menu, "Listening (2s max)", 0, nfc_field_action, app);
        submenu_add_item(app->menu, "Back: stop capture", 0, nfc_field_action, app);
    } else {
        submenu_add_item(app->menu, "Capture for 2 seconds", NfcFieldStart, nfc_field_action, app);
        if(app->ready) {
            submenu_add_item(app->menu, "View last trace", NfcFieldView, nfc_field_action, app);
            submenu_add_item(app->menu, "Save trace file", NfcFieldSave, nfc_field_action, app);
        } else
            submenu_add_item(app->menu, "No trace yet", 0, nfc_field_action, app);
        submenu_add_item(app->menu, "About / limits", NfcFieldAbout, nfc_field_action, app);
    }
    view_dispatcher_switch_to_view(app->dispatcher, NfcFieldMenuView);
}
static bool nfc_field_save(NfcFieldApp* app, char path[128]) {
    const char* root = EXT_PATH("nfc/field_sniffer");
    if(!app->ready || !storage_simply_mkdir(app->storage, EXT_PATH("nfc")) ||
       !storage_simply_mkdir(app->storage, root))
        return false;
    uint32_t stamp = furi_hal_rtc_get_timestamp();
    bool unused = false;
    for(unsigned i = 0; i < 100; i++) {
        snprintf(path, 128, "%s/%lu-%02u.txt", root, (unsigned long)stamp, i);
        FS_Error status = storage_common_stat(app->storage, path, NULL);
        if(status == FSE_NOT_EXIST) {
            unused = true;
            break;
        }
        if(status != FSE_OK) return false;
    }
    if(!unused) return false;
    File* file = storage_file_alloc(app->storage);
    bool created = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_NEW);
    size_t size = strlen(app->trace);
    bool ok = created && storage_file_write(file, app->trace, size) == size &&
              storage_file_sync(file);
    if(created) ok = storage_file_close(file) && ok;
    bool opened = ok && storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    ok = opened && storage_file_size(file) == size;
    uint8_t bytes[256];
    for(size_t offset = 0; ok && offset < size;) {
        size_t count = size - offset < sizeof(bytes) ? size - offset : sizeof(bytes);
        ok = storage_file_read(file, bytes, count) == count &&
             !memcmp(bytes, app->trace + offset, count);
        offset += count;
    }
    if(opened) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    if(!ok && created) storage_common_remove(app->storage, path);
    return ok;
}
static bool nfc_field_event(void* context, uint32_t action) {
    NfcFieldApp* app = context;
    if(action == NfcFieldFinished && app->worker) {
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
        app->worker = NULL;
        if(app->result == NfcFieldCaptureComplete || app->result == NfcFieldCaptureCancelled) {
            int prefix = snprintf(
                app->trace,
                sizeof(app->trace),
                "%s\n\n",
                app->result == NfcFieldCaptureCancelled ? "Stopped by user; partial session." :
                                                          "Capture finished; hardware released.");
            bool ok =
                prefix > 0 && (size_t)prefix < sizeof(app->trace) &&
                nfc_field_decoder_format(app->trace + prefix, sizeof(app->trace) - (size_t)prefix);
            app->ready = ok;
            if(ok) {
                nfc_field_message(app, app->trace);
            } else
                nfc_field_message(app, "Trace formatting failed.\nNothing saved.");
        } else
            nfc_field_message(
                app,
                app->result == NfcFieldCaptureNoField ?
                    "No external RF field.\nUse your own active reader\nand card near Flipper.\nPrevious saved trace unchanged." :
                app->result == NfcFieldCaptureBusy ?
                    "NFC / DMA / TIM2 busy.\nNo resource was taken\nfrom its current owner.\nPrevious trace unchanged." :
                    "Capture failed.\nResources released.\nNo files changed.");
        return true;
    }
    if(app->worker || app->showing_text) return true;
    if(action == NfcFieldStart) {
        app->worker = furi_thread_alloc_ex("NfcField", 4096, nfc_field_worker, app);
        nfc_field_menu(app);
        furi_thread_start(app->worker);
    } else if(action == NfcFieldView && app->ready)
        nfc_field_message(app, app->trace);
    else if(action == NfcFieldSave && app->ready) {
        char path[128];
        if(nfc_field_save(app, path)) {
            furi_string_printf(
                app->message,
                "Trace readback verified.\n%s\n\nMay contain private data.\nCopy with Companion > Files.",
                path);
            nfc_field_message(app, furi_string_get_cstr(app->message));
        } else
            nfc_field_message(
                app, "Save failed.\nCheck SD / free space.\nExisting files unchanged.");
    } else if(action == NfcFieldAbout)
        nfc_field_message(
            app,
            "Tumo Passive NFC 1.0\n\nExperimental ISO14443-A106k candidate-frame sniffer for your own reader/card. Flipper does not generate an RF field, relay traffic or recover keys.\n\nPosition both devices close to Flipper. Capture lasts up to2 seconds. Back cancels and joins the worker before exit.\n\nStatic8KiB sample ring;24 frames/24 bytes each. Lost regions, overruns and truncation are reported. Timing is relative sample time; gaps may be lost after overruns. CRC is unverified unless matched; a match is not authentication.\n\nBusy resources fail closed. No capture runs in the background. Trace files may contain private data and are written only on Save. Physical-device acceptance is pending.");
    return true;
}
static bool nfc_field_back(void* context) {
    NfcFieldApp* app = context;
    if(app->worker) {
        furi_thread_flags_set(furi_thread_get_id(app->worker), 1U);
        return true;
    }
    if(app->showing_text)
        nfc_field_menu(app);
    else
        view_dispatcher_stop(app->dispatcher);
    return true;
}
int32_t nfc_field_sniffer_app(void* context) {
    UNUSED(context);
    NfcFieldApp* app = calloc(1, sizeof(*app));
    app->storage = furi_record_open(RECORD_STORAGE);
    Gui* gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->text = text_box_alloc();
    app->message = furi_string_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, nfc_field_event);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, nfc_field_back);
    text_box_set_font(app->text, TextBoxFontText);
    view_dispatcher_add_view(app->dispatcher, NfcFieldMenuView, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, NfcFieldTextView, text_box_get_view(app->text));
    view_dispatcher_attach_to_gui(app->dispatcher, gui, ViewDispatcherTypeFullscreen);
    nfc_field_menu(app);
    view_dispatcher_run(app->dispatcher);
    if(app->worker) {
        furi_thread_flags_set(furi_thread_get_id(app->worker), 1U);
        furi_thread_join(app->worker);
        furi_thread_free(app->worker);
    }
    view_dispatcher_remove_view(app->dispatcher, NfcFieldTextView);
    view_dispatcher_remove_view(app->dispatcher, NfcFieldMenuView);
    text_box_free(app->text);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);
    furi_string_free(app->message);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
