#pragma once

#include <storage/storage.h>
#include <toolbox/stream/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Replace an existing signal only after saving and verifying its old contents.
 * A pre-existing .tumo-bak is never overwritten. On a failed update a verified
 * backup is retained, even if rollback succeeds. This is recovery, not an atomic
 * power-loss transaction. Caller must serialize writes to this path.
 */
bool signal_file_update(Storage* storage, Stream* source, const char* path);

#ifdef __cplusplus
}
#endif
