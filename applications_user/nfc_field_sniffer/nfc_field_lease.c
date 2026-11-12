#include "nfc_field_lease.h"
#include <stddef.h>

void nfc_field_lease_release(NfcFieldLease* lease) {
    if(!lease || !lease->ops) return;
    for(unsigned i = NfcFieldResourceCount; i > 0; i--) {
        uint8_t mask = (uint8_t)(1U << (i - 1));
        if(!(lease->owned & mask)) continue;
        lease->owned &= (uint8_t)~mask;
        lease->ops->drop(lease->context, (NfcFieldResource)(i - 1));
    }
    lease->ops = NULL;
    lease->context = NULL;
}
bool nfc_field_lease_acquire(NfcFieldLease* lease, const NfcFieldLeaseOps* ops, void* context) {
    if(!lease || lease->owned || !ops || !ops->take || !ops->drop) return false;
    lease->ops = ops;
    lease->context = context;
    for(unsigned i = 0; i < NfcFieldResourceCount; i++) {
        if(!ops->take(context, (NfcFieldResource)i)) {
            nfc_field_lease_release(lease);
            return false;
        }
        lease->owned |= (uint8_t)(1U << i);
    }
    return true;
}
