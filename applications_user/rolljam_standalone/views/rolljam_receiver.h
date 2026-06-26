// views/rolljam_receiver.h
#pragma once

#include <gui/view.h>
#include "../helpers/rolljam_types.h"

typedef struct RollJamReceiver RollJamReceiver;
typedef struct RollJamHistory RollJamHistory;

typedef void (*RollJamReceiverCallback)(RollJamCustomEvent event, void* context);

void rolljam_view_receiver_set_callback(
    RollJamReceiver* receiver,
    RollJamReceiverCallback callback,
    void* context);

RollJamReceiver* rolljam_view_receiver_alloc(bool auto_save);
void rolljam_view_receiver_free(RollJamReceiver* receiver);
View* rolljam_view_receiver_get_view(RollJamReceiver* receiver);

void rolljam_view_receiver_add_data_statusbar(
    RollJamReceiver* receiver,
    const char* frequency_str,
    const char* preset_str,
    const char* history_stat_str,
    bool external_radio);

uint16_t rolljam_view_receiver_get_idx_menu(RollJamReceiver* receiver);
void rolljam_view_receiver_set_idx_menu(RollJamReceiver* receiver, uint16_t idx);
void rolljam_view_receiver_set_rssi(RollJamReceiver* receiver, float rssi);
void rolljam_view_receiver_set_lock(RollJamReceiver* receiver, RollJamLock lock);
void rolljam_view_receiver_set_autosave(RollJamReceiver* receiver, bool auto_save);
void rolljam_view_receiver_set_history_mutex(
    RollJamReceiver* receiver,
    FuriMutex* history_mutex);
void rolljam_view_receiver_set_sub_decode_mode(
    RollJamReceiver* receiver,
    bool sub_decode_mode);
void rolljam_view_receiver_reset_menu(RollJamReceiver* receiver);

void rolljam_view_receiver_sync_menu_from_history(
    RollJamReceiver* receiver,
    RollJamHistory* history);

void rolljam_view_receiver_pop_first_menu_item(RollJamReceiver* receiver);
void rolljam_view_receiver_delete_item(RollJamReceiver* receiver, uint16_t idx);

void rolljam_view_receiver_append_menu_row_from_history(
    RollJamReceiver* receiver,
    RollJamHistory* history,
    uint16_t idx);
