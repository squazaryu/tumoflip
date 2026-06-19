/**
 * Scene: CarEmulateSettings
 * Toggle: Custom Emulate  Off / On
 * Selector: TX Power (reuses the same table as Radio Settings)
 * Both settings are persisted in SubGhzLastSettings.
 */
#include "../subghz_i.h"
#include <lib/toolbox/value_index.h>

#define TAG "SubGhzCarEmulateSettings"

/* ── Toggle ──────────────────────────────────────────────────────────────── */
static const char* const toggle_text[] = {"Off", "On"};

static void subghz_scene_car_emulate_settings_toggle_changed(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    furi_assert(subghz);

    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, toggle_text[index]);

    subghz->last_settings->custom_car_emulate = (index == 1);
    subghz_last_settings_save(subghz->last_settings);
}

/* ── TX Power ────────────────────────────────────────────────────────────── */
/* Must match the table in subghz_scene_radio_settings.c exactly */
#define CE_TX_POWER_COUNT 9
static const char* const ce_tx_power_text[CE_TX_POWER_COUNT] = {
    "Preset",   /* index 0 → use whatever the preset has baked in */
    "10dBm +",
    "7dBm",
    "5dBm",
    "0dBm",
    "-10dBm",
    "-15dBm",
    "-20dBm",
    "-30dBm",
};

static void subghz_scene_car_emulate_settings_power_changed(VariableItem* item) {
    SubGhz* subghz = variable_item_get_context(item);
    furi_assert(subghz);

    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, ce_tx_power_text[index]);

    /* Mirror the same fields that Radio Settings touches so the value is
     * visible everywhere and survives app restart. */
    subghz->tx_power                  = index;
    subghz->last_settings->tx_power   = index;
    subghz_last_settings_save(subghz->last_settings);

    /* Patch the live preset buffer immediately so any subsequent TX in this
     * session uses the new power without needing a restart. */
    SubGhzRadioPreset preset = subghz_txrx_get_preset(subghz->txrx);
    if(preset.data && preset.data_size > 0) {
        subghz_txrx_set_tx_power(preset.data, preset.data_size, index);
    }
}

/* ── Scene callbacks ─────────────────────────────────────────────────────── */
void subghz_scene_car_emulate_settings_on_enter(void* context) {
    SubGhz* subghz = context;
    furi_assert(subghz);

    VariableItemList* list = subghz->variable_item_list;
    variable_item_list_reset(list);

    /* ── Row 1: Custom Emulate toggle ── */
    VariableItem* item = variable_item_list_add(
        list,
        "Custom Emulate",
        2,
        subghz_scene_car_emulate_settings_toggle_changed,
        subghz);

    uint8_t toggle_idx = subghz->last_settings->custom_car_emulate ? 1 : 0;
    variable_item_set_current_value_index(item, toggle_idx);
    variable_item_set_current_value_text(item, toggle_text[toggle_idx]);

    /* ── Row 2: TX Power ── */
    item = variable_item_list_add(
        list,
        "TX Power",
        CE_TX_POWER_COUNT,
        subghz_scene_car_emulate_settings_power_changed,
        subghz);

    /* Clamp stored value to valid range in case settings file is corrupt */
    uint8_t power_idx = subghz->tx_power;
    if(power_idx >= CE_TX_POWER_COUNT) power_idx = 0;

    variable_item_set_current_value_index(item, power_idx);
    variable_item_set_current_value_text(item, ce_tx_power_text[power_idx]);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdVariableItemList);
}

bool subghz_scene_car_emulate_settings_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subghz_scene_car_emulate_settings_on_exit(void* context) {
    SubGhz* subghz = context;
    variable_item_list_reset(subghz->variable_item_list);
}
