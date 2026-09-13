/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 Tumoflip contributors
 *
 * Clean-room, non-secure-only Picopass file and response state machine.
 * Protocol and licensing references are documented in PROVENANCE.md.
 */
#include "quac_picopass.h"

#include <limits.h>
#include <string.h>

#define QUAC_PICOPASS_HEADER          "Flipper Picopass device"
#define QUAC_PICOPASS_FORMAT_VERSION  1U
#define QUAC_PICOPASS_MAX_LINE_SIZE   128U
#define QUAC_PICOPASS_NONSECURE_MODE  1U
#define QUAC_PICOPASS_ENCRYPTION_NONE 0x14U

static void quac_picopass_secure_clear(void* memory, size_t size) {
    volatile uint8_t* bytes = memory;
    while(size--)
        *bytes++ = 0;
}

void quac_picopass_credential_clear(QuacPicopassCredential* credential) {
    if(credential) quac_picopass_secure_clear(credential, sizeof(*credential));
}

void quac_picopass_session_clear(QuacPicopassSession* session) {
    if(session) quac_picopass_secure_clear(session, sizeof(*session));
}

static void quac_picopass_trim(const char** begin, const char** end) {
    while(*begin < *end && (**begin == ' ' || **begin == '\t'))
        ++*begin;
    while(*end > *begin && ((*end)[-1] == ' ' || (*end)[-1] == '\t' || (*end)[-1] == '\r'))
        --*end;
}

static bool quac_picopass_text_equal(const char* begin, const char* end, const char* expected) {
    const size_t length = (size_t)(end - begin);
    return strlen(expected) == length && !memcmp(begin, expected, length);
}

static bool quac_picopass_parse_decimal(
    const char* begin,
    const char* end,
    uint32_t* value,
    bool canonical) {
    if(begin == end) return false;
    if(canonical && end - begin > 1 && *begin == '0') return false;
    uint32_t parsed = 0;
    for(const char* cursor = begin; cursor < end; ++cursor) {
        if(*cursor < '0' || *cursor > '9') return false;
        const uint32_t digit = (uint32_t)(*cursor - '0');
        if(parsed > (UINT32_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }
    *value = parsed;
    return true;
}

static int quac_picopass_hex_nibble(char value) {
    if(value >= '0' && value <= '9') return value - '0';
    if(value >= 'A' && value <= 'F') return value - 'A' + 10;
    if(value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

static bool quac_picopass_parse_hex8(
    const char* begin,
    const char* end,
    uint8_t output[QUAC_PICOPASS_BLOCK_SIZE]) {
    uint8_t parsed[QUAC_PICOPASS_BLOCK_SIZE] = {0};
    bool valid = true;
    for(size_t index = 0; index < QUAC_PICOPASS_BLOCK_SIZE; ++index) {
        if(end - begin < 2) {
            valid = false;
            break;
        }
        const int high = quac_picopass_hex_nibble(begin[0]);
        const int low = quac_picopass_hex_nibble(begin[1]);
        if(high < 0 || low < 0) {
            valid = false;
            break;
        }
        parsed[index] = (uint8_t)((high << 4) | low);
        begin += 2;
        if(index + 1U < QUAC_PICOPASS_BLOCK_SIZE) {
            if(begin == end || *begin != ' ') {
                valid = false;
                break;
            }
            ++begin;
        }
    }
    if(begin != end) valid = false;
    if(valid) memcpy(output, parsed, sizeof(parsed));
    quac_picopass_secure_clear(parsed, sizeof(parsed));
    return valid;
}

static bool quac_picopass_is_placeholder(const uint8_t block[8]) {
    uint8_t any_nonzero = 0;
    uint8_t any_not_ff = 0;
    for(size_t index = 0; index < 8; ++index) {
        any_nonzero |= block[index];
        any_not_ff |= (uint8_t)(block[index] ^ 0xFFU);
    }
    return any_nonzero == 0 || any_not_ff == 0;
}

bool quac_picopass_duration_parse(const char* text, uint32_t* duration_ms) {
    if(!text || !duration_ms) return false;
    const char* end = text + strlen(text);
    uint32_t value = 0;
    if(!quac_picopass_parse_decimal(text, end, &value, true)) return false;
    if(value < QUAC_PICOPASS_MIN_DURATION_MS || value > QUAC_PICOPASS_MAX_DURATION_MS)
        return false;
    *duration_ms = value;
    return true;
}

QuacPicopassParseResult quac_picopass_parse(
    const char* file_data,
    size_t file_size,
    QuacPicopassCredential* credential) {
    if(!credential) return QuacPicopassParseInvalid;
    quac_picopass_credential_clear(credential);
    if(!file_data || !file_size || file_size > QUAC_PICOPASS_MAX_FILE_SIZE ||
       memchr(file_data, 0, file_size)) {
        return QuacPicopassParseInvalid;
    }

    bool blocks_seen[QUAC_PICOPASS_MAX_BLOCKS] = {false};
    uint8_t metadata_seen = 0;
    size_t structural_line = 0;
    const char* cursor = file_data;
    const char* const file_end = file_data + file_size;
    bool valid = true;
    bool unsupported_secure = false;

    while(cursor < file_end && valid) {
        const char* line_end = memchr(cursor, '\n', (size_t)(file_end - cursor));
        if(!line_end) line_end = file_end;
        if((size_t)(line_end - cursor) >= QUAC_PICOPASS_MAX_LINE_SIZE) {
            valid = false;
            break;
        }
        const char* begin = cursor;
        const char* end = line_end;
        quac_picopass_trim(&begin, &end);
        cursor = line_end < file_end ? line_end + 1 : file_end;
        if(begin == end || *begin == '#') continue;

        const char* separator = memchr(begin, ':', (size_t)(end - begin));
        if(!separator) {
            valid = false;
            break;
        }
        const char* key_begin = begin;
        const char* key_end = separator;
        const char* value_begin = separator + 1;
        const char* value_end = end;
        quac_picopass_trim(&key_begin, &key_end);
        quac_picopass_trim(&value_begin, &value_end);

        if(structural_line == 0) {
            valid = quac_picopass_text_equal(key_begin, key_end, "Filetype") &&
                    quac_picopass_text_equal(value_begin, value_end, QUAC_PICOPASS_HEADER);
            structural_line++;
            continue;
        }
        if(structural_line == 1) {
            uint32_t version = 0;
            valid = quac_picopass_text_equal(key_begin, key_end, "Version") &&
                    quac_picopass_parse_decimal(value_begin, value_end, &version, true) &&
                    version == QUAC_PICOPASS_FORMAT_VERSION;
            structural_line++;
            continue;
        }

        if(quac_picopass_text_equal(key_begin, key_end, "Facility Code") ||
           quac_picopass_text_equal(key_begin, key_end, "Card Number")) {
            const uint8_t bit =
                quac_picopass_text_equal(key_begin, key_end, "Facility Code") ? 0x01U : 0x02U;
            uint32_t ignored = 0;
            valid = !(metadata_seen & bit) &&
                    quac_picopass_parse_decimal(value_begin, value_end, &ignored, false);
            metadata_seen |= bit;
            continue;
        }

        uint8_t metadata_bit = 0;
        if(quac_picopass_text_equal(key_begin, key_end, "Credential")) {
            metadata_bit = 0x04;
        } else if(quac_picopass_text_equal(key_begin, key_end, "PIN")) {
            metadata_bit = 0x08;
        } else if(quac_picopass_text_equal(key_begin, key_end, "PIN(cont.)")) {
            metadata_bit = 0x10;
        }
        if(metadata_bit) {
            uint8_t ignored[QUAC_PICOPASS_BLOCK_SIZE] = {0};
            valid = !(metadata_seen & metadata_bit) &&
                    quac_picopass_parse_hex8(value_begin, value_end, ignored);
            metadata_seen |= metadata_bit;
            quac_picopass_secure_clear(ignored, sizeof(ignored));
            continue;
        }

        static const char block_prefix[] = "Block ";
        if((size_t)(key_end - key_begin) <= sizeof(block_prefix) - 1 ||
           memcmp(key_begin, block_prefix, sizeof(block_prefix) - 1)) {
            valid = false;
            break;
        }

        uint32_t block = 0;
        uint8_t parsed_block[QUAC_PICOPASS_BLOCK_SIZE] = {0};
        valid = quac_picopass_parse_decimal(
                    key_begin + sizeof(block_prefix) - 1, key_end, &block, true) &&
                block < QUAC_PICOPASS_MAX_BLOCKS && !blocks_seen[block] &&
                quac_picopass_parse_hex8(value_begin, value_end, parsed_block);
        if(valid) {
            blocks_seen[block] = true;
            if(block == 3U || block == 4U) {
                unsupported_secure |= !quac_picopass_is_placeholder(parsed_block);
            } else {
                memcpy(credential->blocks[block], parsed_block, sizeof(parsed_block));
            }
        }
        quac_picopass_secure_clear(parsed_block, sizeof(parsed_block));
    }

    if(valid) valid = structural_line == 2 && blocks_seen[0] && blocks_seen[1];
    uint8_t app_limit = 0;
    if(valid) {
        app_limit = credential->blocks[1][0];
        if(app_limit == 0xFFU) app_limit = QUAC_PICOPASS_MAX_BLOCKS;
        valid = app_limit >= 6U && app_limit <= QUAC_PICOPASS_MAX_BLOCKS;
    }
    if(valid) {
        for(size_t block = 0; block < QUAC_PICOPASS_MAX_BLOCKS; ++block) {
            if(blocks_seen[block] != (block < app_limit)) {
                valid = false;
                break;
            }
        }
    }
    if(valid) {
        const uint8_t page_mode = (uint8_t)((credential->blocks[1][7] & 0x18U) >> 3);
        unsupported_secure |= page_mode != QUAC_PICOPASS_NONSECURE_MODE;
        if(app_limit > 6U)
            unsupported_secure |= credential->blocks[6][7] != QUAC_PICOPASS_ENCRYPTION_NONE;
        credential->app_limit = app_limit;
    }

    if(!valid || unsupported_secure) quac_picopass_credential_clear(credential);
    if(!valid) return QuacPicopassParseInvalid;
    if(unsupported_secure) return QuacPicopassParseUnsupportedSecure;
    return QuacPicopassParseOk;
}

static uint16_t quac_picopass_crc(const uint8_t* data, size_t size) {
    uint16_t crc = 0xE012U;
    for(size_t index = 0; index < size; ++index) {
        crc ^= data[index];
        for(size_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? (uint16_t)((crc >> 1) ^ 0x8408U) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

static bool quac_picopass_crc_valid(const uint8_t* frame, size_t size) {
    if(size < 3) return false;
    const uint16_t expected = quac_picopass_crc(frame, size - 2);
    const uint16_t actual = (uint16_t)frame[size - 2] | (uint16_t)((uint16_t)frame[size - 1] << 8);
    return expected == actual;
}

static bool quac_picopass_response_with_crc(
    const uint8_t* data,
    size_t data_size,
    uint8_t* response,
    size_t response_capacity,
    size_t* response_size) {
    if(response_capacity < data_size + 2U) return false;
    memcpy(response, data, data_size);
    const uint16_t crc = quac_picopass_crc(data, data_size);
    response[data_size] = (uint8_t)(crc & 0xFFU);
    response[data_size + 1U] = (uint8_t)(crc >> 8);
    *response_size = data_size + 2U;
    return true;
}

static void quac_picopass_rotate_csn(const uint8_t csn[8], uint8_t rotated[8]) {
    for(size_t index = 0; index < 8; ++index) {
        rotated[index] = (uint8_t)((csn[index] >> 3) | (uint8_t)(csn[(index + 1U) % 8U] << 5));
    }
}

static bool quac_picopass_equal(const uint8_t* left, const uint8_t* right, size_t size) {
    uint8_t difference = 0;
    for(size_t index = 0; index < size; ++index)
        difference |= left[index] ^ right[index];
    return difference == 0;
}

void quac_picopass_session_init(
    QuacPicopassSession* session,
    const QuacPicopassCredential* credential) {
    if(!session) return;
    quac_picopass_session_clear(session);
    session->credential = credential;
    session->state = QuacPicopassStateIdle;
}

QuacPicopassFrameResult quac_picopass_process_frame(
    QuacPicopassSession* session,
    const uint8_t* request,
    size_t request_size,
    uint8_t* response,
    size_t response_capacity,
    size_t* response_size,
    bool* response_is_sof) {
    if(response_size) *response_size = 0;
    if(response_is_sof) *response_is_sof = false;
    if(!session || !session->credential || !request || !request_size || !response ||
       !response_size || !response_is_sof) {
        return QuacPicopassFrameError;
    }

    const QuacPicopassCredential* credential = session->credential;
    const uint8_t command = request[0];

    if(command == 0x0AU && request_size == 1U) {
        session->state = QuacPicopassStateActivated;
        *response_is_sof = true;
        return QuacPicopassFrameSof;
    }

    if(command == 0x0CU && request_size == 1U) {
        if(session->state != QuacPicopassStateActivated &&
           session->state != QuacPicopassStateSelected) {
            return QuacPicopassFrameRejected;
        }
        uint8_t rotated[8] = {0};
        quac_picopass_rotate_csn(credential->blocks[0], rotated);
        const bool success = quac_picopass_response_with_crc(
            rotated, sizeof(rotated), response, response_capacity, response_size);
        quac_picopass_secure_clear(rotated, sizeof(rotated));
        return success ? QuacPicopassFrameResponse : QuacPicopassFrameError;
    }

    if(command == 0x81U && request_size == 9U) {
        uint8_t expected[8] = {0};
        if(session->state == QuacPicopassStateActivated ||
           session->state == QuacPicopassStateSelected) {
            quac_picopass_rotate_csn(credential->blocks[0], expected);
        } else if(
            session->state == QuacPicopassStateIdle || session->state == QuacPicopassStateHalted) {
            memcpy(expected, credential->blocks[0], sizeof(expected));
        } else {
            return QuacPicopassFrameRejected;
        }
        if(!quac_picopass_equal(request + 1, expected, sizeof(expected))) {
            quac_picopass_secure_clear(expected, sizeof(expected));
            session->state = QuacPicopassStateIdle;
            return QuacPicopassFrameRejected;
        }
        quac_picopass_secure_clear(expected, sizeof(expected));
        session->state = QuacPicopassStateSelected;
        return quac_picopass_response_with_crc(
                   credential->blocks[0], 8, response, response_capacity, response_size) ?
                   QuacPicopassFrameResponse :
                   QuacPicopassFrameError;
    }

    if(command == 0x00U && request_size == 1U) {
        if(session->state != QuacPicopassStateSelected) return QuacPicopassFrameRejected;
        session->state = QuacPicopassStateHalted;
        *response_is_sof = true;
        return QuacPicopassFrameSof;
    }

    if((command == 0x0CU || command == 0x06U) && request_size == 4U) {
        if(session->state != QuacPicopassStateSelected) return QuacPicopassFrameRejected;
        if(!quac_picopass_crc_valid(request + 1, request_size - 1U))
            return QuacPicopassFrameIgnored;
        const size_t first_block = request[1];
        const size_t block_count = command == 0x06U ? 4U : 1U;
        if(first_block >= credential->app_limit ||
           block_count > (size_t)credential->app_limit - first_block) {
            return QuacPicopassFrameRejected;
        }
        uint8_t blocks[32] = {0};
        for(size_t offset = 0; offset < block_count; ++offset) {
            const size_t block = first_block + offset;
            if(block == 3U || block == 4U) {
                memset(blocks + offset * 8U, 0xFF, 8);
            } else {
                memcpy(blocks + offset * 8U, credential->blocks[block], 8);
            }
        }
        const bool success = quac_picopass_response_with_crc(
            blocks, block_count * 8U, response, response_capacity, response_size);
        quac_picopass_secure_clear(blocks, sizeof(blocks));
        return success ? QuacPicopassFrameResponse : QuacPicopassFrameError;
    }

    return QuacPicopassFrameIgnored;
}

QuacPicopassRunResult
    quac_picopass_run(const QuacPicopassRuntimeOps* ops, const char* path, uint32_t duration_ms) {
    if(!ops || !path || !ops->load || !ops->start || !ops->wait || !ops->stop ||
       duration_ms < QUAC_PICOPASS_MIN_DURATION_MS ||
       duration_ms > QUAC_PICOPASS_MAX_DURATION_MS) {
        return QuacPicopassRunInvalidDuration;
    }

    QuacPicopassCredential credential = {0};
    QuacPicopassSession session = {0};
    QuacPicopassRunResult result = QuacPicopassRunLoadError;
    bool start_attempted = false;

    const QuacPicopassParseResult parse_result = ops->load(ops->context, path, &credential);
    if(parse_result == QuacPicopassParseUnsupportedSecure) {
        result = QuacPicopassRunUnsupportedSecure;
        goto cleanup;
    }
    if(parse_result != QuacPicopassParseOk) goto cleanup;

    quac_picopass_session_init(&session, &credential);
    start_attempted = true;
    if(!ops->start(ops->context, &session)) {
        result = QuacPicopassRunStartError;
        goto cleanup;
    }

    const QuacPicopassWaitResult wait_result = ops->wait(ops->context, duration_ms);
    if(wait_result == QuacPicopassWaitComplete) {
        result = QuacPicopassRunComplete;
    } else if(wait_result == QuacPicopassWaitCancelled) {
        result = QuacPicopassRunCancelled;
    } else {
        result = QuacPicopassRunWaitError;
    }

cleanup:
    if(start_attempted) ops->stop(ops->context);
    quac_picopass_session_clear(&session);
    quac_picopass_credential_clear(&credential);
    return result;
}
