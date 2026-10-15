#pragma once
#include "rx_link.h"
#include <gui/canvas.h>

typedef enum {
    RxUiIdle,
    RxUiWaiting,
    RxUiLive,
    RxUiStopped,
    RxUiError
} RxUiPhase;
typedef enum {
    RxErrorNone,
    RxErrorUart,
    RxErrorRadio,
    RxErrorModule,
    RxErrorFirmware,
    RxErrorDisconnected,
    RxErrorRadioLost
} RxLinkError;
typedef struct {
    RxUiPhase phase;
    RxLinkError error;
    uint8_t frequency;
    uint8_t modulation;
    uint8_t page;
    uint32_t local_drops;
    uint32_t uart_errors;
    RxLinkSnapshot snapshot;
} RxLinkViewModel;

void rx_link_draw(Canvas* canvas, const RxLinkViewModel* model);
