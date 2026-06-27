#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_sd.h>
#include <gui/gui.h>
#include <gui/modules/dialog_ex.h>
#include <gui/view_dispatcher.h>
#include <mass_storage_usb.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <storage/storage_sd_api.h>

#define TAG "UsbSdMode"

typedef enum {
    UsbSdModeViewDialog,
} UsbSdModeView;

typedef enum {
    UsbSdModeStateIntro,
    UsbSdModeStateActive,
    UsbSdModeStateError,
    UsbSdModeStateStopConfirm,
} UsbSdModeState;

typedef struct {
    Gui* gui;
    Storage* storage;
    NotificationApp* notification;
    ViewDispatcher* view_dispatcher;
    DialogEx* dialog;

    MassStorageUsb* usb;
    bool sd_needs_mount;
    bool was_ejected;
    uint32_t block_count;
    UsbSdModeState state;
} UsbSdModeApp;

static bool usb_sd_mode_read(
    void* context,
    uint32_t lba,
    uint16_t count,
    uint8_t* out,
    uint32_t* out_len,
    uint32_t out_cap) {
    UsbSdModeApp* app = context;
    uint32_t blocks = MIN(count, out_cap / SCSI_BLOCK_SIZE);

    if((blocks == 0) || (lba >= app->block_count)) {
        *out_len = 0;
        return false;
    }

    if((lba + blocks) > app->block_count) {
        blocks = app->block_count - lba;
    }

    FuriStatus status = furi_hal_sd_read_blocks((uint32_t*)out, lba, blocks);
    *out_len = blocks * SCSI_BLOCK_SIZE;

    return status == FuriStatusOk;
}

static bool usb_sd_mode_write(
    void* context,
    uint32_t lba,
    uint16_t count,
    uint8_t* data,
    uint32_t len) {
    UsbSdModeApp* app = context;
    uint32_t blocks = len / SCSI_BLOCK_SIZE;

    if((len % SCSI_BLOCK_SIZE) || (blocks != count) || (lba >= app->block_count)) {
        return false;
    }

    if((lba + blocks) > app->block_count) {
        return false;
    }

    return furi_hal_sd_write_blocks((uint32_t*)data, lba, blocks) == FuriStatusOk;
}

static uint32_t usb_sd_mode_num_blocks(void* context) {
    UsbSdModeApp* app = context;
    return app->block_count;
}

static void usb_sd_mode_eject(void* context) {
    UsbSdModeApp* app = context;
    app->was_ejected = true;
}

static void usb_sd_mode_stop(UsbSdModeApp* app) {
    if(app->usb) {
        mass_storage_usb_stop(app->usb);
        app->usb = NULL;
    }

    if(app->sd_needs_mount) {
        // Reinitialize SD to drop stale sector cache before FatFS mounts it again.
        furi_hal_sd_init(false);
        FS_Error mount_error = storage_sd_mount(app->storage);
        if(mount_error != FSE_OK) {
            FURI_LOG_E(TAG, "Failed to remount SD: %s", storage_error_get_desc(mount_error));
        }
        app->sd_needs_mount = false;
    }

    app->was_ejected = false;
    app->block_count = 0;
}

static const char* usb_sd_mode_start(UsbSdModeApp* app) {
    FS_Error sd_status = storage_sd_status(app->storage);
    if(sd_status != FSE_OK) {
        return "SD card is not\nmounted.";
    }

    FuriHalSdInfo sd_info;
    if(furi_hal_sd_info(&sd_info) != FuriStatusOk) {
        return "Cannot read\nSD card info.";
    }

    if(sd_info.logical_block_size != SCSI_BLOCK_SIZE) {
        return "Unsupported\nSD block size.";
    }

    FS_Error unmount_error = storage_sd_unmount(app->storage);
    if(unmount_error != FSE_OK) {
        if(unmount_error == FSE_DENIED) {
            return "Close apps that\nuse SD card.";
        }
        return storage_error_get_desc(unmount_error);
    }

    app->sd_needs_mount = true;
    app->was_ejected = false;
    app->block_count = sd_info.logical_block_count;

    if(furi_hal_sd_init(false) != FuriStatusOk) {
        usb_sd_mode_stop(app);
        return "Cannot init\nSD card.";
    }

    SCSIDeviceFunc fn = {
        .ctx = app,
        .read = usb_sd_mode_read,
        .write = usb_sd_mode_write,
        .num_blocks = usb_sd_mode_num_blocks,
        .eject = usb_sd_mode_eject,
    };

    furi_hal_usb_unlock();
    app->usb = mass_storage_usb_start("Tumoflip SD", fn);
    if(!app->usb) {
        usb_sd_mode_stop(app);
        return "USB is busy.\nTry reconnecting.";
    }

    notification_message(app->notification, &sequence_blink_green_100);
    return NULL;
}

static void usb_sd_mode_dialog_callback(DialogExResult result, void* context) {
    UsbSdModeApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, result);
}

static void usb_sd_mode_show_intro(UsbSdModeApp* app) {
    app->state = UsbSdModeStateIntro;
    DialogEx* dialog = app->dialog;

    dialog_ex_reset(dialog);
    dialog_ex_set_header(dialog, "USB SD Mode", 64, 4, AlignCenter, AlignTop);
    dialog_ex_set_text(
        dialog,
        "Expose SD as USB disk.\nClose Files before\npressing Stop.",
        64,
        18,
        AlignCenter,
        AlignTop);
    dialog_ex_set_left_button_text(dialog, "Back");
    dialog_ex_set_center_button_text(dialog, "Start");
    dialog_ex_set_context(dialog, app);
    dialog_ex_set_result_callback(dialog, usb_sd_mode_dialog_callback);
}

static void usb_sd_mode_show_active(UsbSdModeApp* app) {
    app->state = UsbSdModeStateActive;
    DialogEx* dialog = app->dialog;

    dialog_ex_reset(dialog);
    dialog_ex_set_header(dialog, "USB SD Active", 64, 4, AlignCenter, AlignTop);
    dialog_ex_set_text(
        dialog,
        "Use Files on iPhone.\nWait for copies,\nthen press Stop.",
        64,
        18,
        AlignCenter,
        AlignTop);
    dialog_ex_set_center_button_text(dialog, "Stop");
    dialog_ex_set_context(dialog, app);
    dialog_ex_set_result_callback(dialog, usb_sd_mode_dialog_callback);
}

static void usb_sd_mode_show_error(UsbSdModeApp* app, const char* error) {
    app->state = UsbSdModeStateError;
    DialogEx* dialog = app->dialog;

    dialog_ex_reset(dialog);
    dialog_ex_set_header(dialog, "Cannot Start", 64, 3, AlignCenter, AlignTop);
    dialog_ex_set_text(dialog, error, 64, 21, AlignCenter, AlignTop);
    dialog_ex_set_center_button_text(dialog, "OK");
    dialog_ex_set_context(dialog, app);
    dialog_ex_set_result_callback(dialog, usb_sd_mode_dialog_callback);
    notification_message(app->notification, &sequence_blink_red_100);
}

static void usb_sd_mode_show_stop_confirm(UsbSdModeApp* app) {
    app->state = UsbSdModeStateStopConfirm;
    DialogEx* dialog = app->dialog;

    dialog_ex_reset(dialog);
    dialog_ex_set_header(dialog, "Stop USB SD?", 64, 4, AlignCenter, AlignTop);
    dialog_ex_set_text(
        dialog,
        "Close Files first.\nStop only when\ncopies are done.",
        64,
        18,
        AlignCenter,
        AlignTop);
    dialog_ex_set_left_button_text(dialog, "Back");
    dialog_ex_set_right_button_text(dialog, "Stop");
    dialog_ex_set_context(dialog, app);
    dialog_ex_set_result_callback(dialog, usb_sd_mode_dialog_callback);
    notification_message(app->notification, &sequence_blink_red_100);
}

static bool usb_sd_mode_stop_or_confirm(UsbSdModeApp* app) {
    if(app->usb && !app->was_ejected) {
        usb_sd_mode_show_stop_confirm(app);
        return false;
    }

    usb_sd_mode_stop(app);
    return true;
}

static bool usb_sd_mode_custom_event_callback(void* context, uint32_t event) {
    UsbSdModeApp* app = context;

    if(event == DialogExResultLeft) {
        if(app->state == UsbSdModeStateStopConfirm) {
            usb_sd_mode_show_active(app);
        } else {
            view_dispatcher_stop(app->view_dispatcher);
        }
        return true;
    }

    if(event == DialogExResultCenter) {
        if(app->state == UsbSdModeStateError) {
            usb_sd_mode_show_intro(app);
            return true;
        }

        if(app->usb) {
            if(usb_sd_mode_stop_or_confirm(app)) {
                view_dispatcher_stop(app->view_dispatcher);
            }
            return true;
        }

        const char* error = usb_sd_mode_start(app);
        if(error) {
            usb_sd_mode_show_error(app, error);
        } else {
            usb_sd_mode_show_active(app);
        }
        return true;
    }

    if(event == DialogExResultRight) {
        if(app->state == UsbSdModeStateStopConfirm) {
            usb_sd_mode_stop(app);
            view_dispatcher_stop(app->view_dispatcher);
        }
        return true;
    }

    return false;
}

static bool usb_sd_mode_back_event_callback(void* context) {
    UsbSdModeApp* app = context;

    if(app->state == UsbSdModeStateStopConfirm) {
        usb_sd_mode_show_active(app);
        return true;
    }

    if(app->usb) {
        if(usb_sd_mode_stop_or_confirm(app)) {
            view_dispatcher_stop(app->view_dispatcher);
        }
        return true;
    }

    view_dispatcher_stop(app->view_dispatcher);
    return true;
}

static UsbSdModeApp* usb_sd_mode_alloc(void) {
    UsbSdModeApp* app = malloc(sizeof(UsbSdModeApp));

    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->usb = NULL;
    app->sd_needs_mount = false;
    app->was_ejected = false;
    app->block_count = 0;
    app->state = UsbSdModeStateIntro;

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);
    view_dispatcher_set_custom_event_callback(
        app->view_dispatcher, usb_sd_mode_custom_event_callback);
    view_dispatcher_set_navigation_event_callback(
        app->view_dispatcher, usb_sd_mode_back_event_callback);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    app->dialog = dialog_ex_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, UsbSdModeViewDialog, dialog_ex_get_view(app->dialog));
    usb_sd_mode_show_intro(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, UsbSdModeViewDialog);

    return app;
}

static void usb_sd_mode_free(UsbSdModeApp* app) {
    furi_assert(app);

    usb_sd_mode_stop(app);
    view_dispatcher_remove_view(app->view_dispatcher, UsbSdModeViewDialog);
    dialog_ex_free(app->dialog);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

int32_t usb_sd_mode_app(void* p) {
    UNUSED(p);

    UsbSdModeApp* app = usb_sd_mode_alloc();
    view_dispatcher_run(app->view_dispatcher);
    usb_sd_mode_free(app);

    return 0;
}
