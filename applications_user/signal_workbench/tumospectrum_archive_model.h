#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMOSPECTRUM_ARCHIVE_DIGEST_SIZE 32U

typedef enum {
    TumoSpectrumArchiveSourceUnverified,
    TumoSpectrumArchiveSourceUnchanged,
    TumoSpectrumArchiveSourceChanged,
    TumoSpectrumArchiveSourceMissing,
    TumoSpectrumArchiveSourceReadError,
} TumoSpectrumArchiveSourceStatus;

TumoSpectrumArchiveSourceStatus tumospectrum_archive_source_status(
    bool fingerprinted,
    bool source_exists,
    bool source_readable,
    uint64_t expected_size,
    uint64_t actual_size,
    const uint8_t expected_digest[TUMOSPECTRUM_ARCHIVE_DIGEST_SIZE],
    const uint8_t actual_digest[TUMOSPECTRUM_ARCHIVE_DIGEST_SIZE]);

const char* tumospectrum_archive_source_status_name(TumoSpectrumArchiveSourceStatus status);
