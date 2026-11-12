#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    NfcFieldResourceNfc,
    NfcFieldResourceDmaIrq,
    NfcFieldResourceTimerIrq,
    NfcFieldResourceAwake,
    NfcFieldResourceCount,
} NfcFieldResource;
typedef struct {
    // A failed take must leave that resource unchanged/unowned by this lease.
    bool (*take)(void* context, NfcFieldResource resource);
    void (*drop)(void* context, NfcFieldResource resource);
} NfcFieldLeaseOps;
typedef struct {
    const NfcFieldLeaseOps* ops;
    void* context;
    uint8_t owned;
} NfcFieldLease;
bool nfc_field_lease_acquire(NfcFieldLease* lease, const NfcFieldLeaseOps* ops, void* context);
void nfc_field_lease_release(NfcFieldLease* lease);
