#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

typedef size_t (*RawEditTextWidth)(void *context, const char *text);

static inline size_t raw_edit_filename_width(size_t available, size_t right_label)
{
    const size_t gap = 4;
    return available > gap && right_label < available - gap ?
               available - gap - right_label :
               0;
}

static inline bool raw_edit_utf8_continuation(char byte)
{
    return ((unsigned char)byte & 0xC0) == 0x80;
}

static inline void raw_edit_join_label(
    char *output, const char *name, size_t length, size_t prefix, size_t tail)
{
    memcpy(output, name, prefix);
    memcpy(output + prefix, "...", 3);
    memcpy(output + prefix + 3, name + tail, length - tail);
    output[prefix + 3 + length - tail] = '\0';
}

static inline bool raw_edit_label_candidate(
    char *output,
    size_t capacity,
    const char *name,
    size_t length,
    size_t prefix,
    size_t tail,
    size_t pixels,
    RawEditTextWidth measure,
    void *context)
{
    if (prefix >= tail || prefix + (length - tail) > capacity - sizeof("..."))
        return false;
    raw_edit_join_label(output, name, length, prefix, tail);
    return measure(context, output) <= pixels;
}

/* Preserve both ends of the real filename, not the bounded save-name buffer.
   Advance only at UTF-8 boundaries; use the actual font widths and no heap. */
static inline void raw_edit_fit_filename(
    char *output,
    size_t capacity,
    const char *path,
    size_t pixels,
    RawEditTextWidth measure,
    void *context)
{
    if (capacity == 0)
        return;
    output[0] = '\0';
    if (!path || pixels == 0)
        return;

    const char *slash = strrchr(path, '/');
    const char *name = slash ? slash + 1 : path;
    size_t length = strlen(name);
    if (length > 4 && name[length - 4] == '.' &&
        (name[length - 3] | 0x20) == 's' && (name[length - 2] | 0x20) == 'u' &&
        (name[length - 1] | 0x20) == 'b')
        length -= 4;

    if (length < capacity)
    {
        memcpy(output, name, length);
        output[length] = '\0';
        if (measure(context, output) <= pixels)
            return;
    }
    if (capacity < sizeof("...") || measure(context, "...") > pixels)
    {
        output[0] = '\0';
        return;
    }

    size_t prefix = 0;
    size_t tail = length;
    bool add_prefix = true;
    bool add_tail = true;
    while (add_prefix || add_tail)
    {
        if (add_tail)
        {
            size_t next = tail - 1;
            while (next > prefix && raw_edit_utf8_continuation(name[next]))
                next--;
            add_tail = raw_edit_label_candidate(
                output, capacity, name, length, prefix, next, pixels, measure, context);
            if (add_tail)
                tail = next;
        }
        if (add_prefix)
        {
            size_t next = prefix + 1;
            while (next < tail && raw_edit_utf8_continuation(name[next]))
                next++;
            add_prefix = raw_edit_label_candidate(
                output, capacity, name, length, next, tail, pixels, measure, context);
            if (add_prefix)
                prefix = next;
        }
    }
    raw_edit_join_label(output, name, length, prefix, tail);
}
