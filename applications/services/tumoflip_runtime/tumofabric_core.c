#include "tumofabric_core.h"

#include <stdio.h>
#include <string.h>

#define TUMOFABRIC_COUNTER_MIN (-999)
#define TUMOFABRIC_COUNTER_MAX 999

typedef struct {
    const uint8_t* value;
    size_t size;
    bool present;
} TumoFabricField;

static bool tumofabric_parse_fields(
    const uint8_t* payload,
    size_t payload_size,
    const char* const* keys,
    TumoFabricField* fields,
    size_t field_count) {
    if(!payload || payload_size == 0U) return false;

    size_t offset = 0U;
    while(offset < payload_size) {
        const size_t pair_start = offset;
        size_t equals = payload_size;
        while(offset < payload_size && payload[offset] != ';') {
            if(payload[offset] == '=' && equals == payload_size) equals = offset;
            offset++;
        }

        const size_t pair_end = offset;
        if(equals == payload_size || equals == pair_start || equals + 1U >= pair_end) return false;

        const size_t key_size = equals - pair_start;
        bool matched = false;
        for(size_t i = 0U; i < field_count; i++) {
            if(strlen(keys[i]) == key_size && memcmp(&payload[pair_start], keys[i], key_size) == 0) {
                if(fields[i].present) return false;
                fields[i].value = &payload[equals + 1U];
                fields[i].size = pair_end - equals - 1U;
                fields[i].present = true;
                matched = true;
                break;
            }
        }
        if(!matched) return false;

        if(offset < payload_size) {
            offset++;
            if(offset == payload_size) return false;
        }
    }

    for(size_t i = 0U; i < field_count; i++) {
        if(!fields[i].present) return false;
    }
    return true;
}

static bool tumofabric_field_equals(const TumoFabricField* field, const char* text) {
    const size_t text_size = strlen(text);
    return field->size == text_size && memcmp(field->value, text, text_size) == 0;
}

static bool tumofabric_parse_hex32(const TumoFabricField* field, uint32_t* value) {
    if(field->size != 8U) return false;

    uint32_t parsed = 0U;
    for(size_t i = 0U; i < field->size; i++) {
        const uint8_t c = field->value[i];
        uint8_t nibble;
        if(c >= '0' && c <= '9') {
            nibble = c - '0';
        } else if(c >= 'A' && c <= 'F') {
            nibble = c - 'A' + 10U;
        } else if(c >= 'a' && c <= 'f') {
            nibble = c - 'a' + 10U;
        } else {
            return false;
        }
        parsed = (parsed << 4U) | nibble;
    }

    if(parsed == 0U) return false;
    *value = parsed;
    return true;
}

static bool tumofabric_parse_sequence(const TumoFabricField* field, uint32_t* value) {
    if(field->size == 0U || field->size > 10U) return false;

    uint32_t parsed = 0U;
    for(size_t i = 0U; i < field->size; i++) {
        const uint8_t c = field->value[i];
        if(c < '0' || c > '9') return false;
        const uint32_t digit = c - '0';
        if(parsed > (UINT32_MAX - digit) / 10U) return false;
        parsed = parsed * 10U + digit;
    }

    if(parsed == 0U) return false;
    *value = parsed;
    return true;
}

static bool tumofabric_parse_owner(const TumoFabricField* field, char* owner) {
    if(field->size == 0U || field->size > TUMOFABRIC_OWNER_MAX) return false;
    for(size_t i = 0U; i < field->size; i++) {
        const uint8_t c = field->value[i];
        const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_';
        if(!valid) return false;
    }
    memcpy(owner, field->value, field->size);
    owner[field->size] = '\0';
    return true;
}

static bool tumofabric_authorized(
    const TumoFabricState* state,
    uint32_t session_id,
    uint32_t token) {
    return state->active && state->session_id == session_id && state->token == token;
}

static bool tumofabric_format_state(
    const TumoFabricState* state,
    bool duplicate,
    char* response,
    size_t response_size) {
    const int written = snprintf(
        response,
        response_size,
        "schema=1;pkg=counter;sid=%08lX;token=%08lX;seq=%lu;value=%d;dup=%u;persist=ram",
        (unsigned long)state->session_id,
        (unsigned long)state->token,
        (unsigned long)state->last_sequence,
        state->counter,
        duplicate ? 1U : 0U);
    return written >= 0 && (size_t)written < response_size;
}

void tumofabric_init(TumoFabricState* state) {
    memset(state, 0, sizeof(TumoFabricState));
}

bool tumofabric_open_local(TumoFabricState* state) {
    if(state->active) return strcmp(state->owner, "flipper") == 0;

    tumofabric_init(state);
    state->active = true;
    state->session_id = 0xF17F0001U;
    state->token = 0xF17F0001U;
    memcpy(state->owner, "flipper", sizeof("flipper"));
    return true;
}

bool tumofabric_step_local(TumoFabricState* state, int8_t delta) {
    if(!state->active || strcmp(state->owner, "flipper") != 0 ||
       (delta != -1 && delta != 1)) {
        return false;
    }

    const int16_t next = state->counter + delta;
    if(next < TUMOFABRIC_COUNTER_MIN || next > TUMOFABRIC_COUNTER_MAX) return false;
    state->counter = next;
    state->last_sequence++;
    return true;
}

TumoFabricResult tumofabric_open(
    TumoFabricState* state,
    uint32_t request_id,
    const uint8_t* payload,
    size_t payload_size,
    char* response,
    size_t response_size) {
    static const char* const keys[] = {"owner", "pkg", "token"};
    TumoFabricField fields[3] = {0};
    char owner[TUMOFABRIC_OWNER_MAX + 1U];
    uint32_t token = 0U;

    if(!tumofabric_parse_fields(payload, payload_size, keys, fields, 3U) ||
       !tumofabric_parse_owner(&fields[0], owner) ||
       !tumofabric_field_equals(&fields[1], "counter") ||
       !tumofabric_parse_hex32(&fields[2], &token)) {
        return TumoFabricResultPayload;
    }

    if(state->active) {
        if(state->token != token || strcmp(state->owner, owner) != 0) {
            return TumoFabricResultBusy;
        }
    } else {
        tumofabric_init(state);
        state->active = true;
        state->session_id = request_id ? request_id : 1U;
        state->token = token;
        memcpy(state->owner, owner, strlen(owner) + 1U);
    }

    return tumofabric_format_state(state, false, response, response_size) ?
               TumoFabricResultOk :
               TumoFabricResultPayload;
}

TumoFabricResult tumofabric_get_state(
    const TumoFabricState* state,
    const uint8_t* payload,
    size_t payload_size,
    char* response,
    size_t response_size) {
    static const char* const keys[] = {"sid", "token"};
    TumoFabricField fields[2] = {0};
    uint32_t session_id = 0U;
    uint32_t token = 0U;

    if(!tumofabric_parse_fields(payload, payload_size, keys, fields, 2U) ||
       !tumofabric_parse_hex32(&fields[0], &session_id) ||
       !tumofabric_parse_hex32(&fields[1], &token)) {
        return TumoFabricResultPayload;
    }
    if(!tumofabric_authorized(state, session_id, token)) return TumoFabricResultSession;

    return tumofabric_format_state(state, false, response, response_size) ?
               TumoFabricResultOk :
               TumoFabricResultPayload;
}

TumoFabricResult tumofabric_step(
    TumoFabricState* state,
    const uint8_t* payload,
    size_t payload_size,
    char* response,
    size_t response_size) {
    static const char* const keys[] = {"sid", "token", "seq", "op"};
    TumoFabricField fields[4] = {0};
    uint32_t session_id = 0U;
    uint32_t token = 0U;
    uint32_t sequence = 0U;

    if(!tumofabric_parse_fields(payload, payload_size, keys, fields, 4U) ||
       !tumofabric_parse_hex32(&fields[0], &session_id) ||
       !tumofabric_parse_hex32(&fields[1], &token) ||
       !tumofabric_parse_sequence(&fields[2], &sequence)) {
        return TumoFabricResultPayload;
    }
    if(!tumofabric_authorized(state, session_id, token)) return TumoFabricResultSession;

    if(sequence == state->last_sequence) {
        return tumofabric_format_state(state, true, response, response_size) ?
                   TumoFabricResultOk :
                   TumoFabricResultPayload;
    }
    if(sequence != state->last_sequence + 1U) return TumoFabricResultSequence;

    int16_t next_value = state->counter;
    if(tumofabric_field_equals(&fields[3], "inc")) {
        if(next_value >= TUMOFABRIC_COUNTER_MAX) return TumoFabricResultRange;
        next_value++;
    } else if(tumofabric_field_equals(&fields[3], "dec")) {
        if(next_value <= TUMOFABRIC_COUNTER_MIN) return TumoFabricResultRange;
        next_value--;
    } else {
        return TumoFabricResultPayload;
    }

    state->counter = next_value;
    state->last_sequence = sequence;
    return tumofabric_format_state(state, false, response, response_size) ?
               TumoFabricResultOk :
               TumoFabricResultPayload;
}

TumoFabricResult tumofabric_cancel(
    TumoFabricState* state,
    const uint8_t* payload,
    size_t payload_size,
    char* response,
    size_t response_size) {
    static const char* const keys[] = {"sid", "token"};
    TumoFabricField fields[2] = {0};
    uint32_t session_id = 0U;
    uint32_t token = 0U;

    if(!tumofabric_parse_fields(payload, payload_size, keys, fields, 2U) ||
       !tumofabric_parse_hex32(&fields[0], &session_id) ||
       !tumofabric_parse_hex32(&fields[1], &token)) {
        return TumoFabricResultPayload;
    }
    if(!tumofabric_authorized(state, session_id, token)) return TumoFabricResultSession;

    tumofabric_init(state);
    const int written = snprintf(response, response_size, "schema=1;status=cancelled");
    return written >= 0 && (size_t)written < response_size ?
               TumoFabricResultOk :
               TumoFabricResultPayload;
}

const char* tumofabric_result_token(TumoFabricResult result) {
    switch(result) {
    case TumoFabricResultOk:
        return "ok";
    case TumoFabricResultPayload:
        return "payload";
    case TumoFabricResultBusy:
        return "busy";
    case TumoFabricResultSession:
        return "session";
    case TumoFabricResultSequence:
        return "seq";
    case TumoFabricResultRange:
        return "range";
    default:
        return "internal";
    }
}
