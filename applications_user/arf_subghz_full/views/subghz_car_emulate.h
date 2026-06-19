#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SubGhzCarEmulateView SubGhzCarEmulateView;

typedef void (*SubGhzCarEmulateViewCallback)(uint32_t event, void* context);

SubGhzCarEmulateView* subghz_car_emulate_view_alloc(void);
void subghz_car_emulate_view_free(SubGhzCarEmulateView* instance);
View* subghz_car_emulate_view_get_view(SubGhzCarEmulateView* instance);

void subghz_car_emulate_view_set_callback(
    SubGhzCarEmulateView* instance,
    SubGhzCarEmulateViewCallback callback,
    void* context);

/** Update the fields shown on the view.
 *  All strings are copied internally so the caller can free them after the call.
 */
void subghz_car_emulate_view_set_labels(
    SubGhzCarEmulateView* instance,
    const char* ok,
    const char* up,
    const char* down,
    const char* left,
    const char* right);

void subghz_car_emulate_view_set_data(
    SubGhzCarEmulateView* instance,
    const char* protocol_name,
    uint32_t serial,
    uint32_t counter,
    uint32_t original_counter,
    uint32_t freq,
    const char* preset,
    bool is_transmitting);

#ifdef __cplusplus
}
#endif
