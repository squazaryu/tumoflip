#include "../subghz_i.h"
#include <lib/subghz/protocols/psa.h>
#include <furi.h>
#include <storage/storage.h>
#include <bt/bt_service/bt.h>

#define PSA_DECRYPT_EVENT_DONE  (0xD1)
#define PSA_DECRYPT_EVENT_LOCAL (0xD3)
#define PSA_TOTAL_KEYS          0x2000000UL // 32M
#define PSA_BLE_PHASE_KEYS      0x1000000UL // 16M per BF phase
#define PSA_BF_PARAMS_PATH      EXT_PATH("subghz/psa_bf_params.txt")

#define ARF_OFFLOAD_APP_ID "arf_offload"

typedef struct {
    SubGhz* subghz;
    FuriThread* thread;
    FuriPubSubSubscription* app_bridge_sub;
    volatile bool cancel;
    uint32_t start_tick;
    bool success;
    FuriString* result;
    uint8_t key1[8];
    uint8_t key2[8];
    uint32_t w0;
    uint32_t w1;
    bool needs_bf;
    bool ble_offload;
} PsaDecryptCtx;

static void psa_decrypt_start_local(PsaDecryptCtx* ctx);

static bool psa_decrypt_progress_cb(uint8_t progress, uint32_t keys_tested, void* context) {
    UNUSED(progress);
    PsaDecryptCtx* ctx = context;
    if(ctx->cancel) return false;

    uint32_t now = furi_get_tick();
    uint32_t elapsed_ms = now - ctx->start_tick;
    uint32_t elapsed_sec = elapsed_ms / 1000;
    uint32_t keys_per_sec =
        (elapsed_ms > 0) ? (uint32_t)((uint64_t)keys_tested * 1000 / elapsed_ms) : 0;
    uint32_t remaining = (keys_tested < PSA_TOTAL_KEYS) ? (PSA_TOTAL_KEYS - keys_tested) : 0;
    uint32_t eta_sec = (keys_per_sec > 0) ? (remaining / keys_per_sec) : 0;
    uint8_t pct = (uint8_t)((uint64_t)keys_tested * 100 / PSA_TOTAL_KEYS);

    subghz_view_psa_decrypt_update_stats(
        ctx->subghz->subghz_psa_decrypt, pct, keys_tested, keys_per_sec, elapsed_sec, eta_sec);

    furi_delay_ms(1);
    return true;
}

static FlipperFormat* psa_decrypt_build_fff(PsaDecryptCtx* ctx) {
    FlipperFormat* fff = flipper_format_string_alloc();
    flipper_format_write_header_cstr(fff, "Flipper SubGhz Key File", 1);
    flipper_format_write_string_cstr(fff, "Protocol", "PSA GROUP");

    char key1_str[32];
    snprintf(
        key1_str, sizeof(key1_str),
        "%02X %02X %02X %02X %02X %02X %02X %02X",
        ctx->key1[0], ctx->key1[1], ctx->key1[2], ctx->key1[3],
        ctx->key1[4], ctx->key1[5], ctx->key1[6], ctx->key1[7]);
    flipper_format_write_string_cstr(fff, "Key", key1_str);

    char key2_str[32];
    snprintf(
        key2_str, sizeof(key2_str),
        "%02X %02X %02X %02X %02X %02X %02X %02X",
        ctx->key2[0], ctx->key2[1], ctx->key2[2], ctx->key2[3],
        ctx->key2[4], ctx->key2[5], ctx->key2[6], ctx->key2[7]);
    flipper_format_write_string_cstr(fff, "Key_2", key2_str);

    flipper_format_rewind(fff);
    return fff;
}

static void psa_decrypt_copy_fields(PsaDecryptCtx* ctx, FlipperFormat* src_fff) {
    FlipperFormat* real_fff = subghz_txrx_get_fff_data(ctx->subghz->txrx);
    FuriString* tmp = furi_string_alloc();
    const char* fields[] = {"Serial", "Cnt", "Btn", "Type", "CRC", "Seed"};
    for(int i = 0; i < 6; i++) {
        flipper_format_rewind(src_fff);
        if(flipper_format_read_string(src_fff, fields[i], tmp)) {
            flipper_format_rewind(real_fff);
            flipper_format_insert_or_update_string_cstr(
                real_fff, fields[i], furi_string_get_cstr(tmp));
        }
    }
    furi_string_free(tmp);
}

static void psa_decrypt_save_bf_params(PsaDecryptCtx* ctx) {
    FlipperFormat* fff = psa_decrypt_build_fff(ctx);
    ctx->needs_bf = subghz_protocol_psa_get_bf_params(fff, &ctx->w0, &ctx->w1);
    flipper_format_free(fff);

    if(!ctx->needs_bf) return;

    Storage* storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);
    if(storage_file_open(file, PSA_BF_PARAMS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "w0=%08lX\nw1=%08lX\n", ctx->w0, ctx->w1);
        storage_file_write(file, buf, len);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_record_close(RECORD_STORAGE);
}

/* --- BLE App Bridge offload callbacks --- */

static void psa_app_bridge_rx(const void* message, void* context) {
    PsaDecryptCtx* ctx = context;
    const BtAppBridgeEvent* event = message;
    if(!ctx || !event || ctx->cancel) return;
    if(strcmp(event->app_id, ARF_OFFLOAD_APP_ID) != 0) return;

    const uint8_t* data = event->payload;
    const uint16_t size = event->payload_len;

    if(strcmp(event->command, "psa_progress") == 0 && size >= 8) {
        uint32_t keys_tested, keys_per_sec;
        memcpy(&keys_tested, data, 4);
        memcpy(&keys_per_sec, data + 4, 4);

        uint32_t elapsed_sec = (furi_get_tick() - ctx->start_tick) / 1000;
        uint32_t remaining =
            (keys_tested < PSA_BLE_PHASE_KEYS) ? (PSA_BLE_PHASE_KEYS - keys_tested) : 0;
        uint32_t eta_sec = (keys_per_sec > 0) ? (remaining / keys_per_sec) : 0;
        uint8_t pct = (uint8_t)((uint64_t)keys_tested * 100 / PSA_BLE_PHASE_KEYS);

        subghz_view_psa_decrypt_update_stats(
            ctx->subghz->subghz_psa_decrypt, pct, keys_tested, keys_per_sec, elapsed_sec, eta_sec);

    } else if(strcmp(event->command, "psa_result") == 0 && size >= 13) {
        uint8_t success = data[0];
        uint32_t counter, dec_v0, dec_v1;
        memcpy(&counter, data + 1, 4);
        memcpy(&dec_v0, data + 5, 4);
        memcpy(&dec_v1, data + 9, 4);

        if(success) {
            int bf_type = (counter >= 0xF3000000UL) ? 2 : 1;
            FlipperFormat* fff = psa_decrypt_build_fff(ctx);
            ctx->success = subghz_protocol_psa_apply_bf_result(
                fff, ctx->result, counter, dec_v0, dec_v1, bf_type);
            if(ctx->success) {
                psa_decrypt_copy_fields(ctx, fff);
            }
            flipper_format_free(fff);
        }

        view_dispatcher_send_custom_event(ctx->subghz->view_dispatcher, PSA_DECRYPT_EVENT_DONE);
    } else if(strcmp(event->command, "reject") == 0) {
        view_dispatcher_send_custom_event(ctx->subghz->view_dispatcher, PSA_DECRYPT_EVENT_LOCAL);
    }
}

static void psa_ble_cleanup(PsaDecryptCtx* ctx) {
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

static bool __attribute__((unused)) psa_ble_start_offload(PsaDecryptCtx* ctx) {
    psa_decrypt_save_bf_params(ctx);
    if(!ctx->needs_bf) {
        return false;
    }

    Bt* bt = furi_record_open(RECORD_BT);
    FuriPubSub* pubsub = bt_app_bridge_get_pubsub(bt);
    if(pubsub) {
        ctx->app_bridge_sub = furi_pubsub_subscribe(pubsub, psa_app_bridge_rx, ctx);
    }

    uint8_t req[8];
    memcpy(req, &ctx->w0, 4);
    memcpy(req + 4, &ctx->w1, 4);
    const bool sent = bt_app_bridge_send(bt, ARF_OFFLOAD_APP_ID, "psa_bf_request", req, sizeof(req));
    furi_record_close(RECORD_BT);

    if(!sent) {
        psa_ble_cleanup(ctx);
        return false;
    }

    ctx->ble_offload = true;
    subghz_view_psa_decrypt_set_status(
        ctx->subghz->subghz_psa_decrypt, "[BT] Offloading...");
    return true;
}

/* --- Local BF thread --- */

static int32_t psa_decrypt_thread(void* context) {
    PsaDecryptCtx* ctx = context;

    psa_decrypt_save_bf_params(ctx);

    FlipperFormat* fff = psa_decrypt_build_fff(ctx);
    ctx->success =
        subghz_protocol_psa_decrypt_file(fff, ctx->result, psa_decrypt_progress_cb, ctx);

    if(ctx->success) {
        psa_decrypt_copy_fields(ctx, fff);
    }

    flipper_format_free(fff);
    view_dispatcher_send_custom_event(ctx->subghz->view_dispatcher, PSA_DECRYPT_EVENT_DONE);
    return 0;
}

static void __attribute__((unused)) psa_decrypt_start_local(PsaDecryptCtx* ctx) {
    if(ctx->thread) return;
    subghz_view_psa_decrypt_set_status(
        ctx->subghz->subghz_psa_decrypt, "[BT] Local fallback...");
    ctx->thread = furi_thread_alloc_ex("PsaDecrypt", 4096, psa_decrypt_thread, ctx);
    furi_thread_start(ctx->thread);
}

static void psa_decrypt_view_callback(SubGhzCustomEvent event, void* context) {
    SubGhz* subghz = context;
    view_dispatcher_send_custom_event(subghz->view_dispatcher, event);
}

void subghz_scene_psa_decrypt_on_enter(void* context) {
    SubGhz* subghz = context;

    PsaDecryptCtx* ctx = malloc(sizeof(PsaDecryptCtx));
    memset(ctx, 0, sizeof(PsaDecryptCtx));
    ctx->subghz = subghz;
    ctx->result = furi_string_alloc_set("No result");

    FlipperFormat* fff = subghz_txrx_get_fff_data(subghz->txrx);
    flipper_format_rewind(fff);
    flipper_format_read_hex(fff, "Key", ctx->key1, 8);
    FuriString* k2_str = furi_string_alloc();
    flipper_format_rewind(fff);
    if(flipper_format_read_string(fff, "Key_2", k2_str)) {
        const char* s = furi_string_get_cstr(k2_str);
        int bi = 0;
        for(size_t i = 0; i < strlen(s) && bi < 8; i++) {
            char c = s[i];
            if(c == ' ') continue;
            uint8_t hi =
                (c >= '0' && c <= '9') ? c - '0' :
                (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                                         c - 'a' + 10;
            i++;
            while(i < strlen(s) && s[i] == ' ') i++;
            if(i < strlen(s)) {
                c = s[i];
                uint8_t lo =
                    (c >= '0' && c <= '9') ? c - '0' :
                    (c >= 'A' && c <= 'F') ? c - 'A' + 10 :
                                             c - 'a' + 10;
                ctx->key2[bi++] = (hi << 4) | lo;
            }
        }
    }
    furi_string_free(k2_str);

    scene_manager_set_scene_state(
        subghz->scene_manager, SubGhzScenePsaDecrypt, (uint32_t)(uintptr_t)ctx);

    subghz_view_psa_decrypt_reset(subghz->subghz_psa_decrypt);
    subghz_view_psa_decrypt_set_callback(
        subghz->subghz_psa_decrypt, psa_decrypt_view_callback, subghz);

    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdPsaDecrypt);

    ctx->start_tick = furi_get_tick();

    subghz_view_psa_decrypt_set_result(
        subghz->subghz_psa_decrypt,
        false,
        "PSA decrypt is not\n"
        "embedded in this app.\n\n"
        "Use ProtoPirate PSA\n"
        "tools from ARF Tools.");
}

bool subghz_scene_psa_decrypt_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    PsaDecryptCtx* ctx = (PsaDecryptCtx*)(uintptr_t)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzScenePsaDecrypt);
    if(!ctx) return false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == PSA_DECRYPT_EVENT_DONE) {
            // Clean up BLE or thread
            psa_ble_cleanup(ctx);
            if(ctx->thread) {
                furi_thread_join(ctx->thread);
                furi_thread_free(ctx->thread);
                ctx->thread = NULL;
            }

            if(ctx->success) {
                subghz_save_protocol_to_file(
                    subghz,
                    subghz_txrx_get_fff_data(subghz->txrx),
                    furi_string_get_cstr(subghz->file_path));
                subghz_view_psa_decrypt_set_result(
                    subghz->subghz_psa_decrypt, true, furi_string_get_cstr(ctx->result));
            } else if(!ctx->cancel) {
                if(ctx->needs_bf) {
                    char fail_msg[96];
                    snprintf(fail_msg, sizeof(fail_msg),
                        "Decrypt FAILED!\n"
                        "BF params saved to SD:\n"
                        "w0=%08lX\nw1=%08lX",
                        ctx->w0, ctx->w1);
                    subghz_view_psa_decrypt_set_result(
                        subghz->subghz_psa_decrypt, false, fail_msg);
                } else {
                    subghz_view_psa_decrypt_set_result(
                        subghz->subghz_psa_decrypt,
                        false,
                        "Decrypt FAILED!\nType not supported.");
                }
            } else {
                subghz_view_psa_decrypt_set_result(
                    subghz->subghz_psa_decrypt, false, "Cancelled.");
            }
            return true;

        } else if(event.event == SubGhzCustomEventViewTransmitterBack) {
            if(ctx->ble_offload) {
                Bt* bt = furi_record_open(RECORD_BT);
                bt_app_bridge_send_text(bt, ARF_OFFLOAD_APP_ID, "psa_bf_cancel", "");
                furi_record_close(RECORD_BT);
                psa_ble_cleanup(ctx);
            }
            if(ctx->thread) {
                ctx->cancel = true;
                furi_thread_join(ctx->thread);
                furi_thread_free(ctx->thread);
                ctx->thread = NULL;
            }
            furi_string_free(ctx->result);
            free(ctx);
            scene_manager_set_scene_state(
                subghz->scene_manager, SubGhzScenePsaDecrypt, 0);
            scene_manager_previous_scene(subghz->scene_manager);
            return true;
        }
    }
    return false;
}

void subghz_scene_psa_decrypt_on_exit(void* context) {
    SubGhz* subghz = context;
    PsaDecryptCtx* ctx = (PsaDecryptCtx*)(uintptr_t)scene_manager_get_scene_state(
        subghz->scene_manager, SubGhzScenePsaDecrypt);

    if(ctx) {
        psa_ble_cleanup(ctx);
        if(ctx->thread) {
            ctx->cancel = true;
            furi_thread_join(ctx->thread);
            furi_thread_free(ctx->thread);
            ctx->thread = NULL;
        }
        furi_string_free(ctx->result);
        free(ctx);
        scene_manager_set_scene_state(subghz->scene_manager, SubGhzScenePsaDecrypt, 0);
    }
}
