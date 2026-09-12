// App-local copy of the firmware VariableItemList; only its layout is customized.
/**
 * @file weather_variable_item_list.h
 * GUI: WeatherVariableItemList view module API
 */

#pragma once

#include <gui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct WeatherVariableItemList WeatherVariableItemList;
typedef struct WeatherVariableItem WeatherVariableItem;
typedef void (*WeatherVariableItemChangeCallback)(WeatherVariableItem* item);
typedef void (*WeatherVariableItemListEnterCallback)(void* context, uint32_t index);

/** Allocate and initialize WeatherVariableItemList
 *
 * @return     WeatherVariableItemList*
 */
WeatherVariableItemList* weather_variable_item_list_alloc(void);

/** Deinitialize and free WeatherVariableItemList
 *
 * @param      weather_variable_item_list  WeatherVariableItemList instance
 */
void weather_variable_item_list_free(WeatherVariableItemList* weather_variable_item_list);

/** Clear all elements from list
 *
 * @param      weather_variable_item_list  WeatherVariableItemList instance
 */
void weather_variable_item_list_reset(WeatherVariableItemList* weather_variable_item_list);

/** Get WeatherVariableItemList View instance
 *
 * @param      weather_variable_item_list  WeatherVariableItemList instance
 *
 * @return     View instance
 */
View* weather_variable_item_list_get_view(WeatherVariableItemList* weather_variable_item_list);

/** Add item to WeatherVariableItemList
 *
 * @param      weather_variable_item_list  WeatherVariableItemList instance
 * @param      label               item name
 * @param      values_count        item values count
 * @param      change_callback     called on value change in gui
 * @param      context             item context
 *
 * @return     WeatherVariableItem* item instance
 */
WeatherVariableItem* weather_variable_item_list_add(
    WeatherVariableItemList* weather_variable_item_list,
    const char* label,
    uint8_t values_count,
    WeatherVariableItemChangeCallback change_callback,
    void* context);

/** Get item in WeatherVariableItemList
 *
 * @param      weather_variable_item_list  WeatherVariableItemList instance
 * @param      position            index of the item to get
 *
 * @return     WeatherVariableItem* item instance
 */
WeatherVariableItem* weather_variable_item_list_get(WeatherVariableItemList* weather_variable_item_list, uint8_t position);

/** Set enter callback
 *
 * @param      weather_variable_item_list  WeatherVariableItemList instance
 * @param      callback            WeatherVariableItemListEnterCallback instance
 * @param      context             pointer to context
 */
void weather_variable_item_list_set_enter_callback(
    WeatherVariableItemList* weather_variable_item_list,
    WeatherVariableItemListEnterCallback callback,
    void* context);

void weather_variable_item_list_set_selected_item(WeatherVariableItemList* weather_variable_item_list, uint8_t index);

uint8_t weather_variable_item_list_get_selected_item_index(WeatherVariableItemList* weather_variable_item_list);

/** Set item current selected index
 *
 * @param      item                 WeatherVariableItem* instance
 * @param      current_value_index  The current value index
 */
void weather_variable_item_set_current_value_index(WeatherVariableItem* item, uint8_t current_value_index);

/** Set number of values for item
 *
 * @param      item                 WeatherVariableItem* instance
 * @param      values_count         The new values count
 */
void weather_variable_item_set_values_count(WeatherVariableItem* item, uint8_t values_count);

/** Set new label for item
 *
 * @param      item                 WeatherVariableItem* instance
 * @param      label                The new label text
 */
void weather_variable_item_set_item_label(WeatherVariableItem* item, const char* label);

/** Set item current selected text
 *
 * @param      item                WeatherVariableItem* instance
 * @param      current_value_text  The current value text
 */
void weather_variable_item_set_current_value_text(WeatherVariableItem* item, const char* current_value_text);

/** Set item locked state and text
 *
 * @param      item                WeatherVariableItem* instance
 * @param      locked              Is item locked boolean
 * @param      locked_message      The locked message text
 */
void weather_variable_item_set_locked(WeatherVariableItem* item, bool locked, const char* locked_message);

/** Get item current selected index
 *
 * @param      item  WeatherVariableItem* instance
 *
 * @return     uint8_t current selected index
 */
uint8_t weather_variable_item_get_current_value_index(WeatherVariableItem* item);

/** Get item context
 *
 * @param      item  WeatherVariableItem* instance
 *
 * @return     void* item context
 */
void* weather_variable_item_get_context(WeatherVariableItem* item);

#ifdef __cplusplus
}
#endif
