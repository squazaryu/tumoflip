#include <furi.h>
#include <bt/bt_service/bt.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>

// BLE App Bridge "app id" for the Sber section. This is the WIRE protocol id and
// must match APP_BRIDGE_APP_ID in the host bridge (relay_bridge.py). Each future
// section/target gets its own wire id (and a matching handler in the bridge).
#define FLIPPER_RELAY_SBER_APP_ID "sber_relay"

typedef enum {
    FlipperRelayViewMain = 0, // top-level list of relay targets
    FlipperRelayViewSber = 1, // Sber-specific actions
} FlipperRelayView;

typedef enum {
    FlipperRelayMainSber, // open the Sber section
} FlipperRelayMainIndex;

typedef enum {
    SberMenuOn,
    SberMenuOff,
    SberMenuToggle,
} SberMenuIndex;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Submenu* main_menu;
    Submenu* sber_menu;
} FlipperRelayApp;

static void flipper_relay_send(const char* app_id, const char* action) {
    Bt* bt = furi_record_open(RECORD_BT);
    if(!bt_app_bridge_send_text(bt, app_id, action, "")) {
        FURI_LOG_W("FlipperRelay", "Failed to send BLE app bridge action: %s", action);
    }
    furi_record_close(RECORD_BT);
}

// --- main (top-level) menu -------------------------------------------------
static void flipper_relay_main_callback(void* context, uint32_t index) {
    FlipperRelayApp* app = context;
    if(index == FlipperRelayMainSber) {
        view_dispatcher_switch_to_view(app->view_dispatcher, FlipperRelayViewSber);
    }
}

static uint32_t flipper_relay_nav_exit(void* context) {
    UNUSED(context);
    return VIEW_NONE; // back from the top-level menu exits the app
}

// --- Sber section ----------------------------------------------------------
static void flipper_relay_sber_callback(void* context, uint32_t index) {
    UNUSED(context);
    if(index == SberMenuOn) {
        flipper_relay_send(FLIPPER_RELAY_SBER_APP_ID, "on");
    } else if(index == SberMenuOff) {
        flipper_relay_send(FLIPPER_RELAY_SBER_APP_ID, "off");
    } else if(index == SberMenuToggle) {
        flipper_relay_send(FLIPPER_RELAY_SBER_APP_ID, "toggle");
    }
}

static uint32_t flipper_relay_nav_to_main(void* context) {
    UNUSED(context);
    return FlipperRelayViewMain; // back from a section returns to the top-level menu
}

static FlipperRelayApp* flipper_relay_app_alloc(void) {
    FlipperRelayApp* app = malloc(sizeof(FlipperRelayApp));

    app->gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Top-level menu: list of relay targets (Sber is the first; more can follow).
    app->main_menu = submenu_alloc();
    submenu_set_header(app->main_menu, "Flipper Relay");
    submenu_add_item(
        app->main_menu, "Sber Relay", FlipperRelayMainSber, flipper_relay_main_callback, app);
    view_set_previous_callback(submenu_get_view(app->main_menu), flipper_relay_nav_exit);
    view_dispatcher_add_view(
        app->view_dispatcher, FlipperRelayViewMain, submenu_get_view(app->main_menu));

    // Sber section: the actual on/off/toggle actions.
    app->sber_menu = submenu_alloc();
    submenu_set_header(app->sber_menu, "Sber Relay");
    submenu_add_item(app->sber_menu, "Turn ON", SberMenuOn, flipper_relay_sber_callback, app);
    submenu_add_item(app->sber_menu, "Turn OFF", SberMenuOff, flipper_relay_sber_callback, app);
    submenu_add_item(app->sber_menu, "Toggle", SberMenuToggle, flipper_relay_sber_callback, app);
    view_set_previous_callback(submenu_get_view(app->sber_menu), flipper_relay_nav_to_main);
    view_dispatcher_add_view(
        app->view_dispatcher, FlipperRelayViewSber, submenu_get_view(app->sber_menu));

    view_dispatcher_switch_to_view(app->view_dispatcher, FlipperRelayViewMain);

    return app;
}

static void flipper_relay_app_free(FlipperRelayApp* app) {
    furi_assert(app);

    view_dispatcher_remove_view(app->view_dispatcher, FlipperRelayViewMain);
    view_dispatcher_remove_view(app->view_dispatcher, FlipperRelayViewSber);
    submenu_free(app->main_menu);
    submenu_free(app->sber_menu);
    view_dispatcher_free(app->view_dispatcher);

    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t flipper_relay_main(void* p) {
    UNUSED(p);
    FlipperRelayApp* app = flipper_relay_app_alloc();
    view_dispatcher_run(app->view_dispatcher);
    flipper_relay_app_free(app);
    return 0;
}
