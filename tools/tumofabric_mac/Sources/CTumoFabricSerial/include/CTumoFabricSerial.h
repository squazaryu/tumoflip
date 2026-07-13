#pragma once

#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

int tf_serial_open(const char* path);
int tf_serial_close(int descriptor);
ssize_t tf_serial_write_all(int descriptor, const void* bytes, size_t count);
ssize_t tf_serial_read_until(
    int descriptor,
    void* buffer,
    size_t capacity,
    const char* marker,
    int timeout_ms);

#ifdef __cplusplus
}
#endif
