#include "core/tumonet_protocol.h"
#include "radio/tumonet_radio.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_rtc.h>
#include <gui/elements.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "TumoNetBench"

#define TUMONET_BENCH_VERSION    "0.1.2"
#define TUMONET_BENCH_DATA_DIR   EXT_PATH("apps_data/tumonet_bench")
#define TUMONET_BENCH_REPORT_DIR TUMONET_BENCH_DATA_DIR "/reports"
#define TUMONET_BENCH_MAX_RETRY  2U

typedef enum {
    TumoNetBenchScreenSetup,
    TumoNetBenchScreenPair,
    TumoNetBenchScreenRun,
    TumoNetBenchScreenAbout,
} TumoNetBenchScreen;

typedef enum {
    TumoNetBenchFaultClean,
    TumoNetBenchFaultDrop,
    TumoNetBenchFaultDuplicate,
    TumoNetBenchFaultReplay,
    TumoNetBenchFaultCorrupt,
    TumoNetBenchFaultWrongKey,
    TumoNetBenchFaultInterrupt,
    TumoNetBenchFaultCount,
} TumoNetBenchFault;

typedef struct {
    TumoNetBenchScreen screen;
    uint8_t selected_row;
    uint8_t direction;
    uint8_t fault;
    uint8_t payload_index;
    bool paired;
    bool running;
    bool finished;
    bool passed;
    bool report_written;
    uint8_t fragments;
    uint8_t retries;
    uint16_t tx_packets;
    uint16_t rx_packets;
    uint32_t frequency;
    char status[24];
} TumoNetBenchModel;

typedef struct {
    Gui* gui;
    Storage* storage;
    NotificationApp* notifications;
    ViewDispatcher* view_dispatcher;
    View* view;
    FuriThread* worker;
    volatile bool cancel_requested;

    uint8_t direction;
    uint8_t fault;
    uint8_t payload_index;
    uint8_t master_key[TUMONET_MASTER_KEY_SIZE];
    uint32_t session_id;
    bool paired;

    bool last_passed;
    uint8_t last_fragments;
    uint8_t last_retries;
    uint16_t last_tx_packets;
    uint16_t last_rx_packets;
    uint32_t last_frequency;
    char last_status[24];
    char report_path[128];
} TumoNetBenchApp;

static const char* const tumonet_direction_labels[] = {"INT > EXT", "EXT > INT"};
static const char* const tumonet_fault_labels[] =
    {"Clean", "Drop", "Duplicate", "Replay", "Corrupt", "Wrong key", "Interrupt"};
static const uint8_t tumonet_payload_sizes[] = {48U, 96U, 144U};

static uint32_t tumonet_random_nonzero(void) {
    return (furi_hal_random_get() & 0x7FFFFFFFU) + 1U;
}

static TumoNetRadioDirection tumonet_data_direction(const TumoNetBenchApp* app) {
    return app->direction == 0U ? TumoNetRadioDirectionInternalToExternal :
                                  TumoNetRadioDirectionExternalToInternal;
}

static TumoNetRadioDirection tumonet_reverse_direction(TumoNetRadioDirection direction) {
    return direction == TumoNetRadioDirectionInternalToExternal ?
               TumoNetRadioDirectionExternalToInternal :
               TumoNetRadioDirectionInternalToExternal;
}

static void tumonet_model_sync_config(TumoNetBenchApp* app, bool redraw) {
    with_view_model(
        app->view,
        TumoNetBenchModel * model,
        {
            model->direction = app->direction;
            model->fault = app->fault;
            model->payload_index = app->payload_index;
            model->paired = app->paired;
        },
        redraw);
}

static void tumonet_model_progress(TumoNetBenchApp* app, const char* status) {
    with_view_model(
        app->view,
        TumoNetBenchModel * model,
        {
            model->fragments = app->last_fragments;
            model->retries = app->last_retries;
            model->tx_packets = app->last_tx_packets;
            model->rx_packets = app->last_rx_packets;
            model->frequency = app->last_frequency;
            strlcpy(model->status, status, sizeof(model->status));
        },
        true);
}

static void tumonet_draw_badge(Canvas* canvas, const TumoNetBenchModel* model) {
    const char* label =
        model->running ? "RUN" : (model->finished ? (model->passed ? "PASS" : "FAIL") : "IDLE");
    const uint8_t width = model->finished ? 34U : 29U;
    const uint8_t x = 127U - width;
    canvas_draw_rframe(canvas, x, 1, width, 11, 2);
    if(model->running || (model->finished && model->passed)) {
        canvas_draw_rbox(canvas, x, 1, width, 11, 2);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, x + width / 2U, 10, AlignCenter, AlignBottom, label);
    canvas_set_color(canvas, ColorBlack);
}

static void tumonet_draw_row(
    Canvas* canvas,
    uint8_t row,
    uint8_t selected,
    const char* label,
    const char* value) {
    const uint8_t y = 19U + row * 10U;
    if(row == selected) {
        canvas_draw_box(canvas, 0, y - 7U, 128, 9);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_draw_str(canvas, 3, y, label);
    canvas_draw_str_aligned(canvas, 125, y, AlignRight, AlignBottom, value);
    canvas_set_color(canvas, ColorBlack);
}

static void tumonet_draw_setup(Canvas* canvas, const TumoNetBenchModel* model) {
    char payload[12];
    snprintf(
        payload,
        sizeof(payload),
        "%u bytes",
        (unsigned int)tumonet_payload_sizes[model->payload_index]);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "TumoNet Bench");
    canvas_set_font(canvas, FontSecondary);
    tumonet_draw_row(
        canvas, 0U, model->selected_row, "Route", tumonet_direction_labels[model->direction]);
    tumonet_draw_row(canvas, 1U, model->selected_row, "Fault", tumonet_fault_labels[model->fault]);
    tumonet_draw_row(canvas, 2U, model->selected_row, "Payload", payload);
    tumonet_draw_row(canvas, 3U, model->selected_row, "About", "Protocol v1");
    elements_button_center(canvas, model->paired ? "Run" : "Pair");
}

static void tumonet_draw_pair(Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "Pair bench nodes");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 23, "Internal + external CC1101");
    canvas_draw_str(canvas, 2, 34, "New key lives in RAM only");
    canvas_draw_str(canvas, 2, 45, "Exit erases the session");
    elements_button_left(canvas, "Cancel");
    elements_button_center(canvas, "Pair");
}

static void tumonet_draw_run(Canvas* canvas, const TumoNetBenchModel* model) {
    char route[40];
    snprintf(
        route,
        sizeof(route),
        "%s  %lu.%02lu MHz",
        tumonet_direction_labels[model->direction],
        (unsigned long)(model->frequency / 1000000U),
        (unsigned long)((model->frequency / 10000U) % 100U));
    char stats[40];
    snprintf(
        stats,
        sizeof(stats),
        "F:%u R:%u TX:%u RX:%u",
        (unsigned int)model->fragments,
        (unsigned int)model->retries,
        (unsigned int)model->tx_packets,
        (unsigned int)model->rx_packets);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "TumoNet");
    tumonet_draw_badge(canvas, model);
    canvas_draw_line(canvas, 0, 14, 127, 14);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 23, route);
    canvas_draw_str(canvas, 2, 32, tumonet_fault_labels[model->fault]);
    canvas_draw_str(canvas, 2, 41, model->status);
    canvas_draw_str(canvas, 2, 50, stats);
    if(model->running) {
        elements_button_center(canvas, "Stop");
    } else {
        elements_button_left(canvas, "Back");
        elements_button_center(canvas, "Again");
        elements_button_right(canvas, model->report_written ? "Saved" : "Report");
    }
}

static void tumonet_draw_about(Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "TumoNet Bench");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 22, "v" TUMONET_BENCH_VERSION " / wire protocol 1");
    canvas_draw_str(canvas, 2, 33, "AES-CTR + HMAC-SHA256");
    canvas_draw_str(canvas, 2, 44, "One-device dual-radio test");
    canvas_draw_str(canvas, 2, 55, "github.com/squazaryu/tumoflip #86");
    elements_button_left(canvas, "Back");
}

static void tumonet_draw(Canvas* canvas, void* context) {
    const TumoNetBenchModel* model = context;
    canvas_clear(canvas);
    switch(model->screen) {
    case TumoNetBenchScreenPair:
        tumonet_draw_pair(canvas);
        break;
    case TumoNetBenchScreenRun:
        tumonet_draw_run(canvas, model);
        break;
    case TumoNetBenchScreenAbout:
        tumonet_draw_about(canvas);
        break;
    case TumoNetBenchScreenSetup:
    default:
        tumonet_draw_setup(canvas, model);
        break;
    }
}

static bool tumonet_radio_send_with_retry(
    TumoNetBenchApp* app,
    TumoNetRadio* radio,
    TumoNetRadioDirection direction,
    const uint8_t* frame,
    uint8_t frame_size,
    uint8_t* received,
    uint8_t* received_size,
    TumoNetRadioResult* final_result) {
    for(uint8_t attempt = 0U; attempt <= TUMONET_BENCH_MAX_RETRY; attempt++) {
        if(app->cancel_requested) {
            *final_result = TumoNetRadioResultCancelled;
            return false;
        }
        app->last_tx_packets++;
        const TumoNetRadioResult result = tumonet_radio_transfer(
            radio, direction, frame, frame_size, received, received_size, &app->cancel_requested);
        if(result == TumoNetRadioResultOk) {
            app->last_rx_packets++;
            *final_result = result;
            tumonet_model_progress(app, "Authenticated RF");
            return true;
        }
        *final_result = result;
        if(!tumonet_radio_result_is_retryable(result)) return false;
        if(attempt < TUMONET_BENCH_MAX_RETRY) {
            app->last_retries++;
            tumonet_model_progress(app, tumonet_radio_result_name(result));
        }
    }
    return false;
}

static bool tumonet_send_ack(
    TumoNetBenchApp* app,
    TumoNetRadio* radio,
    TumoNetEndpoint* receiver,
    TumoNetEndpoint* sender,
    TumoNetRadioDirection data_direction,
    uint16_t message_id,
    uint32_t acknowledged_counter,
    char* failure,
    size_t failure_size) {
    uint8_t ack_payload[TUMONET_ACK_PAYLOAD_SIZE];
    uint8_t frame[TUMONET_MAX_FRAME_SIZE];
    uint8_t frame_size = 0U;
    uint8_t received[TUMONET_MAX_FRAME_SIZE];
    uint8_t received_size = 0U;
    TumoNetPacket packet;
    tumonet_ack_encode_counter(acknowledged_counter, ack_payload);
    const TumoNetResult encoded = tumonet_encode(
        receiver,
        TumoNetFlagAck,
        message_id,
        0U,
        1U,
        ack_payload,
        sizeof(ack_payload),
        frame,
        &frame_size,
        NULL);
    if(encoded != TumoNetResultOk) {
        strlcpy(failure, tumonet_result_name(encoded), failure_size);
        return false;
    }

    TumoNetRadioResult radio_result = TumoNetRadioResultInit;
    if(!tumonet_radio_send_with_retry(
           app,
           radio,
           tumonet_reverse_direction(data_direction),
           frame,
           frame_size,
           received,
           &received_size,
           &radio_result)) {
        strlcpy(failure, tumonet_radio_result_name(radio_result), failure_size);
        return false;
    }
    const TumoNetResult decoded = tumonet_decode(sender, received, received_size, &packet);
    if(decoded != TumoNetResultOk || packet.flags != TumoNetFlagAck ||
       tumonet_ack_decode_counter(packet.payload) != acknowledged_counter) {
        strlcpy(
            failure,
            decoded == TumoNetResultOk ? "BAD ACK" : tumonet_result_name(decoded),
            failure_size);
        return false;
    }
    return true;
}

static bool tumonet_run_protocol(TumoNetBenchApp* app, TumoNetRadio* radio) {
    TumoNetEndpoint internal_endpoint;
    TumoNetEndpoint external_endpoint;
    TumoNetEndpoint* sender;
    TumoNetEndpoint* receiver;
    TumoNetReassembly reassembly;
    uint8_t payload[TUMONET_MAX_MESSAGE_SIZE];
    uint8_t assembled[TUMONET_MAX_MESSAGE_SIZE];
    uint8_t first_frame[TUMONET_MAX_FRAME_SIZE];
    uint8_t first_frame_size = 0U;
    size_t assembled_size = 0U;
    char failure[24] = "PROTOCOL";
    bool passed = false;

    const uint8_t payload_size = tumonet_payload_sizes[app->payload_index];
    const uint8_t fragment_count =
        (payload_size + TUMONET_FRAGMENT_PAYLOAD_SIZE - 1U) / TUMONET_FRAGMENT_PAYLOAD_SIZE;
    uint16_t message_id = (uint16_t)(tumonet_random_nonzero() & 0xFFFFU);
    if(message_id == 0U) message_id = 1U;
    for(uint8_t i = 0U; i < payload_size; i++)
        payload[i] = (uint8_t)(i * 29U + 7U);
    tumonet_reassembly_reset(&reassembly);

    if(!tumonet_endpoint_init(
           &internal_endpoint,
           TUMONET_NODE_INTERNAL,
           TUMONET_NODE_EXTERNAL,
           app->session_id,
           tumonet_random_nonzero(),
           app->master_key) ||
       !tumonet_endpoint_init(
           &external_endpoint,
           TUMONET_NODE_EXTERNAL,
           TUMONET_NODE_INTERNAL,
           app->session_id,
           tumonet_random_nonzero(),
           app->master_key)) {
        strlcpy(failure, "KEY DERIVATION", sizeof(failure));
        goto cleanup;
    }

    const TumoNetRadioDirection direction = tumonet_data_direction(app);
    sender = direction == TumoNetRadioDirectionInternalToExternal ? &internal_endpoint :
                                                                    &external_endpoint;
    receiver = direction == TumoNetRadioDirectionInternalToExternal ? &external_endpoint :
                                                                      &internal_endpoint;

    for(uint8_t fragment = 0U; fragment < fragment_count; fragment++) {
        if(app->cancel_requested) {
            strlcpy(failure, "STOPPED", sizeof(failure));
            goto cleanup;
        }
        const uint8_t offset = (uint8_t)(fragment * TUMONET_FRAGMENT_PAYLOAD_SIZE);
        const uint8_t remaining = (uint8_t)(payload_size - offset);
        const uint8_t fragment_size = remaining > TUMONET_FRAGMENT_PAYLOAD_SIZE ?
                                          (uint8_t)TUMONET_FRAGMENT_PAYLOAD_SIZE :
                                          remaining;
        uint8_t frame[TUMONET_MAX_FRAME_SIZE];
        uint8_t frame_size = 0U;
        uint32_t frame_counter = 0U;
        TumoNetResult protocol_result = tumonet_encode(
            sender,
            TumoNetFlagData,
            message_id,
            fragment,
            fragment_count,
            &payload[offset],
            fragment_size,
            frame,
            &frame_size,
            &frame_counter);
        if(protocol_result != TumoNetResultOk) {
            strlcpy(failure, tumonet_result_name(protocol_result), sizeof(failure));
            goto cleanup;
        }
        if(fragment == 0U) {
            memcpy(first_frame, frame, frame_size);
            first_frame_size = frame_size;
        }

        if(fragment == 0U && app->fault == TumoNetBenchFaultDrop) {
            app->last_retries++;
            tumonet_model_progress(app, "Drop injected");
        }

        uint8_t received[TUMONET_MAX_FRAME_SIZE];
        uint8_t received_size = 0U;
        TumoNetRadioResult radio_result = TumoNetRadioResultInit;

        if(fragment == 0U && app->fault == TumoNetBenchFaultCorrupt) {
            uint8_t corrupt[TUMONET_MAX_FRAME_SIZE];
            memcpy(corrupt, frame, frame_size);
            corrupt[TUMONET_HEADER_SIZE] ^= 0x80U;
            if(!tumonet_radio_send_with_retry(
                   app,
                   radio,
                   direction,
                   corrupt,
                   frame_size,
                   received,
                   &received_size,
                   &radio_result)) {
                strlcpy(failure, tumonet_radio_result_name(radio_result), sizeof(failure));
                goto cleanup;
            }
            TumoNetPacket rejected;
            if(tumonet_decode(receiver, received, received_size, &rejected) !=
               TumoNetResultAuthentication) {
                strlcpy(failure, "CORRUPT ACCEPTED", sizeof(failure));
                goto cleanup;
            }
            app->last_retries++;
            tumonet_model_progress(app, "Corrupt rejected");
        }

        if(!tumonet_radio_send_with_retry(
               app, radio, direction, frame, frame_size, received, &received_size, &radio_result)) {
            strlcpy(failure, tumonet_radio_result_name(radio_result), sizeof(failure));
            goto cleanup;
        }

        if(fragment == 0U && app->fault == TumoNetBenchFaultWrongKey) {
            uint8_t wrong_master[TUMONET_MASTER_KEY_SIZE];
            TumoNetEndpoint wrong_receiver;
            memcpy(wrong_master, app->master_key, sizeof(wrong_master));
            wrong_master[0] ^= 0xA5U;
            const bool wrong_ready = tumonet_endpoint_init(
                &wrong_receiver,
                receiver->local_id,
                sender->local_id,
                app->session_id,
                tumonet_random_nonzero(),
                wrong_master);
            tumonet_crypto_zero(wrong_master, sizeof(wrong_master));
            TumoNetPacket rejected;
            const TumoNetResult wrong_result =
                wrong_ready ? tumonet_decode(&wrong_receiver, received, received_size, &rejected) :
                              TumoNetResultCrypto;
            tumonet_endpoint_clear(&wrong_receiver);
            if(wrong_result != TumoNetResultAuthentication) {
                strlcpy(failure, "WRONG KEY ACCEPTED", sizeof(failure));
                goto cleanup;
            }
            strlcpy(failure, "Wrong key rejected", sizeof(failure));
            passed = true;
            goto cleanup;
        }

        TumoNetPacket packet;
        protocol_result = tumonet_decode(receiver, received, received_size, &packet);
        if(protocol_result != TumoNetResultOk) {
            strlcpy(failure, tumonet_result_name(protocol_result), sizeof(failure));
            goto cleanup;
        }
        app->last_fragments++;
        protocol_result = tumonet_reassembly_push(
            &reassembly, &packet, assembled, sizeof(assembled), &assembled_size);

        if(fragment == 0U && app->fault == TumoNetBenchFaultDuplicate) {
            if(!tumonet_radio_send_with_retry(
                   app,
                   radio,
                   direction,
                   frame,
                   frame_size,
                   received,
                   &received_size,
                   &radio_result)) {
                strlcpy(failure, tumonet_radio_result_name(radio_result), sizeof(failure));
                goto cleanup;
            }
            TumoNetPacket duplicate;
            if(tumonet_decode(receiver, received, received_size, &duplicate) !=
               TumoNetResultDuplicate) {
                strlcpy(failure, "DUP ACCEPTED", sizeof(failure));
                goto cleanup;
            }
            tumonet_model_progress(app, "Duplicate rejected");
        }

        if(!tumonet_send_ack(
               app,
               radio,
               receiver,
               sender,
               direction,
               message_id,
               frame_counter,
               failure,
               sizeof(failure))) {
            goto cleanup;
        }

        if(fragment == 0U && app->fault == TumoNetBenchFaultInterrupt) {
            tumonet_reassembly_reset(&reassembly);
            strlcpy(failure, "Interrupt cleaned", sizeof(failure));
            passed = protocol_result == TumoNetResultPending;
            goto cleanup;
        }

        const TumoNetResult expected = fragment + 1U == fragment_count ? TumoNetResultComplete :
                                                                         TumoNetResultPending;
        if(protocol_result != expected) {
            strlcpy(failure, tumonet_result_name(protocol_result), sizeof(failure));
            goto cleanup;
        }
    }

    if(assembled_size != payload_size || memcmp(assembled, payload, payload_size) != 0) {
        strlcpy(failure, "PAYLOAD MISMATCH", sizeof(failure));
        goto cleanup;
    }

    if(app->fault == TumoNetBenchFaultReplay) {
        uint8_t received[TUMONET_MAX_FRAME_SIZE];
        uint8_t received_size = 0U;
        TumoNetRadioResult radio_result;
        if(!tumonet_radio_send_with_retry(
               app,
               radio,
               direction,
               first_frame,
               first_frame_size,
               received,
               &received_size,
               &radio_result)) {
            strlcpy(failure, tumonet_radio_result_name(radio_result), sizeof(failure));
            goto cleanup;
        }
        TumoNetPacket replay;
        const TumoNetResult replay_result =
            tumonet_decode(receiver, received, received_size, &replay);
        if(replay_result != TumoNetResultDuplicate && replay_result != TumoNetResultReplay) {
            strlcpy(failure, "REPLAY ACCEPTED", sizeof(failure));
            goto cleanup;
        }
        tumonet_model_progress(app, "Replay rejected");
    }

    strlcpy(failure, "Message verified", sizeof(failure));
    passed = true;

cleanup:
    tumonet_endpoint_clear(&internal_endpoint);
    tumonet_endpoint_clear(&external_endpoint);
    tumonet_crypto_zero(payload, sizeof(payload));
    tumonet_crypto_zero(assembled, sizeof(assembled));
    tumonet_crypto_zero(first_frame, sizeof(first_frame));
    strlcpy(app->last_status, failure, sizeof(app->last_status));
    return passed;
}

static int32_t tumonet_worker(void* context) {
    TumoNetBenchApp* app = context;
    TumoNetRadio* radio = tumonet_radio_alloc();
    TumoNetRadioResult radio_result = radio != NULL ? tumonet_radio_open(radio) :
                                                      TumoNetRadioResultInit;
    if(radio_result == TumoNetRadioResultOk) {
        app->last_frequency = tumonet_radio_frequency(radio);
        tumonet_model_progress(app, "Radio ready");
        app->last_passed = tumonet_run_protocol(app, radio);
    } else {
        app->last_passed = false;
        strlcpy(
            app->last_status, tumonet_radio_result_name(radio_result), sizeof(app->last_status));
    }
    tumonet_radio_free(radio);

    with_view_model(
        app->view,
        TumoNetBenchModel * model,
        {
            model->running = false;
            model->finished = true;
            model->passed = app->last_passed;
            model->fragments = app->last_fragments;
            model->retries = app->last_retries;
            model->tx_packets = app->last_tx_packets;
            model->rx_packets = app->last_rx_packets;
            model->frequency = app->last_frequency;
            strlcpy(model->status, app->last_status, sizeof(model->status));
        },
        true);
    notification_message(
        app->notifications, app->last_passed ? &sequence_success : &sequence_error);
    return 0;
}

static void tumonet_join_worker(TumoNetBenchApp* app) {
    if(app->worker == NULL) return;
    app->cancel_requested = true;
    furi_thread_join(app->worker);
    furi_thread_free(app->worker);
    app->worker = NULL;
}

static void tumonet_start(TumoNetBenchApp* app) {
    tumonet_join_worker(app);
    app->cancel_requested = false;
    app->last_passed = false;
    app->last_fragments = 0U;
    app->last_retries = 0U;
    app->last_tx_packets = 0U;
    app->last_rx_packets = 0U;
    app->last_frequency = 0U;
    app->report_path[0] = '\0';
    strlcpy(app->last_status, "Opening radios", sizeof(app->last_status));
    with_view_model(
        app->view,
        TumoNetBenchModel * model,
        {
            model->screen = TumoNetBenchScreenRun;
            model->running = true;
            model->finished = false;
            model->passed = false;
            model->report_written = false;
            model->fragments = 0U;
            model->retries = 0U;
            model->tx_packets = 0U;
            model->rx_packets = 0U;
            model->frequency = 0U;
            strlcpy(model->status, app->last_status, sizeof(model->status));
        },
        true);
    app->worker = furi_thread_alloc_ex("TumoNetBench", 8U * 1024U, tumonet_worker, app);
    furi_thread_start(app->worker);
}

static bool tumonet_write_report(TumoNetBenchApp* app) {
    storage_common_mkdir(app->storage, TUMONET_BENCH_DATA_DIR);
    storage_common_mkdir(app->storage, TUMONET_BENCH_REPORT_DIR);
    snprintf(
        app->report_path,
        sizeof(app->report_path),
        TUMONET_BENCH_REPORT_DIR "/bench_%lu.txt",
        (unsigned long)furi_hal_rtc_get_timestamp());

    char report[512];
    const int length = snprintf(
        report,
        sizeof(report),
        "TumoNet Bench report\n"
        "app_version=%s\n"
        "protocol=1\n"
        "result=%s\n"
        "route=%s\n"
        "fault=%s\n"
        "payload_bytes=%u\n"
        "frequency_hz=%lu\n"
        "fragments=%u\n"
        "retries=%u\n"
        "rf_tx=%u\n"
        "rf_rx=%u\n"
        "status=%s\n"
        "secret_material=not_exported\n",
        TUMONET_BENCH_VERSION,
        app->last_passed ? "PASS" : "FAIL",
        tumonet_direction_labels[app->direction],
        tumonet_fault_labels[app->fault],
        (unsigned int)tumonet_payload_sizes[app->payload_index],
        (unsigned long)app->last_frequency,
        (unsigned int)app->last_fragments,
        (unsigned int)app->last_retries,
        (unsigned int)app->last_tx_packets,
        (unsigned int)app->last_rx_packets,
        app->last_status);
    if(length <= 0 || (size_t)length >= sizeof(report)) return false;

    File* file = storage_file_alloc(app->storage);
    const bool opened = storage_file_open(file, app->report_path, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    const bool written = opened &&
                         storage_file_write(file, report, (size_t)length) == (size_t)length;
    storage_file_close(file);
    storage_file_free(file);
    return written;
}

static void tumonet_pair(TumoNetBenchApp* app) {
    furi_hal_random_fill_buf(app->master_key, sizeof(app->master_key));
    app->session_id = tumonet_random_nonzero();
    app->paired = true;
    with_view_model(
        app->view,
        TumoNetBenchModel * model,
        {
            model->screen = TumoNetBenchScreenSetup;
            model->paired = true;
        },
        true);
}

static bool tumonet_input(InputEvent* event, void* context) {
    TumoNetBenchApp* app = context;
    if(event->type != InputTypeShort) return false;

    TumoNetBenchScreen screen;
    bool running;
    bool finished;
    uint8_t selected;
    with_view_model(
        app->view,
        TumoNetBenchModel * model,
        {
            screen = model->screen;
            running = model->running;
            finished = model->finished;
            selected = model->selected_row;
        },
        false);

    if(screen == TumoNetBenchScreenSetup) {
        if(event->key == InputKeyBack) {
            view_dispatcher_stop(app->view_dispatcher);
            return true;
        }
        if(event->key == InputKeyUp || event->key == InputKeyDown) {
            selected = event->key == InputKeyUp ? (selected + 3U) % 4U : (selected + 1U) % 4U;
            with_view_model(
                app->view, TumoNetBenchModel * model, { model->selected_row = selected; }, true);
            return true;
        }
        if(event->key == InputKeyLeft || event->key == InputKeyRight) {
            const bool next = event->key == InputKeyRight;
            if(selected == 0U) {
                app->direction ^= 1U;
            } else if(selected == 1U) {
                app->fault =
                    next ? (app->fault + 1U) % TumoNetBenchFaultCount :
                           (app->fault + TumoNetBenchFaultCount - 1U) % TumoNetBenchFaultCount;
            } else if(selected == 2U) {
                app->payload_index =
                    next ? (app->payload_index + 1U) % COUNT_OF(tumonet_payload_sizes) :
                           (app->payload_index + COUNT_OF(tumonet_payload_sizes) - 1U) %
                               COUNT_OF(tumonet_payload_sizes);
            }
            tumonet_model_sync_config(app, true);
            return true;
        }
        if(event->key == InputKeyOk) {
            if(selected == 3U) {
                with_view_model(
                    app->view,
                    TumoNetBenchModel * model,
                    { model->screen = TumoNetBenchScreenAbout; },
                    true);
            } else if(app->paired) {
                tumonet_start(app);
            } else {
                with_view_model(
                    app->view,
                    TumoNetBenchModel * model,
                    { model->screen = TumoNetBenchScreenPair; },
                    true);
            }
            return true;
        }
    } else if(screen == TumoNetBenchScreenPair) {
        if(event->key == InputKeyBack || event->key == InputKeyLeft) {
            with_view_model(
                app->view,
                TumoNetBenchModel * model,
                { model->screen = TumoNetBenchScreenSetup; },
                true);
            return true;
        }
        if(event->key == InputKeyOk) {
            tumonet_pair(app);
            return true;
        }
    } else if(screen == TumoNetBenchScreenAbout) {
        if(event->key == InputKeyBack || event->key == InputKeyLeft) {
            with_view_model(
                app->view,
                TumoNetBenchModel * model,
                { model->screen = TumoNetBenchScreenSetup; },
                true);
            return true;
        }
    } else if(screen == TumoNetBenchScreenRun) {
        if(running) {
            if(event->key == InputKeyBack || event->key == InputKeyOk) {
                app->cancel_requested = true;
                tumonet_model_progress(app, "Stopping safely");
                return true;
            }
        } else if(finished) {
            if(event->key == InputKeyBack || event->key == InputKeyLeft) {
                tumonet_join_worker(app);
                with_view_model(
                    app->view,
                    TumoNetBenchModel * model,
                    { model->screen = TumoNetBenchScreenSetup; },
                    true);
                return true;
            }
            if(event->key == InputKeyOk) {
                tumonet_start(app);
                return true;
            }
            if(event->key == InputKeyRight) {
                const bool written = tumonet_write_report(app);
                with_view_model(
                    app->view,
                    TumoNetBenchModel * model,
                    {
                        model->report_written = written;
                        strlcpy(
                            model->status,
                            written ? "Report saved" : "Report failed",
                            sizeof(model->status));
                    },
                    true);
                notification_message(
                    app->notifications, written ? &sequence_success : &sequence_error);
                return true;
            }
        }
    }
    return false;
}

static TumoNetBenchApp* tumonet_alloc(void) {
    TumoNetBenchApp* app = calloc(1U, sizeof(TumoNetBenchApp));
    app->gui = furi_record_open(RECORD_GUI);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->view_dispatcher = view_dispatcher_alloc();
    app->view = view_alloc();
    app->direction = 0U;
    app->fault = TumoNetBenchFaultClean;
    app->payload_index = 0U;

    view_allocate_model(app->view, ViewModelTypeLocking, sizeof(TumoNetBenchModel));
    view_set_context(app->view, app);
    view_set_draw_callback(app->view, tumonet_draw);
    view_set_input_callback(app->view, tumonet_input);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_add_view(app->view_dispatcher, 0U, app->view);
    with_view_model(
        app->view,
        TumoNetBenchModel * model,
        {
            memset(model, 0, sizeof(*model));
            model->screen = TumoNetBenchScreenSetup;
            strlcpy(model->status, "Ready", sizeof(model->status));
        },
        false);
    tumonet_model_sync_config(app, false);
    return app;
}

static void tumonet_free(TumoNetBenchApp* app) {
    tumonet_join_worker(app);
    tumonet_crypto_zero(app->master_key, sizeof(app->master_key));
    app->session_id = 0U;
    app->paired = false;
    view_dispatcher_remove_view(app->view_dispatcher, 0U);
    view_free(app->view);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_NOTIFICATION);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_GUI);
    free(app);
}

int32_t tumonet_bench_app(void* context) {
    UNUSED(context);
    TumoNetBenchApp* app = tumonet_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, 0U);
    view_dispatcher_run(app->view_dispatcher);
    tumonet_free(app);
    return 0;
}
