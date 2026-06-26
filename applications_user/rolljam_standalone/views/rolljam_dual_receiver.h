// views/rolljam_dual_receiver.h
#pragma once

#include "../helpers/rolljam_types.h"

#ifdef ENABLE_DUAL_RX_SCENE

#include <gui/view.h>

typedef struct RollJamDualReceiver RollJamDualReceiver;
typedef struct RollJamHistory RollJamHistory;

typedef void (*RollJamDualReceiverCallback)(RollJamCustomEvent event, void* context);

RollJamDualReceiver* rolljam_view_dual_receiver_alloc(void);
void rolljam_view_dual_receiver_free(RollJamDualReceiver* receiver);
View* rolljam_view_dual_receiver_get_view(RollJamDualReceiver* receiver);

void rolljam_view_dual_receiver_set_callback(
    RollJamDualReceiver* receiver,
    RollJamDualReceiverCallback callback,
    void* context);

void rolljam_view_dual_receiver_set_history(
    RollJamDualReceiver* receiver,
    RollJamHistory* history);

void rolljam_view_dual_receiver_set_history_mutex(
    RollJamDualReceiver* receiver,
    FuriMutex* mutex);

void rolljam_view_dual_receiver_set_chain_status(
    RollJamDualReceiver* receiver,
    uint8_t slot,
    const char* tag,
    const char* frequency_str,
    const char* modulation_str,
    bool external);

void rolljam_view_dual_receiver_set_rssi(
    RollJamDualReceiver* receiver,
    uint8_t slot,
    float rssi);

void rolljam_view_dual_receiver_set_history_stat(
    RollJamDualReceiver* receiver,
    const char* history_stat_str);

void rolljam_view_dual_receiver_sync_menu_from_history(
    RollJamDualReceiver* receiver,
    RollJamHistory* history);

void rolljam_view_dual_receiver_reset_menu(RollJamDualReceiver* receiver);

uint16_t rolljam_view_dual_receiver_get_idx_menu(RollJamDualReceiver* receiver);
void rolljam_view_dual_receiver_set_idx_menu(RollJamDualReceiver* receiver, uint16_t idx);
void rolljam_view_dual_receiver_delete_item(RollJamDualReceiver* receiver, uint16_t idx);

#endif // ENABLE_DUAL_RX_SCENE
