#include "xremote_ac.h"
#include "xremote.h"

static uint32_t xremote_ac_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

int32_t xremote_ac_main(void* argument) {
    XRemoteAppContext* context = xremote_app_context_alloc(argument);
    XRemoteApp* app = xremote_ac_alloc(context);

    View* menu_view = submenu_get_view(app->submenu);
    view_set_previous_callback(menu_view, xremote_ac_exit_callback);
    xremote_app_switch_to_view(app, XRemoteViewAcSmart);
    view_dispatcher_run(context->view_dispatcher);

    xremote_app_free(app);
    xremote_app_context_free(context);
    return 0;
}
