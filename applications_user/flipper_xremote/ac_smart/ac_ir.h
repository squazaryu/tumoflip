#pragma once

#include <furi.h>
#include <storage/storage.h>
#include <infrared.h>
#include <infrared_worker.h>
#include <flipper_format/flipper_format.h>

#define AC_BUTTON_COUNT     6
#define AC_MAX_RAW_TIMINGS  1024

#define AC_MAX_PRESETS      8
#define AC_PRESET_NAME_LEN  16
#define AC_TEMP_BASE        10
#define AC_TEMP_SLOTS       32
#define AC_SWEEP_MAX        16
#define AC_OFF_NAME         "Off"

typedef enum {
    AcButtonOff,
    AcButtonMode,
    AcButtonTempUp,
    AcButtonTempDown,
    AcButtonFan,
    AcButtonVane,
} AcButton;

typedef enum {
    AcTypeSimple,
    AcTypeSmart,
} AcType;

extern const char* const ac_button_names[AC_BUTTON_COUNT];
extern const char* const ac_button_labels[AC_BUTTON_COUNT];

typedef struct {
    bool present;
    bool is_raw;
    InfraredMessage message;
    uint32_t* timings;
    size_t timings_size;
    uint32_t frequency;
    float duty_cycle;
} AcIrSignal;

typedef struct {
    AcIrSignal signals[AC_BUTTON_COUNT];
} AcRemote;

typedef struct {
    char name[AC_PRESET_NAME_LEN];
    uint32_t temp_bits; // bit i set = temp (AC_TEMP_BASE + i) captured
} AcPresetInfo;

typedef struct {
    bool has_off;
    uint8_t preset_count;
    AcPresetInfo presets[AC_MAX_PRESETS];
} AcSmartIndex;

void ac_ir_signal_reset(AcIrSignal* signal);
void ac_ir_signal_from_worker(AcIrSignal* signal, const InfraredWorkerSignal* worker_signal);
void ac_ir_signal_move(AcIrSignal* dst, AcIrSignal* src);
void ac_ir_signal_describe(const AcIrSignal* signal, FuriString* out);
void ac_ir_signal_send(const AcIrSignal* signal);

void ac_remote_reset(AcRemote* remote);
bool ac_remote_load(AcRemote* remote, Storage* storage, const char* path);
bool ac_remote_save(const AcRemote* remote, Storage* storage, const char* path);

// Smart AC files: signals named "Off" and "<preset> <temp>", stock .ir format.
// Signals are loaded from SD one at a time, only an index is kept in RAM.
void ac_smart_signal_name(char* out, size_t out_size, const char* preset, uint8_t temp);
bool ac_smart_name_parse(const char* name, char* preset, size_t preset_size, uint8_t* temp);
AcType ac_file_scan(Storage* storage, const char* path, AcSmartIndex* index);
bool ac_file_load_signal(AcIrSignal* signal, Storage* storage, const char* path, const char* name);
bool ac_smart_write_preset(
    Storage* storage,
    const char* path,
    bool create,
    const AcIrSignal* off_signal,
    const char* preset,
    uint8_t temp_start,
    const AcIrSignal* sweep,
    uint8_t count);
bool ac_smart_delete_preset(Storage* storage, const char* path, const char* preset);

// helpers on the temp bitmap
int32_t ac_temp_bits_lowest(uint32_t bits);
int32_t ac_temp_bits_next(uint32_t bits, uint8_t temp, bool up);
int32_t ac_temp_bits_nearest(uint32_t bits, uint8_t temp);
