#include <subghz/subghz_i.h>
#include <subghz/helpers/subghz_txrx_create_protocol_key.h>

#include <machine/endian.h>

#define TAG "SubGhzSetKey"

static uint64_t subghz_set_key_original;
static bool subghz_set_key_pending;

#ifdef SUBGHZ_MANUAL_PARAMETERS
bool subghz_scene_set_type_generate_protocol_from_infos(SubGhz* subghz);
#endif

void subghz_scene_set_key_byte_input_callback(void* context) {
    SubGhz* subghz = context;

    view_dispatcher_send_custom_event(subghz->view_dispatcher, SubGhzCustomEventByteInputDone);
}

void subghz_scene_set_key_on_enter(void* context) {
    SubGhz* subghz = context;

    uint8_t* byte_ptr = NULL;
    uint8_t byte_count = 0;

    if(subghz->gen_info->type == GenData) {
        subghz_set_key_original = subghz->gen_info->data.key;
        subghz_set_key_pending = true;
        byte_ptr = (uint8_t*)&subghz->gen_info->data.key;
        byte_count = sizeof(subghz->gen_info->data.key);
    } else {
        subghz_scene_show_unsupported(subghz);
        return;
    }

    furi_assert(byte_ptr);
    furi_assert(byte_count > 0);

    *((uint64_t*)byte_ptr) = __bswap64(*((uint64_t*)byte_ptr)); // Convert

    // Setup view
    ByteInput* byte_input = subghz->byte_input;
    byte_input_set_header_text(byte_input, "Enter KEY in hex");
    byte_input_set_result_callback(
        byte_input, subghz_scene_set_key_byte_input_callback, NULL, subghz, byte_ptr, byte_count);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdByteInput);
}

bool subghz_scene_set_key_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;
    bool generated_protocol = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SubGhzCustomEventByteInputDone) {
            if(subghz->gen_info->type == GenData) {
                subghz->gen_info->data.key = __bswap64(subghz->gen_info->data.key);
                subghz_set_key_pending = false;

#ifdef SUBGHZ_MANUAL_PARAMETERS
                (void)subghz_scene_set_type_generate_protocol_from_infos(subghz);
                return true;
#else
                if(subghz->gen_info->data.te) {
                    generated_protocol = subghz_txrx_gen_data_protocol_and_te(
                        subghz->txrx,
                        subghz->gen_info->mod,
                        subghz->gen_info->freq,
                        subghz->gen_info->data.name,
                        subghz->gen_info->data.key,
                        subghz->gen_info->data.bits,
                        subghz->gen_info->data.te);
                } else {
                    generated_protocol = subghz_txrx_gen_data_protocol(
                        subghz->txrx,
                        subghz->gen_info->mod,
                        subghz->gen_info->freq,
                        subghz->gen_info->data.name,
                        subghz->gen_info->data.key,
                        subghz->gen_info->data.bits);
                }
#endif
            } else {
                subghz_scene_show_unsupported(subghz);
                return true;
            }

            consumed = true;

            if(!generated_protocol) {
                furi_string_set(
                    subghz->error_str, "Function requires\nan SD card with\nfresh databases.");
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneShowError);
            } else {
                subghz_file_name_clear(subghz);

                scene_manager_set_scene_state(
                    subghz->scene_manager, SubGhzSceneSaveName, SubGhzCustomEventManagerSet);
                scene_manager_next_scene(subghz->scene_manager, SubGhzSceneSaveName);
            }
        }
    }
    return consumed;
}

void subghz_scene_set_key_on_exit(void* context) {
    SubGhz* subghz = context;

    if(subghz_set_key_pending && subghz->gen_info->type == GenData) {
        subghz->gen_info->data.key = subghz_set_key_original;
    }
    subghz_set_key_pending = false;

    // Clear view
    byte_input_set_result_callback(subghz->byte_input, NULL, NULL, NULL, NULL, 0);
    byte_input_set_header_text(subghz->byte_input, "");
}
