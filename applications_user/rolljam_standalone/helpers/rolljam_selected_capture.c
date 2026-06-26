#include "rolljam_selected_capture.h"
#include "../rolljam_app_i.h"
#include "rolljam_types.h"
#include "../rolljam_history.h"

void rolljam_selected_capture_set(RollJamApp* app, RollJamCaptureOwner owner, uint16_t index) {
    furi_check(app);
    app->selected_capture.owner = owner;
    app->selected_capture.index = index;

    if(owner == RollJamCaptureOwnerMainReceiver && app->txrx) {
        app->selected_capture.history = app->txrx->history;
    } else if(owner == RollJamCaptureOwnerDualReceiver) {
#ifdef ENABLE_DUAL_RX_SCENE
        if(app->dual_history) {
            app->selected_capture.history = app->dual_history;
        }
#endif
    } else if(owner == RollJamCaptureOwnerShieldReceiver) {
#ifdef ENABLE_SHIELD_RX_SCENE
        if(app->shield_history) {
            app->selected_capture.history = app->shield_history;
        }
#endif
    } else {
        app->selected_capture.history = NULL;
    }
}

void rolljam_selected_capture_clear(RollJamApp* app) {
    furi_check(app);
    app->selected_capture.owner = RollJamCaptureOwnerNone;
    app->selected_capture.index = 0;
    app->selected_capture.history = NULL;
}

bool rolljam_selected_capture_is_valid(RollJamApp* app) {
    furi_check(app);
    return (app->selected_capture.owner != RollJamCaptureOwnerNone && app->selected_capture.history != NULL);
}

uint16_t rolljam_selected_capture_get_index(RollJamApp* app) {
    furi_check(app);
    return app->selected_capture.index;
}

RollJamHistory* rolljam_selected_capture_get_history(RollJamApp* app) {
    furi_check(app);
    return app->selected_capture.history;
}

RollJamHistorySource rolljam_selected_capture_get_source(RollJamApp* app) {
    furi_check(app);
    if(!rolljam_selected_capture_is_valid(app)) {
        return RollJamHistorySourceUnknown;
    }
    return rolljam_history_get_source(app->selected_capture.history, app->selected_capture.index);
}

bool rolljam_selected_capture_get_path(RollJamApp* app, FuriString* out_path) {
    furi_check(app);
    if(!rolljam_selected_capture_is_valid(app)) {
        return false;
    }
    return rolljam_history_get_capture_path(app->selected_capture.history, app->selected_capture.index, out_path);
}

FlipperFormat* rolljam_selected_capture_get_raw_data(RollJamApp* app) {
    furi_check(app);
    if(!rolljam_selected_capture_is_valid(app)) {
        return NULL;
    }
    return rolljam_history_get_raw_data(app->selected_capture.history, app->selected_capture.index);
}

void rolljam_selected_capture_release_scratch(RollJamApp* app) {
    furi_check(app);
    if(rolljam_selected_capture_is_valid(app)) {
        rolljam_history_release_scratch(app->selected_capture.history);
    }
}
