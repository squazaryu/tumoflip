#include "CTumoFabricSerial.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static int64_t tf_monotonic_ms(void) {
    struct timespec time;
    if(clock_gettime(CLOCK_MONOTONIC, &time) != 0) return 0;
    return (int64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}

static bool tf_contains(const char* bytes, size_t count, const char* marker) {
    const size_t marker_size = strlen(marker);
    if(marker_size == 0 || count < marker_size) return false;

    for(size_t offset = 0; offset <= count - marker_size; offset++) {
        if(memcmp(&bytes[offset], marker, marker_size) == 0) return true;
    }
    return false;
}

int tf_serial_open(const char* path) {
    if(!path || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    const int descriptor = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(descriptor < 0) return -1;

    if(ioctl(descriptor, TIOCEXCL) != 0) {
        const int error = errno;
        close(descriptor);
        errno = error;
        return -1;
    }

    struct termios options;
    if(tcgetattr(descriptor, &options) != 0) {
        const int error = errno;
        close(descriptor);
        errno = error;
        return -1;
    }

    cfmakeraw(&options);
    cfsetispeed(&options, B115200);
    cfsetospeed(&options, B115200);
    options.c_cflag |= CLOCAL | CREAD;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 0;
    if(tcsetattr(descriptor, TCSANOW, &options) != 0 || tcflush(descriptor, TCIOFLUSH) != 0 ||
       fcntl(descriptor, F_SETFL, 0) != 0) {
        const int error = errno;
        close(descriptor);
        errno = error;
        return -1;
    }

    return descriptor;
}

int tf_serial_close(int descriptor) {
    if(descriptor < 0) return 0;
    ioctl(descriptor, TIOCNXCL);
    return close(descriptor);
}

ssize_t tf_serial_write_all(int descriptor, const void* bytes, size_t count) {
    if(descriptor < 0 || !bytes) {
        errno = EINVAL;
        return -1;
    }

    size_t written = 0;
    while(written < count) {
        const ssize_t result = write(descriptor, (const uint8_t*)bytes + written, count - written);
        if(result > 0) {
            written += (size_t)result;
        } else if(result < 0 && errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }
    return (ssize_t)written;
}

ssize_t tf_serial_read_until(
    int descriptor,
    void* buffer,
    size_t capacity,
    const char* marker,
    int timeout_ms) {
    if(descriptor < 0 || !buffer || capacity < 2 || !marker || timeout_ms <= 0) {
        errno = EINVAL;
        return -1;
    }

    char* output = buffer;
    size_t used = 0;
    const int64_t deadline = tf_monotonic_ms() + timeout_ms;
    while(used < capacity - 1) {
        const int64_t remaining = deadline - tf_monotonic_ms();
        if(remaining <= 0) {
            output[used] = '\0';
            errno = ETIMEDOUT;
            return -2;
        }

        struct pollfd item = {.fd = descriptor, .events = POLLIN, .revents = 0};
        const int poll_result = poll(&item, 1, (int)remaining);
        if(poll_result == 0) continue;
        if(poll_result < 0) {
            if(errno == EINTR) continue;
            return -1;
        }
        if(item.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            errno = EIO;
            return -1;
        }

        const ssize_t count = read(descriptor, &output[used], capacity - 1 - used);
        if(count > 0) {
            used += (size_t)count;
            output[used] = '\0';
            if(tf_contains(output, used, marker)) return (ssize_t)used;
        } else if(count == 0 || errno == EAGAIN || errno == EINTR) {
            continue;
        } else {
            return -1;
        }
    }

    output[used] = '\0';
    errno = ENOBUFS;
    return -1;
}
