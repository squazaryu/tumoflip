#pragma once

#include "subghz_radio_broker.h"


/** Internal, append-only broker state used by built-in Tumoflip components. */
typedef struct {
    SubGhzRadioBrokerStatus base;
    SubGhzRadioBrokerState state;
    uint32_t session_id;
    uint32_t sequence;
} SubGhzRadioBrokerStatusInternal;

void subghz_radio_broker_get_status_internal(
    SubGhzRadioBroker* broker,
    SubGhzRadioBrokerStatusInternal* status);
