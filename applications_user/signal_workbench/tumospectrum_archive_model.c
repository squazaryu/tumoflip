#include "tumospectrum_archive_model.h"

#include <string.h>

TumoSpectrumArchiveSourceStatus tumospectrum_archive_source_status(
    bool fingerprinted,
    bool source_exists,
    bool source_readable,
    uint64_t expected_size,
    uint64_t actual_size,
    const uint8_t expected_digest[TUMOSPECTRUM_ARCHIVE_DIGEST_SIZE],
    const uint8_t actual_digest[TUMOSPECTRUM_ARCHIVE_DIGEST_SIZE]) {
    if(!source_exists) return TumoSpectrumArchiveSourceMissing;
    if(!source_readable) return TumoSpectrumArchiveSourceReadError;
    if(!fingerprinted || !expected_digest || !actual_digest) {
        return TumoSpectrumArchiveSourceUnverified;
    }
    if(expected_size != actual_size ||
       memcmp(expected_digest, actual_digest, TUMOSPECTRUM_ARCHIVE_DIGEST_SIZE) != 0) {
        return TumoSpectrumArchiveSourceChanged;
    }
    return TumoSpectrumArchiveSourceUnchanged;
}

const char* tumospectrum_archive_source_status_name(TumoSpectrumArchiveSourceStatus status) {
    switch(status) {
    case TumoSpectrumArchiveSourceUnchanged:
        return "OK";
    case TumoSpectrumArchiveSourceChanged:
        return "CHANGED";
    case TumoSpectrumArchiveSourceMissing:
        return "MISSING";
    case TumoSpectrumArchiveSourceReadError:
        return "READ ERROR";
    case TumoSpectrumArchiveSourceUnverified:
    default:
        return "UNVERIFIED";
    }
}
