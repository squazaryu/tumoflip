// views/protopirate_dual_receiver.h
#pragma once

#include "../helpers/protopirate_types.h"

#ifdef ENABLE_DUAL_RX_SCENE

#include <gui/view.h>

typedef struct ProtoPirateDualReceiver ProtoPirateDualReceiver;
typedef struct ProtoPirateHistory ProtoPirateHistory;

typedef void (*ProtoPirateDualReceiverCallback)(ProtoPirateCustomEvent event, void* context);

ProtoPirateDualReceiver* protopirate_view_dual_receiver_alloc(void);
void protopirate_view_dual_receiver_free(ProtoPirateDualReceiver* receiver);
View* protopirate_view_dual_receiver_get_view(ProtoPirateDualReceiver* receiver);

void protopirate_view_dual_receiver_set_callback(
    ProtoPirateDualReceiver* receiver,
    ProtoPirateDualReceiverCallback callback,
    void* context);

void protopirate_view_dual_receiver_set_history(
    ProtoPirateDualReceiver* receiver,
    ProtoPirateHistory* history);

void protopirate_view_dual_receiver_set_history_mutex(
    ProtoPirateDualReceiver* receiver,
    FuriMutex* mutex);

void protopirate_view_dual_receiver_set_chain_status(
    ProtoPirateDualReceiver* receiver,
    uint8_t slot,
    const char* tag,
    const char* frequency_str,
    const char* modulation_str,
    bool external);

void protopirate_view_dual_receiver_set_rssi(
    ProtoPirateDualReceiver* receiver,
    uint8_t slot,
    float rssi);

void protopirate_view_dual_receiver_set_history_stat(
    ProtoPirateDualReceiver* receiver,
    const char* history_stat_str);

void protopirate_view_dual_receiver_sync_menu_from_history(
    ProtoPirateDualReceiver* receiver,
    ProtoPirateHistory* history);

void protopirate_view_dual_receiver_reset_menu(ProtoPirateDualReceiver* receiver);

uint16_t protopirate_view_dual_receiver_get_idx_menu(ProtoPirateDualReceiver* receiver);
void protopirate_view_dual_receiver_set_idx_menu(ProtoPirateDualReceiver* receiver, uint16_t idx);
void protopirate_view_dual_receiver_delete_item(ProtoPirateDualReceiver* receiver, uint16_t idx);

#endif // ENABLE_DUAL_RX_SCENE
