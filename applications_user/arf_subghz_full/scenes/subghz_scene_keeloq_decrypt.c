#include "../subghz_i.h"
#include "../helpers/subghz_txrx_i.h"
#include <lib/subghz/protocols/keeloq.h>
#include <lib/subghz/protocols/keeloq_common.h>
#include <lib/subghz/blocks/math.h>
#include <lib/subghz/environment.h>
#include <lib/subghz/subghz_keystore.h>
#include <furi.h>
#include <bt/bt_service/bt.h>

#define KL_DECRYPT_EVENT_DONE      (0xD2)
#define KL_DECRYPT_EVENT_CANDIDATE (0xD3)
#define KL_DECRYPT_EVENT_REJECTED  (0xD4)
#define KL_TOTAL_KEYS              0x100000000ULL

#define ARF_OFFLOAD_APP_ID "arf_offload"

typedef struct {
    SubGhz* subghz;
    FuriPubSubSubscription* app_bridge_sub;
    volatile bool cancel;
    uint32_t start_tick;
    bool success;
    FuriString* result;
    FuriString* reject_reason;

    uint32_t fix;
    uint32_t hop;
    uint32_t serial;
    uint8_t btn;
    uint16_t disc;

    uint32_t hop2;

    uint32_t candidate_count;
    uint64_t recovered_mfkey;
    uint16_t recovered_type;
    uint32_t recovered_cnt;

    bool ble_offload;
} KlDecryptCtx;

static void kl_app_bridge_rx(const void* message, void* context) {
    KlDecryptCtx* ctx = context;
    const BtAppBridgeEvent* event = message;
    if(!ctx || !event || ctx->cancel) return;
    if(strcmp(event->app_id, ARF_OFFLOAD_APP_ID) != 0) return;

    const uint8_t* data = event->payload;
    const uint16_t size = event->payload_len;

    if(strcmp(event->command, "keeloq_progress") == 0 && size >= 8) {
        uint32_t keys_tested, keys_per_sec;
        memcpy(&keys_tested, data, 4);
        memcpy(&keys_per_sec, data + 4, 4);

        uint32_t elapsed_sec = (furi_get_tick() - ctx->start_tick) / 1000;
        uint32_t remaining = (keys_tested > 0) ? (0xFFFFFFFFU - keys_tested) : 0xFFFFFFFFU;
        uint32_t eta_sec = (keys_per_sec > 0) ? (remaining / keys_per_sec) : 0;
        uint8_t pct = (uint8_t)((uint64_t)keys_tested * 100 / 0xFFFFFFFFULL);

        subghz_view_keeloq_decrypt_update_stats(
            ctx->subghz->subghz_keeloq_decrypt, pct, keys_tested, keys_per_sec, elapsed_sec, eta_sec);

    } else if(strcmp(event->command, "keeloq_result") == 0 && size >= 25) {
        uint8_t found = data[0];

        if(found == 1) {
            uint64_t mfkey = 0;
            uint32_t cnt = 0;
            memcpy(&mfkey, data + 1, 8);
            memcpy(&cnt, data + 17, 4);
            uint16_t learn_type = (size >= 26) ? data[25] : 6;

            ctx->candidate_count++;
            ctx->recovered_mfkey = mfkey;
            ctx->recovered_type = learn_type;
            ctx->recovered_cnt = cnt;

            subghz_view_keeloq_decrypt_update_candidates(
                ctx->subghz->subghz_keeloq_decrypt, ctx->candidate_count);

            view_dispatcher_send_custom_event(
                ctx->subghz->view_dispatcher, KL_DECRYPT_EVENT_CANDIDATE);

        } else if(found == 2) {
            ctx->success = (ctx->candidate_count > 0);
            view_dispatcher_send_custom_event(
                ctx->subghz->view_dispatcher, KL_DECRYPT_EVENT_DONE);
        }
    } else if(strcmp(event->command, "reject") == 0) {
        if(size > 0) {
            furi_string_set_strn(ctx->reject_reason, (const char*)data, size);
        } else {
            furi_string_set_str(ctx->reject_reason, "Bridge rejected request.");
        }
        view_dispatcher_send_custom_event(
            ctx->subghz->view_dispatcher, KL_DECRYPT_EVENT_REJECTED);
    }
}

static void kl_ble_cleanup(KlDecryptCtx* ctx) {
    if(ctx->app_bridge_sub) {
        Bt* bt = furi_record_open(RECORD_BT);
        FuriPubSub* pubsub = bt_app_bridge_get_pubsub(bt);
        if(pubsub) {
            furi_pubsub_unsubscribe(pubsub, ctx->app_bridge_sub);
        }
        furi_record_close(RECORD_BT);
        ctx->app_bridge_sub = NULL;
    }
    ctx->ble_offload = false;
}

static bool kl_ble_start_offload(KlDecryptCtx* ctx) {
    Bt* bt = furi_record_open(RECORD_BT);
    FuriPubSub* pubsub = bt_app_bridge_get_pubsub(bt);
    if(pubsub) {
        ctx->app_bridge_sub = furi_pubsub_subscribe(pubsub, kl_app_bridge_rx, ctx);
    }

    uint8_t req[17];
    req[0] = ctx->subghz->keeloq_bf2.learn_type;
    memcpy(req + 1, &ctx->fix, 4);
    memcpy(req + 5, &ctx->hop, 4);
    memcpy(req + 9, &ctx->hop2, 4);
    memcpy(req + 13, &ctx->serial, 4);
    const bool sent =
        bt_app_bridge_send(bt, ARF_OFFLOAD_APP_ID, "keeloq_bf_request", req, sizeof(req));
    furi_record_close(RECORD_BT);

    if(!sent) {
        kl_ble_cleanup(ctx);
        return false;
    }

    ctx->ble_offload = true;
    subghz_view_keeloq_decrypt_set_status(
        ctx->subghz->subghz_keeloq_decrypt, "[BT] Offloading...");
    return true;
}

static void kl_decrypt_view_callback(SubGhzCustomEvent event, void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

void subghz_scene_keeloq_decrypt_on_enter(void* context) {
    SubGhz* subghz = context;

    KlDecryptCtx* ctx = malloc(sizeof(KlDecryptCtx));
    memset(ctx, 0, sizeof(KlDecryptCtx));
    ctx->subghz = subghz;
    ctx->result = furi_string_alloc_set("No result");
    ctx->reject_reason = furi_string_alloc_set("Bridge rejected request.");

    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    flipper_format_rewind(fff);

    uint8_t key_data[8] = {0};
    if(flipper_format_read_hex(fff, "Key", key_data, 8)) {
        uint64_t raw = 0;
        for(uint8_t i = 0; i < 8; i++) {
            raw = (raw << 8) | key_data[i];
        }
        uint64_t reversed = subghz_protocol_blocks_reverse_key(raw, 64);
        ctx->fix = (uint32_t)(reversed >> 32);
        ctx->hop = (uint32_t)(reversed & 0xFFFFFFFF);
    }

    ctx->serial = ctx->fix & 0x0FFFFFFF;
    ctx->btn = ctx->fix >> 28;
    ctx->disc = ctx->serial & 0x3FF;
    ctx->hop2 = subghz->keeloq_bf2.sig2_loaded ? subghz->keeloq_bf2.hop2 : 0;

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzSceneKeeloqDecrypt, (uint32_t)(uintptr_t)ctx);

    subghz_view_keeloq_decrypt_reset(subghz->subghz_keeloq_decrypt);
    subghz_view_keeloq_decrypt_set_callback(
        subghz->subghz_keeloq_decrypt, kl_decrypt_view_callback, subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdKeeloqDecrypt);

    ctx->start_tick = furi_get_tick();

    if(!kl_ble_start_offload(ctx)) {
        char msg[128];
        snprintf(
            msg,
            sizeof(msg),
            "No BLE connection!\n"
            "Enable App Bridge\n"
            "and try again.\n\n"
            "Fix:0x%08lX\nHop:0x%08lX",
            ctx->fix,
            ctx->hop);
        subghz_view_keeloq_decrypt_set_result(
            subghz->subghz_keeloq_decrypt, false, msg);
    }
}

bool subghz_scene_keeloq_decrypt_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    KlDecryptCtx* ctx = (KlDecryptCtx*)(uintptr_t)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzSceneKeeloqDecrypt);
    if(!ctx) return false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == KL_DECRYPT_EVENT_CANDIDATE) {
            if(!subghz->keeloq_keys_manager) {
                subghz->keeloq_keys_manager = subghz_keeloq_keys_alloc();
            }
            uint16_t learning_type = ctx->recovered_type ?
                                         ctx->recovered_type :
                                         KEELOQ_LEARNING_SIMPLE;
            char key_name[24];
            snprintf(key_name, sizeof(key_name), "BF_%07lX", ctx->serial);
            subghz_keeloq_keys_add(
                subghz->keeloq_keys_manager,
                ctx->recovered_mfkey,
                learning_type,
                key_name);
            subghz_keeloq_keys_save(subghz->keeloq_keys_manager);

            SubGhzKeystore* env_ks = subghz_environment_get_keystore(
                subghz->txrx->environment);
            SubGhzKeyArray_t* env_arr = subghz_keystore_get_data(env_ks);
            SubGhzKey* entry = SubGhzKeyArray_push_raw(*env_arr);
            entry->name = furi_string_alloc_set(key_name);
            entry->key = ctx->recovered_mfkey;
            entry->type = learning_type;
            return true;

        } else if(event.event == KL_DECRYPT_EVENT_REJECTED) {
            kl_ble_cleanup(ctx);
            subghz_view_keeloq_decrypt_set_result(
                subghz->subghz_keeloq_decrypt,
                false,
                furi_string_get_cstr(ctx->reject_reason));
            return true;

        } else if(event.event == KL_DECRYPT_EVENT_DONE) {
            kl_ble_cleanup(ctx);
            subghz->keeloq_bf2.sig1_loaded = false;
            subghz->keeloq_bf2.sig2_loaded = false;

            if(ctx->success) {
                furi_string_printf(
                    ctx->result,
                    "Found %lu candidate(s)\n"
                    "Last: %08lX%08lX\n"
                    "Type:%u Cnt:%04lX\n"
                    "Saved to user keys",
                    ctx->candidate_count,
                    (uint32_t)(ctx->recovered_mfkey >> 32),
                    (uint32_t)(ctx->recovered_mfkey & 0xFFFFFFFF),
                    ctx->recovered_type,
                    ctx->recovered_cnt);

                FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
                flipper_format_rewind(fff);

                char mf_str[20];
                snprintf(mf_str, sizeof(mf_str), "BF_%07lX", ctx->serial);
                flipper_format_insert_or_update_string_cstr(fff, "Manufacture", mf_str);

                uint32_t cnt_val = ctx->recovered_cnt;
                flipper_format_rewind(fff);
                flipper_format_insert_or_update_uint32(fff, "Cnt", &cnt_val, 1);

                if(ctx->hop2 != 0) {
                    flipper_format_rewind(fff);
                    flipper_format_insert_or_update_uint32(fff, "Hop2", &ctx->hop2, 1);
                }

                flipper_format_rewind(fff);
                subghz_protocol_decoder_base_deserialize(
                    subghz_txrx_get_decoder(subghz->txrx), fff);

                const char* save_path = NULL;
                if(subghz_path_is_file(subghz->file_path)) {
                    save_path = furi_string_get_cstr(subghz->file_path);
                } else if(subghz_path_is_file(subghz->keeloq_bf2.sig1_path)) {
                    save_path = furi_string_get_cstr(subghz->keeloq_bf2.sig1_path);
                }
                if(save_path) {
                    subghz_save_protocol_to_file(
                        subghz,
                        subghz_txrx_get_fff_data(subghz->txrx),
                        save_path);
                    furi_string_set_str(subghz->file_path, save_path);
                }

                subghz_view_keeloq_decrypt_set_result(
                    subghz->subghz_keeloq_decrypt, true, furi_string_get_cstr(ctx->result));
            } else if(!ctx->cancel) {
                subghz_view_keeloq_decrypt_set_result(
                    subghz->subghz_keeloq_decrypt, false,
                    "Key NOT found.\nNo matching key in\n2^32 search space.");
            } else {
                subghz_view_keeloq_decrypt_set_result(
                    subghz->subghz_keeloq_decrypt, false, "Cancelled.");
            }
            return true;

        } else if(event.event == SubGhzCustomEventViewTransmitterBack) {
            if(ctx->ble_offload) {
                Bt* bt = furi_record_open(RECORD_BT);
                bt_app_bridge_send_text(bt, ARF_OFFLOAD_APP_ID, "keeloq_bf_cancel", "");
                furi_record_close(RECORD_BT);
                kl_ble_cleanup(ctx);
            }
            ctx->cancel = true;
            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        }
    }
    return false;
}

void subghz_scene_keeloq_decrypt_on_exit(void* context) {
    SubGhz* subghz = context;
    KlDecryptCtx* ctx = (KlDecryptCtx*)(uintptr_t)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzSceneKeeloqDecrypt);

    if(ctx) {
        kl_ble_cleanup(ctx);
        ctx->cancel = true;
        furi_string_free(ctx->reject_reason);
        furi_string_free(ctx->result);
        free(ctx);
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzSceneKeeloqDecrypt, 0);
    }
}
