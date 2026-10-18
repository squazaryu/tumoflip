#pragma once

#include "subghz_file_encoder_worker.h"

typedef enum {
    SubGhzRawDecodeSample,
    SubGhzRawDecodeWait,
    SubGhzRawDecodeEnd,
    SubGhzRawDecodeGap,
} SubGhzRawDecodeResult;

/* Shared by Standard Auto Decode and offline FAP analysis. A caller must launch
 * its worker with a NULL radio device, bound work per tick and own teardown. */
static inline SubGhzRawDecodeResult subghz_raw_decode_sample(
    SubGhzFileEncoderWorker* worker, bool* level, uint32_t* duration) {
    const LevelDuration sample = subghz_file_encoder_worker_get_level_duration(worker);
    if(level_duration_is_reset(sample)) return SubGhzRawDecodeEnd;
    if(level_duration_is_wait(sample)) return SubGhzRawDecodeWait;
    *duration = level_duration_get_duration(sample);
    if(!*duration || *duration > 1000000U) return SubGhzRawDecodeGap;
    *level = level_duration_get_level(sample);
    return SubGhzRawDecodeSample;
}
