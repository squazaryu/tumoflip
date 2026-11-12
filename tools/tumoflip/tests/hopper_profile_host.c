/* Actual Standard hopping + RX profile catalog, with radio/storage replaced by spies. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define furi_assert assert
#define MAX_PRESETS 16U
#define PRESET_BYTES 128U

enum {
    SubGhzHopperStateOFF,
    SubGhzHopperStatePause,
    SubGhzHopperStateRunning,
    SubGhzHopperStateRSSITimeOut,
};
enum {SubGhzTxRxStateIDLE, SubGhzTxRxStateRx, SubGhzTxRxStateTx};
enum {SubGhzRadioDeviceTypeInternal, SubGhzRadioDeviceTypeAuto};
enum {FuriHalSubGhzPresetCustom};

typedef struct {
    const char* name;
    uint8_t bytes[PRESET_BYTES];
    size_t size;
} Preset;
typedef struct {
    Preset presets[MAX_PRESETS];
    size_t count;
    size_t configured[MAX_PRESETS];
    size_t configured_count;
    uint32_t frequencies[3];
    size_t frequency_count;
} SubGhzSetting;
typedef struct {uint8_t bytes[PRESET_BYTES]; size_t size;} FlipperFormat;
typedef struct {uint32_t frequency; uint8_t* data;} SubGhzRadioPreset;
typedef struct {
    unsigned hopper_state, txrx_state, radio_device_type;
    unsigned hopper_timeout, hopper_hold_ticks;
    size_t hopper_idx_frequency, preset_hopper_idx;
    SubGhzSetting* setting;
    SubGhzRadioPreset* preset;
    void* radio_device;
    void* diversity_radio_device;
    size_t selected;
    bool rx_profile_preset_owned;
} SubGhzTxRx;

static unsigned loads, stops, starts, resets, allocations, lookups;
static float rssi = -100;
static FlipperFormat format;
static uint8_t user_data[44] = {0xA5};

static int subghz_setting_get_inx_preset_by_name(SubGhzSetting* s, const char* name) {
    lookups++;
    for(size_t i = 0; i < s->count; i++) if(!strcmp(s->presets[i].name, name)) return (int)i;
    return -1;
}
static uint8_t* subghz_setting_get_preset_data(SubGhzSetting* s, size_t i) {
    assert(i < s->count); return s->presets[i].bytes;
}
static size_t subghz_setting_get_preset_data_size(SubGhzSetting* s, size_t i) {
    assert(i < s->count); return s->presets[i].size;
}
static FlipperFormat* flipper_format_string_alloc(void) {
    assert(!allocations); allocations++; return &format;
}
static bool flipper_format_write_hex(FlipperFormat* f, const char* k, const uint8_t* b, size_t n) {
    (void)k; assert(n <= sizeof(f->bytes)); memcpy(f->bytes, b, n); f->size = n; return true;
}
static bool flipper_format_rewind(FlipperFormat* f) {(void)f; return true;}
static void flipper_format_free(FlipperFormat* f) {(void)f; assert(allocations); allocations--;}
static void add_preset(SubGhzSetting* s, const char* name, const uint8_t* b, size_t n) {
    assert(s->count < MAX_PRESETS && n <= PRESET_BYTES);
    Preset* p = &s->presets[s->count++]; p->name = name; p->size = n; memcpy(p->bytes, b, n);
}
static bool subghz_setting_load_custom_preset(SubGhzSetting* s, const char* name, FlipperFormat* f) {
    add_preset(s, name, f->bytes, f->size); return true;
}
static size_t subghz_setting_get_preset_count(SubGhzSetting* s) {return s->count;}
static size_t subghz_setting_get_hopper_preset_count(SubGhzSetting* s) {return s->configured_count;}
static size_t subghz_setting_get_hopper_preset_index(SubGhzSetting* s, size_t i) {
    assert(i < s->configured_count); return s->configured[i];
}
static size_t subghz_setting_get_hopper_frequency_count(SubGhzSetting* s) {return s->frequency_count;}
static uint32_t subghz_setting_get_hopper_frequency(SubGhzSetting* s, size_t i) {
    assert(i < s->frequency_count); return s->frequencies[i];
}
static float subghz_txrx_radio_device_get_rssi(SubGhzTxRx* x) {(void)x; return rssi;}
static void subghz_txrx_rx_end(SubGhzTxRx* x) {stops++; x->txrx_state = SubGhzTxRxStateIDLE;}
static void subghz_txrx_receiver_reset(SubGhzTxRx* x) {(void)x; resets++;}
static void subghz_txrx_set_preset_internal(SubGhzTxRx* x, uint32_t f, size_t i, int power) {
    (void)power; assert(i < x->setting->count); x->selected = i;
    x->preset->frequency = f; x->preset->data = x->setting->presets[i].bytes;
}
static void subghz_devices_load_preset(void* d, int p, uint8_t* bytes) {
    assert(d && p == FuriHalSubGhzPresetCustom && bytes); loads++;
}
static void subghz_txrx_rx(SubGhzTxRx* x, uint32_t f) {
    x->preset->frequency = f; x->txrx_state = SubGhzTxRxStateRx; starts++;
}

/* ACTUAL_PRODUCTION */

static void init_profiles(SubGhzTxRx* instance) {
    /* ACTUAL_PROFILE_INIT */
}

static void reset(SubGhzSetting* s) {
    memset(s, 0, sizeof(*s));
    s->frequencies[0] = 315000000; s->frequencies[1] = 433920000;
    s->frequencies[2] = 868350000; s->frequency_count = 3;
    const char* names[] = {"AM270", "AM650", "FM238", "FM476", "FM12K"};
    for(size_t i = 0; i < 5; i++) add_preset(s, names[i], user_data, sizeof(user_data));
    loads = stops = starts = resets = lookups = 0; rssi = -100;
}
static SubGhzTxRx receiver(SubGhzSetting* s, SubGhzRadioPreset* p) {
    SubGhzTxRx instance = {.hopper_state = SubGhzHopperStateRunning,
        .txrx_state = SubGhzTxRxStateRx, .setting = s, .preset = p,
        .radio_device = (void*)1, .selected = 0};
    init_profiles(&instance);
    return instance;
}
static void tick(SubGhzTxRx* x, bool frequency, bool preset) {
    x->hopper_timeout = 0;
    subghz_txrx_hopper_update(x, -85, frequency, preset);
}
static void cycle_with_pa(SubGhzSetting* s, const size_t* expected, size_t count, bool frequency, int pa) {
    SubGhzRadioPreset p = {.frequency = s->frequencies[0]};
    SubGhzTxRx x = receiver(s, &p);
    if(pa >= 0) s->presets[5].bytes[s->presets[5].size - 8 + (size_t)pa] ^= 0x5A;
    SubGhzSetting saved = *s;
    x.radio_device_type = SubGhzRadioDeviceTypeAuto; x.diversity_radio_device = (void*)2;
    for(size_t step = 1; step <= count * 3; step++) {
        unsigned previous_loads = loads;
        tick(&x, frequency, true);
        assert(x.selected == expected[step % count]);
        assert(loads == previous_loads + 2);
        assert(p.frequency == s->frequencies[frequency ? (step / count) % 3 : 0]);
    }
    assert(!memcmp(s, &saved, sizeof(*s))); // No preset/config list rewrite.
    assert(!allocations);
}
static void cycle(SubGhzSetting* s, const size_t* expected, size_t count, bool frequency) {
    cycle_with_pa(s, expected, count, frequency, -1);
}

static void automatic(void) {
    for(size_t position = 0; position < 8; position++) {
        for(unsigned mode = 0; mode < 2; mode++) {
            SubGhzSetting s; reset(&s);
            add_preset(&s, "User FSK", user_data, sizeof(user_data));
            add_preset(&s, "User OOK", user_data, sizeof(user_data));
            assert(subghz_rx_profiles_init(&s));
            Preset honda = s.presets[7];
            memmove(s.presets + position + 1, s.presets + position, (7 - position) * sizeof(Preset));
            s.presets[position] = honda;
            size_t expected[7], n = 0;
            for(size_t i = 0; i < s.count; i++) if(i != position) expected[n++] = i;
            cycle(&s, expected, n, mode != 0);
        }
    }
}
static void configured(void) {
    for(unsigned mode = 0; mode < 2; mode++) {
        SubGhzSetting s; reset(&s); assert(subghz_rx_profiles_init(&s));
        s.configured[0] = 5; s.configured[1] = 2; s.configured[2] = 1; s.configured[3] = 5;
        s.configured_count = 4;
        cycle(&s, s.configured, s.configured_count, mode != 0);
    }
}
static void automatic_power(void) {
    for(size_t byte = 0; byte < 8; byte++) {
        SubGhzSetting s; reset(&s); assert(subghz_rx_profiles_init(&s));
        // TX power edits the trailing PA table in place, not the RX registers.
        const size_t expected[] = {0, 1, 2, 3, 4};
        cycle_with_pa(&s, expected, 5, true, (int)byte);
    }
}
static void custom(void) {
    SubGhzSetting s; reset(&s);
    const size_t ordinary[] = {0, 1, 2, 3, 4}; cycle(&s, ordinary, 5, true);
    reset(&s);
    add_preset(&s, "TumoHonda", user_data, sizeof(user_data));
    assert(!subghz_rx_profiles_init(&s)); // Same name is not sufficient ownership.
    const size_t user[] = {0, 1, 2, 3, 4, 5}; cycle(&s, user, 6, false);
    s.presets[5].size--; cycle(&s, user, 6, true);
}
static void custom_power(void) {
    SubGhzSetting s; reset(&s);
    add_preset(&s, "TumoHonda", subghz_rx_honda_preset, sizeof(subghz_rx_honda_preset));
    s.presets[5].bytes[s.presets[5].size - 8] ^= 0x5A;
    // This collision existed before initialization; it is not an app-owned preset.
    const size_t expected[] = {0, 1, 2, 3, 4, 5}; cycle(&s, expected, 6, true);
}
static void frequency_only(void) {
    SubGhzSetting s; reset(&s); assert(subghz_rx_profiles_init(&s));
    SubGhzRadioPreset p = {.frequency = s.frequencies[0], .data = s.presets[5].bytes};
    SubGhzTxRx x = receiver(&s, &p); x.selected = 5; lookups = loads = 0;
    for(size_t step = 1; step < 10; step++) {
        tick(&x, true, false); assert(x.selected == 5 && loads == 0 && lookups == 0);
        assert(p.data == s.presets[5].bytes && p.frequency == s.frequencies[step % 3]);
    }
}
static void explicit_profile(void) {
    SubGhzSetting s; reset(&s);
    assert(subghz_rx_profile_preset_index(&s, 7) == 5 && s.count == 6);
    assert(subghz_rx_profile_preset_index(&s, 8) == 5 && s.count == 6);
    assert(subghz_rx_profile_get(7)->frequency == 433920000);
    assert(subghz_rx_profile_get(8)->frequency == 315000000);
    assert(!memcmp(s.presets[5].bytes, subghz_rx_honda_preset, sizeof(subghz_rx_honda_preset)));
    const size_t expected[] = {0, 1, 2, 3, 4}; cycle(&s, expected, 5, true);
    assert(subghz_rx_profile_preset_index(&s, 7) == 5 && s.count == 6);
}
static void boundaries(void) {
    SubGhzSetting s; reset(&s); assert(subghz_rx_profiles_init(&s));
    SubGhzRadioPreset p = {.frequency = s.frequencies[0]};
    SubGhzTxRx x = receiver(&s, &p);
    s.presets[0] = s.presets[5]; s.count = 1;
    tick(&x, true, true); assert(!loads && !stops && !starts && !resets);
    s.count = 0; tick(&x, false, true); assert(!loads && !stops);
    reset(&s); add_preset(&s, "User only", user_data, 2);
    x = receiver(&s, &p); x.preset_hopper_idx = 255; tick(&x, true, true);
    assert(x.selected == 0 && x.hopper_idx_frequency == 1);
    reset(&s); x = receiver(&s, &p);
    x.hopper_state = SubGhzHopperStateOFF; tick(&x, true, true); assert(!loads);
    x.hopper_state = SubGhzHopperStatePause; tick(&x, true, true); assert(!loads);
    x.hopper_state = SubGhzHopperStateRunning; tick(&x, false, false); assert(!loads);
    x.hopper_timeout = 1; subghz_txrx_hopper_update(&x, -85, true, true); assert(!loads);
    rssi = -60; tick(&x, true, true); assert(x.hopper_state == SubGhzHopperStateRSSITimeOut && !loads);
    tick(&x, true, true); assert(!loads);
    rssi = -100; x.hopper_timeout = 1;
    subghz_txrx_hopper_update(&x, -85, true, true); assert(!loads);
    x.hopper_timeout = 0; subghz_txrx_hopper_update(&x, -85, true, true); assert(loads == 1);
    x.hopper_state = SubGhzHopperStateRSSITimeOut; x.hopper_hold_ticks = SUBGHZ_HOPPER_MAX_HOLD_TICKS;
    rssi = -60; tick(&x, true, true); assert(loads == 2);
    rssi = -100; x.hopper_state = SubGhzHopperStateRunning; x.txrx_state = SubGhzTxRxStateTx;
    tick(&x, true, true); assert(loads == 2);
    x.txrx_state = SubGhzTxRxStateIDLE; x.preset->frequency = s.frequencies[0];
    tick(&x, false, true); assert(loads == 3);
}

int main(int argc, char** argv) {
    assert(argc == 2);
    if(!strcmp(argv[1], "automatic")) automatic();
    else if(!strcmp(argv[1], "configured")) configured();
    else if(!strcmp(argv[1], "power")) automatic_power();
    else if(!strcmp(argv[1], "custom")) custom();
    else if(!strcmp(argv[1], "custom-power")) custom_power();
    else if(!strcmp(argv[1], "frequency")) frequency_only();
    else if(!strcmp(argv[1], "profile")) explicit_profile();
    else if(!strcmp(argv[1], "boundaries")) boundaries();
    else assert(false);
    return 0;
}
