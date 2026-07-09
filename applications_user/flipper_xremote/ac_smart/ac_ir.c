#include "ac_ir.h"

#include <infrared_transmit.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* const ac_button_names[AC_BUTTON_COUNT] = {
    "Off",
    "Mode",
    "Temp_up",
    "Temp_dn",
    "Fan",
    "Vane",
};

const char* const ac_button_labels[AC_BUTTON_COUNT] = {
    "OFF",
    "MODE",
    "TEMP +",
    "TEMP -",
    "FAN",
    "VANE",
};

void ac_ir_signal_reset(AcIrSignal* signal) {
    if(signal->timings) {
        free(signal->timings);
    }
    memset(signal, 0, sizeof(*signal));
}

void ac_ir_signal_from_worker(AcIrSignal* signal, const InfraredWorkerSignal* worker_signal) {
    ac_ir_signal_reset(signal);
    if(infrared_worker_signal_is_decoded(worker_signal)) {
        signal->is_raw = false;
        signal->message = *infrared_worker_get_decoded_signal(worker_signal);
    } else {
        const uint32_t* timings;
        size_t timings_size;
        infrared_worker_get_raw_signal(worker_signal, &timings, &timings_size);
        if(timings_size == 0 || timings_size > AC_MAX_RAW_TIMINGS) return;
        signal->is_raw = true;
        signal->timings = malloc(timings_size * sizeof(uint32_t));
        memcpy(signal->timings, timings, timings_size * sizeof(uint32_t));
        signal->timings_size = timings_size;
        signal->frequency = INFRARED_COMMON_CARRIER_FREQUENCY;
        signal->duty_cycle = INFRARED_COMMON_DUTY_CYCLE;
    }
    signal->present = true;
}

void ac_ir_signal_move(AcIrSignal* dst, AcIrSignal* src) {
    ac_ir_signal_reset(dst);
    *dst = *src;
    memset(src, 0, sizeof(*src));
}

void ac_ir_signal_describe(const AcIrSignal* signal, FuriString* out) {
    if(!signal->present) {
        furi_string_set(out, "none");
    } else if(signal->is_raw) {
        furi_string_printf(out, "RAW signal, %u samples", (unsigned)signal->timings_size);
    } else {
        furi_string_printf(
            out,
            "%s  A:%02lX C:%02lX",
            infrared_get_protocol_name(signal->message.protocol),
            signal->message.address,
            signal->message.command);
    }
}

void ac_ir_signal_send(const AcIrSignal* signal) {
    if(!signal->present) return;
    if(signal->is_raw) {
        infrared_send_raw_ext(
            signal->timings,
            signal->timings_size,
            true,
            signal->frequency,
            signal->duty_cycle);
    } else {
        infrared_send(&signal->message, 2);
    }
}

void ac_remote_reset(AcRemote* remote) {
    for(size_t i = 0; i < AC_BUTTON_COUNT; i++) {
        ac_ir_signal_reset(&remote->signals[i]);
    }
}

// Reads the signal body (type, protocol or raw data) that follows a "name" key.
// On success the signal is marked present. Leaves signal reset on failure.
static bool ac_ir_read_signal_body(FlipperFormat* ff, FuriString* str, AcIrSignal* signal) {
    memset(signal, 0, sizeof(*signal));
    bool ok = false;

    do {
        if(!flipper_format_read_string(ff, "type", str)) break;
        if(furi_string_equal(str, "parsed")) {
            if(!flipper_format_read_string(ff, "protocol", str)) break;
            signal->message.protocol = infrared_get_protocol_by_name(furi_string_get_cstr(str));
            if(!infrared_is_protocol_valid(signal->message.protocol)) break;
            uint32_t address, command;
            if(!flipper_format_read_hex(ff, "address", (uint8_t*)&address, 4)) break;
            if(!flipper_format_read_hex(ff, "command", (uint8_t*)&command, 4)) break;
            signal->message.address = address;
            signal->message.command = command;
            signal->message.repeat = false;
            signal->is_raw = false;
            ok = true;
        } else if(furi_string_equal(str, "raw")) {
            uint32_t count = 0;
            if(!flipper_format_read_uint32(ff, "frequency", &signal->frequency, 1)) break;
            if(!flipper_format_read_float(ff, "duty_cycle", &signal->duty_cycle, 1)) break;
            if(!flipper_format_get_value_count(ff, "data", &count)) break;
            if(count == 0 || count > AC_MAX_RAW_TIMINGS) break;
            signal->timings = malloc(count * sizeof(uint32_t));
            if(!flipper_format_read_uint32(ff, "data", signal->timings, count)) {
                free(signal->timings);
                signal->timings = NULL;
                break;
            }
            signal->timings_size = count;
            signal->is_raw = true;
            ok = true;
        }
    } while(false);

    if(ok) {
        signal->present = true;
    } else {
        ac_ir_signal_reset(signal);
    }
    return ok;
}

static bool ac_ir_write_signal(FlipperFormat* ff, const char* name, const AcIrSignal* signal) {
    bool ok = true;
    ok &= flipper_format_write_comment_cstr(ff, "");
    ok &= flipper_format_write_string_cstr(ff, "name", name);
    if(signal->is_raw) {
        uint32_t frequency = signal->frequency;
        float duty_cycle = signal->duty_cycle;
        ok &= flipper_format_write_string_cstr(ff, "type", "raw");
        ok &= flipper_format_write_uint32(ff, "frequency", &frequency, 1);
        ok &= flipper_format_write_float(ff, "duty_cycle", &duty_cycle, 1);
        ok &= flipper_format_write_uint32(ff, "data", signal->timings, signal->timings_size);
    } else {
        uint32_t address = signal->message.address;
        uint32_t command = signal->message.command;
        ok &= flipper_format_write_string_cstr(ff, "type", "parsed");
        ok &= flipper_format_write_string_cstr(
            ff, "protocol", infrared_get_protocol_name(signal->message.protocol));
        ok &= flipper_format_write_hex(ff, "address", (uint8_t*)&address, 4);
        ok &= flipper_format_write_hex(ff, "command", (uint8_t*)&command, 4);
    }
    return ok;
}

static bool ac_ir_open_read(FlipperFormat* ff, FuriString* str, const char* path) {
    uint32_t version = 0;
    if(!flipper_format_buffered_file_open_existing(ff, path)) return false;
    if(!flipper_format_read_header(ff, str, &version)) return false;
    return furi_string_equal(str, "IR signals file") && (version == 1);
}

bool ac_remote_load(AcRemote* remote, Storage* storage, const char* path) {
    ac_remote_reset(remote);
    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    FuriString* str = furi_string_alloc();
    bool success = false;

    if(ac_ir_open_read(ff, str, path)) {
        FuriString* name = furi_string_alloc();
        while(flipper_format_read_string(ff, "name", name)) {
            int32_t index = -1;
            for(size_t i = 0; i < AC_BUTTON_COUNT; i++) {
                if(furi_string_equal(name, ac_button_names[i])) {
                    index = i;
                    break;
                }
            }
            AcIrSignal tmp;
            memset(&tmp, 0, sizeof(tmp));
            if(!ac_ir_read_signal_body(ff, str, &tmp)) break;
            if(index >= 0) {
                ac_ir_signal_reset(&remote->signals[index]);
                remote->signals[index] = tmp;
                success = true;
            } else {
                ac_ir_signal_reset(&tmp);
            }
        }
        furi_string_free(name);
    }

    furi_string_free(str);
    flipper_format_free(ff);
    return success;
}

bool ac_remote_save(const AcRemote* remote, Storage* storage, const char* path) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool success = false;

    do {
        if(!flipper_format_file_open_always(ff, path)) break;
        if(!flipper_format_write_header_cstr(ff, "IR signals file", 1)) break;

        bool ok = true;
        for(size_t i = 0; i < AC_BUTTON_COUNT; i++) {
            const AcIrSignal* signal = &remote->signals[i];
            if(!signal->present) continue;
            ok &= ac_ir_write_signal(ff, ac_button_names[i], signal);
            if(!ok) break;
        }
        if(!ok) break;
        success = true;
    } while(false);

    flipper_format_free(ff);
    return success;
}

void ac_smart_signal_name(char* out, size_t out_size, const char* preset, uint8_t temp) {
    snprintf(out, out_size, "%s %u", preset, temp);
}

bool ac_smart_name_parse(const char* name, char* preset, size_t preset_size, uint8_t* temp) {
    const char* space = strrchr(name, ' ');
    if(!space || space == name) return false;
    char* end = NULL;
    long value = strtol(space + 1, &end, 10);
    if(!end || *end != '\0' || end == space + 1) return false;
    if(value < AC_TEMP_BASE || value >= AC_TEMP_BASE + AC_TEMP_SLOTS) return false;
    size_t len = space - name;
    if(len >= preset_size) len = preset_size - 1;
    memcpy(preset, name, len);
    preset[len] = '\0';
    *temp = value;
    return true;
}

AcType ac_file_scan(Storage* storage, const char* path, AcSmartIndex* index) {
    memset(index, 0, sizeof(*index));
    AcType type = AcTypeSimple;

    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    FuriString* str = furi_string_alloc();

    if(ac_ir_open_read(ff, str, path)) {
        FuriString* name = furi_string_alloc();
        char preset[AC_PRESET_NAME_LEN];
        uint8_t temp;
        while(flipper_format_read_string(ff, "name", name)) {
            const char* cname = furi_string_get_cstr(name);
            if(strcmp(cname, AC_OFF_NAME) == 0) {
                index->has_off = true;
            } else if(ac_smart_name_parse(cname, preset, sizeof(preset), &temp)) {
                type = AcTypeSmart;
                AcPresetInfo* info = NULL;
                for(uint8_t i = 0; i < index->preset_count; i++) {
                    if(strcmp(index->presets[i].name, preset) == 0) {
                        info = &index->presets[i];
                        break;
                    }
                }
                if(!info && index->preset_count < AC_MAX_PRESETS) {
                    info = &index->presets[index->preset_count++];
                    snprintf(info->name, sizeof(info->name), "%s", preset);
                }
                if(info) info->temp_bits |= 1UL << (temp - AC_TEMP_BASE);
            }
        }
        furi_string_free(name);
    }

    furi_string_free(str);
    flipper_format_free(ff);
    return type;
}

bool ac_file_load_signal(AcIrSignal* signal, Storage* storage, const char* path, const char* name) {
    ac_ir_signal_reset(signal);
    FlipperFormat* ff = flipper_format_buffered_file_alloc(storage);
    FuriString* str = furi_string_alloc();
    bool success = false;

    if(ac_ir_open_read(ff, str, path)) {
        FuriString* current = furi_string_alloc();
        while(flipper_format_read_string(ff, "name", current)) {
            if(furi_string_equal_str(current, name)) {
                success = ac_ir_read_signal_body(ff, str, signal);
                break;
            }
        }
        furi_string_free(current);
    }

    furi_string_free(str);
    flipper_format_free(ff);
    return success;
}

bool ac_smart_write_preset(
    Storage* storage,
    const char* path,
    bool create,
    const AcIrSignal* off_signal,
    const char* preset,
    uint8_t temp_start,
    const AcIrSignal* sweep,
    uint8_t count) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    char name[AC_PRESET_NAME_LEN + 8];
    bool success = false;

    do {
        bool ok = true;
        if(create) {
            if(!flipper_format_file_open_always(ff, path)) break;
            if(!flipper_format_write_header_cstr(ff, "IR signals file", 1)) break;
            if(off_signal && off_signal->present) {
                ok &= ac_ir_write_signal(ff, AC_OFF_NAME, off_signal);
            }
        } else {
            if(!flipper_format_file_open_append(ff, path)) break;
        }
        for(uint8_t i = 0; i < count && ok; i++) {
            if(!sweep[i].present) continue;
            ac_smart_signal_name(name, sizeof(name), preset, temp_start + i);
            ok &= ac_ir_write_signal(ff, name, &sweep[i]);
        }
        if(!ok) break;
        success = true;
    } while(false);

    flipper_format_free(ff);
    return success;
}

bool ac_smart_delete_preset(Storage* storage, const char* path, const char* preset) {
    char tmp_path[256];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FlipperFormat* src = flipper_format_buffered_file_alloc(storage);
    FlipperFormat* dst = flipper_format_file_alloc(storage);
    FuriString* str = furi_string_alloc();
    FuriString* name = furi_string_alloc();
    bool success = false;

    do {
        if(!ac_ir_open_read(src, str, path)) break;
        if(!flipper_format_file_open_always(dst, tmp_path)) break;
        if(!flipper_format_write_header_cstr(dst, "IR signals file", 1)) break;

        bool ok = true;
        char signal_preset[AC_PRESET_NAME_LEN];
        uint8_t temp;
        while(ok && flipper_format_read_string(src, "name", name)) {
            AcIrSignal signal;
            memset(&signal, 0, sizeof(signal));
            if(!ac_ir_read_signal_body(src, str, &signal)) break;
            const char* cname = furi_string_get_cstr(name);
            bool skip = ac_smart_name_parse(cname, signal_preset, sizeof(signal_preset), &temp) &&
                        strcmp(signal_preset, preset) == 0;
            if(!skip) ok &= ac_ir_write_signal(dst, cname, &signal);
            ac_ir_signal_reset(&signal);
        }
        if(!ok) break;
        success = true;
    } while(false);

    flipper_format_free(src);
    flipper_format_free(dst);
    furi_string_free(str);
    furi_string_free(name);

    if(success) {
        storage_common_remove(storage, path);
        success = storage_common_rename(storage, tmp_path, path) == FSE_OK;
    } else {
        storage_common_remove(storage, tmp_path);
    }
    return success;
}

int32_t ac_temp_bits_lowest(uint32_t bits) {
    for(uint8_t i = 0; i < AC_TEMP_SLOTS; i++) {
        if(bits & (1UL << i)) return AC_TEMP_BASE + i;
    }
    return -1;
}

int32_t ac_temp_bits_next(uint32_t bits, uint8_t temp, bool up) {
    int32_t slot = (int32_t)temp - AC_TEMP_BASE;
    if(up) {
        for(int32_t i = slot + 1; i < AC_TEMP_SLOTS; i++) {
            if(bits & (1UL << i)) return AC_TEMP_BASE + i;
        }
    } else {
        for(int32_t i = slot - 1; i >= 0; i--) {
            if(bits & (1UL << i)) return AC_TEMP_BASE + i;
        }
    }
    return -1;
}

int32_t ac_temp_bits_nearest(uint32_t bits, uint8_t temp) {
    int32_t best = -1;
    int32_t best_dist = 1000;
    for(uint8_t i = 0; i < AC_TEMP_SLOTS; i++) {
        if(!(bits & (1UL << i))) continue;
        int32_t t = AC_TEMP_BASE + i;
        int32_t dist = t > temp ? t - temp : temp - t;
        if(dist < best_dist) {
            best_dist = dist;
            best = t;
        }
    }
    return best;
}
