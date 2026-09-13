#include "../subghz_i.h"
#include <string.h>

// Internal step events used only within this scene
#define KL_EDIT_EV_KEY_DONE  200u
#define KL_EDIT_EV_NAME_DONE 201u
#define KL_EDIT_EV_TYPE_BASE 210u // supported learning types mapped to events 211..

static void kl_edit_byte_input_cb(void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, KL_EDIT_EV_KEY_DONE);
}

static void kl_edit_text_input_cb(void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, KL_EDIT_EV_NAME_DONE);
}

static void kl_edit_type_submenu_cb(void* context, uint32_t index) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(
        subghz->view_dispatcher, (uint32_t)(KL_EDIT_EV_TYPE_BASE + 1 + index));
}

typedef struct {
    uint16_t type;
    const char* name;
} KlTypeOption;

static const KlTypeOption kl_type_options[] = {
    {1u, "1-Simple"},
    {2u, "2-Normal"},
    {3u, "3-Secure"},
    {4u, "4-Magic XOR"},
    {5u, "5-FAAC SLH"},
    {6u, "6-Magic Ser1"},
    {7u, "7-Magic Ser2"},
    {8u, "8-Magic Ser3"},
    {10u, "10-KingGates"},
    {11u, "11-Jarolift"},
    {12u, "12-Erreka"},
    {13u, "13-Pujol"},
    {14u, "14-AERF"},
    {15u, "15-JCM"},
    {16u, "16-JCM Gen2"},
    {17u, "17-Stagnoli"},
    {18u, "18-Telcoma hi"},
    {19u, "19-Telcoma lo"},
};

static void kl_edit_show_step(SubGhz* subghz) {
    switch(subghz->keeloq_edit.edit_step) {
    case 0:
        byte_input_set_header_text(subghz->byte_input, "Enter 64-bit key (hex)");
        byte_input_set_result_callback(
            subghz->byte_input,
            kl_edit_byte_input_cb,
            NULL,
            subghz,
            subghz->keeloq_edit.key_bytes,
            sizeof(subghz->keeloq_edit.key_bytes));
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdByteInput);
        break;
    case 1:
        text_input_set_header_text(subghz->text_input, "Manufacturer name");
        text_input_set_result_callback(
            subghz->text_input,
            kl_edit_text_input_cb,
            subghz,
            subghz->keeloq_edit.name,
            sizeof(subghz->keeloq_edit.name),
            false);
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdTextInput);
        break;
    case 2:
        submenu_reset(subghz->submenu);
        submenu_set_header(subghz->submenu, "Learning type");
        for(size_t i = 0; i < COUNT_OF(kl_type_options); i++) {
            submenu_add_item(
                subghz->submenu,
                kl_type_options[i].name,
                (uint32_t)i,
                kl_edit_type_submenu_cb,
                subghz);
        }
        for(size_t i = 0; i < COUNT_OF(kl_type_options); i++) {
            if(subghz->keeloq_edit.type == kl_type_options[i].type) {
                submenu_set_selected_item(subghz->submenu, (uint32_t)i);
                break;
            }
        }
        view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdMenu);
        break;
    default:
        break;
    }
}

void subghz_scene_keeloq_key_edit_on_enter(void* context) {
    SubGhz* subghz = context;
    // edit_step, key_bytes, name, type, is_new, edit_index are pre-set by the caller scene
    kl_edit_show_step(subghz);
}

bool subghz_scene_keeloq_key_edit_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;

    if(event.type != SceneManagerEventTypeCustom) {
        return false;
    }

    if(event.event == KL_EDIT_EV_KEY_DONE) {
        subghz->keeloq_edit.edit_step = 1;
        kl_edit_show_step(subghz);
        return true;
    }

    if(event.event == KL_EDIT_EV_NAME_DONE) {
        subghz->keeloq_edit.edit_step = 2;
        kl_edit_show_step(subghz);
        return true;
    }

    if(event.event > KL_EDIT_EV_TYPE_BASE &&
       event.event <= KL_EDIT_EV_TYPE_BASE + COUNT_OF(kl_type_options)) {
        const size_t option_index = (size_t)(event.event - KL_EDIT_EV_TYPE_BASE - 1u);
        subghz->keeloq_edit.type = kl_type_options[option_index].type;

        // Reconstruct 64-bit key from byte array (big-endian, same as ByteInput display order)
        uint64_t kval = 0;
        for(int b = 0; b < 8; b++) {
            kval = (kval << 8) | (uint64_t)subghz->keeloq_edit.key_bytes[b];
        }

        if(subghz->keeloq_edit.is_new) {
            subghz_keeloq_keys_add(
                subghz->keeloq_keys_manager,
                kval,
                subghz->keeloq_edit.type,
                subghz->keeloq_edit.name);
        } else {
            subghz_keeloq_keys_set(
                subghz->keeloq_keys_manager,
                subghz->keeloq_edit.edit_index,
                kval,
                subghz->keeloq_edit.type,
                subghz->keeloq_edit.name);
        }

        if(!subghz_keeloq_keys_save(subghz->keeloq_keys_manager)) {
            furi_string_set(subghz->error_str, "Cannot save\nkeystore file.");
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowErrorSub);
        } else {
            scene_manager_previous_scene(subghz->scene_manager);
        }
        return true;
    }

    return false;
}

void subghz_scene_keeloq_key_edit_on_exit(void* context) {
    SubGhz* subghz = context;
    byte_input_set_result_callback(subghz->byte_input, NULL, NULL, NULL, NULL, 0);
    byte_input_set_header_text(subghz->byte_input, "");
    text_input_reset(subghz->text_input);
    submenu_reset(subghz->submenu);
}
