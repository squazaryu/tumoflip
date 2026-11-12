// Methods for iButton transmission

#include <furi.h>
#include <furi_hal.h>

#include <ibutton/ibutton_key.h>
#include <ibutton/ibutton_worker.h>
#include <ibutton/ibutton_protocols.h>

#include "action_i.h"
#include "quac.h"

void action_ibutton_tx(void* context, const FuriString* action_path, FuriString* error) {
    App* app = context;
    if(!quac_duration_valid(app->settings.ibutton_duration)) {
        ACTION_SET_ERROR("iButton: Duration must be 100..60000 ms");
        return;
    }
    const char* cpath = furi_string_get_cstr(action_path);

    FURI_LOG_I(TAG, "iButton: Tx %s", cpath);

    iButtonProtocols* protocols = ibutton_protocols_alloc();
    iButtonKey* key = ibutton_key_alloc(ibutton_protocols_get_max_data_size(protocols));

    const bool success = ibutton_protocols_load(protocols, key, cpath);
    if(!success) {
        FURI_LOG_E(TAG, "Error loading iButton file %s", cpath);
        ACTION_SET_ERROR("Error loading %s", cpath);
    } else {
        FURI_LOG_I(TAG, "iButton: Starting...");
        iButtonWorker* worker = ibutton_worker_alloc(protocols);
        ibutton_worker_start_thread(worker);

        QuacActionWait* wait = quac_action_wait_alloc(app);
        ibutton_worker_emulate_start(worker, key);

        if(!quac_action_wait_run(wait, app->settings.ibutton_duration) && !app->action_cancelled)
            ACTION_SET_ERROR("iButton: Wait failed");

        FURI_LOG_I(TAG, "iButton: Done");
        ibutton_worker_stop(worker);
        ibutton_worker_stop_thread(worker);
        ibutton_worker_free(worker);
        quac_action_wait_free(wait);
    }

    ibutton_key_free(key);
    ibutton_protocols_free(protocols);
}
