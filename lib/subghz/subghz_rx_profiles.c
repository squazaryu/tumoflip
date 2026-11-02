#include "subghz_rx_profiles.h"

static const uint8_t subghz_rx_honda_preset[] = {
    0x02, 0x0D, 0x0B, 0x06, 0x08, 0x32, 0x07, 0x04, 0x14, 0x00, 0x13, 0x02, 0x12, 0x07, 0x11, 0x36,
    0x10, 0xE9, 0x15, 0x32, 0x18, 0x18, 0x19, 0x16, 0x1D, 0x92, 0x1C, 0x40, 0x1B, 0x03, 0x20, 0xFB,
    0x22, 0x10, 0x21, 0x56, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Startup/profile selection is not a hot path. Share this code and its preset
// bytes instead of cloning both into the allocator and the profile picker.
__attribute__((noinline)) bool subghz_rx_profiles_init(SubGhzSetting* setting) {
    const int existing = subghz_setting_get_inx_preset_by_name(setting, SUBGHZ_RX_CUSTOM_NAME);
    if(existing >= 0) {
        // Never overwrite or silently adopt a user preset with the same name.
        return subghz_setting_get_preset_data_size(setting, existing) ==
                   sizeof(subghz_rx_honda_preset) &&
               memcmp(
                   subghz_setting_get_preset_data(setting, existing),
                   subghz_rx_honda_preset,
                   sizeof(subghz_rx_honda_preset)) == 0;
    }
    FlipperFormat* data = flipper_format_string_alloc();
    const bool ok =
        flipper_format_write_hex(
            data, "Custom_preset_data", subghz_rx_honda_preset, sizeof(subghz_rx_honda_preset)) &&
        flipper_format_rewind(data) &&
        subghz_setting_load_custom_preset(setting, SUBGHZ_RX_CUSTOM_NAME, data);
    flipper_format_free(data);
    return ok;
}
