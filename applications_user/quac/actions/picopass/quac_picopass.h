/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Tumoflip contributors
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QUAC_PICOPASS_BLOCK_SIZE       8U
#define QUAC_PICOPASS_MAX_BLOCKS       32U
#define QUAC_PICOPASS_MIN_DURATION_MS  100U
#define QUAC_PICOPASS_MAX_DURATION_MS  10000U
#define QUAC_PICOPASS_MAX_FILE_SIZE    4096U
#define QUAC_PICOPASS_MAX_RESPONSE_LEN 34U

typedef struct {
    uint8_t blocks[QUAC_PICOPASS_MAX_BLOCKS][QUAC_PICOPASS_BLOCK_SIZE];
    uint8_t app_limit;
} QuacPicopassCredential;

typedef enum {
    QuacPicopassParseOk,
    QuacPicopassParseInvalid,
    QuacPicopassParseUnsupportedSecure,
} QuacPicopassParseResult;

typedef enum {
    QuacPicopassStateIdle,
    QuacPicopassStateActivated,
    QuacPicopassStateSelected,
    QuacPicopassStateHalted,
} QuacPicopassState;

typedef struct {
    const QuacPicopassCredential* credential;
    QuacPicopassState state;
} QuacPicopassSession;

typedef enum {
    QuacPicopassFrameIgnored,
    QuacPicopassFrameRejected,
    QuacPicopassFrameResponse,
    QuacPicopassFrameSof,
    QuacPicopassFrameError,
} QuacPicopassFrameResult;

typedef enum {
    QuacPicopassWaitComplete,
    QuacPicopassWaitCancelled,
    QuacPicopassWaitError,
} QuacPicopassWaitResult;

typedef enum {
    QuacPicopassRunComplete,
    QuacPicopassRunCancelled,
    QuacPicopassRunInvalidDuration,
    QuacPicopassRunLoadError,
    QuacPicopassRunUnsupportedSecure,
    QuacPicopassRunStartError,
    QuacPicopassRunWaitError,
} QuacPicopassRunResult;

typedef struct {
    void* context;
    QuacPicopassParseResult (
        *load)(void* context, const char* path, QuacPicopassCredential* credential);
    bool (*start)(void* context, QuacPicopassSession* session);
    QuacPicopassWaitResult (*wait)(void* context, uint32_t duration_ms);
    void (*stop)(void* context);
} QuacPicopassRuntimeOps;

QuacPicopassParseResult quac_picopass_parse(
    const char* file_data,
    size_t file_size,
    QuacPicopassCredential* credential);

void quac_picopass_credential_clear(QuacPicopassCredential* credential);

bool quac_picopass_duration_parse(const char* text, uint32_t* duration_ms);

void quac_picopass_session_init(
    QuacPicopassSession* session,
    const QuacPicopassCredential* credential);

void quac_picopass_session_clear(QuacPicopassSession* session);

QuacPicopassFrameResult quac_picopass_process_frame(
    QuacPicopassSession* session,
    const uint8_t* request,
    size_t request_size,
    uint8_t* response,
    size_t response_capacity,
    size_t* response_size,
    bool* response_is_sof);

QuacPicopassRunResult
    quac_picopass_run(const QuacPicopassRuntimeOps* ops, const char* path, uint32_t duration_ms);
