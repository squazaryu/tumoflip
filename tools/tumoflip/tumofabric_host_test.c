#include "tumofabric_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static TumoFabricResult call_open(
    TumoFabricState* state,
    uint32_t request_id,
    const char* payload,
    char* response,
    size_t response_size) {
    return tumofabric_open(
        state,
        request_id,
        (const uint8_t*)payload,
        strlen(payload),
        response,
        response_size);
}

static TumoFabricResult call_state(
    const TumoFabricState* state,
    const char* payload,
    char* response,
    size_t response_size) {
    return tumofabric_get_state(
        state,
        (const uint8_t*)payload,
        strlen(payload),
        response,
        response_size);
}

static TumoFabricResult call_step(
    TumoFabricState* state,
    const char* payload,
    char* response,
    size_t response_size) {
    return tumofabric_step(
        state,
        (const uint8_t*)payload,
        strlen(payload),
        response,
        response_size);
}

static TumoFabricResult call_cancel(
    TumoFabricState* state,
    const char* payload,
    char* response,
    size_t response_size) {
    return tumofabric_cancel(
        state,
        (const uint8_t*)payload,
        strlen(payload),
        response,
        response_size);
}

int main(void) {
    TumoFabricState state;
    char response[160];
    tumofabric_init(&state);

    assert(call_open(
               &state,
               0x1234ABCDU,
               "owner=iphone;pkg=counter;token=89ABCDEF",
               response,
               sizeof(response)) == TumoFabricResultOk);
    assert(state.active);
    assert(state.session_id == 0x1234ABCDU);
    assert(strcmp(state.owner, "iphone") == 0);
    assert(strstr(response, "value=0;dup=0") != NULL);

    assert(call_step(
               &state,
               "sid=1234ABCD;token=89ABCDEF;seq=1;op=inc",
               response,
               sizeof(response)) == TumoFabricResultOk);
    assert(state.counter == 1);
    assert(state.last_sequence == 1U);
    assert(strstr(response, "seq=1;value=1;dup=0") != NULL);

    assert(call_step(
               &state,
               "sid=1234ABCD;token=89ABCDEF;seq=1;op=inc",
               response,
               sizeof(response)) == TumoFabricResultOk);
    assert(state.counter == 1);
    assert(strstr(response, "seq=1;value=1;dup=1") != NULL);

    assert(call_step(
               &state,
               "sid=1234ABCD;token=89ABCDEF;seq=1;op=dec",
               response,
               sizeof(response)) == TumoFabricResultSequence);
    assert(state.counter == 1);

    assert(call_step(
               &state,
               "sid=1234ABCD;token=89ABCDEF;seq=3;op=inc",
               response,
               sizeof(response)) == TumoFabricResultSequence);
    assert(state.counter == 1);

    assert(call_open(
               &state,
               0xDEADBEEFU,
               "owner=iphone;pkg=counter;token=89ABCDEF",
               response,
               sizeof(response)) == TumoFabricResultOk);
    assert(state.session_id == 0x1234ABCDU);
    assert(state.counter == 1);

    assert(call_open(
               &state,
               2U,
               "owner=mac;pkg=counter;token=89ABCDEF",
               response,
               sizeof(response)) == TumoFabricResultBusy);
    assert(call_state(
               &state,
               "sid=1234ABCD;token=00000001",
               response,
               sizeof(response)) == TumoFabricResultSession);

    assert(call_cancel(
               &state,
               "sid=1234ABCD;token=89ABCDEF",
               response,
               sizeof(response)) == TumoFabricResultOk);
    assert(!state.active);
    assert(strcmp(response, "schema=1;status=cancelled") == 0);

    assert(tumofabric_open_local(&state));
    assert(strcmp(state.owner, "flipper") == 0);
    assert(tumofabric_step_local(&state, 1));
    assert(state.counter == 1);
    assert(state.last_sequence == 1U);
    assert(!tumofabric_step_local(&state, 2));

    assert(call_open(
               &state,
               0x01020304U,
               "owner=iphone;pkg=counter;token=A1B2C3D4",
               response,
               sizeof(response)) == TumoFabricResultOk);
    assert(state.session_id == 0x01020304U);
    assert(state.counter == 1);
    assert(strcmp(state.owner, "iphone") == 0);

    assert(tumofabric_step_local(&state, 1));
    assert(state.counter == 2);
    assert(state.last_sequence == 1U);

    assert(call_step(
               &state,
               "sid=01020304;token=A1B2C3D4;seq=2;op=inc",
               response,
               sizeof(response)) == TumoFabricResultOk);
    assert(state.counter == 3);
    assert(strstr(response, "seq=2;value=3;dup=0") != NULL);
    tumofabric_init(&state);

    assert(call_open(
               &state,
               1U,
        "owner=iphone;pkg=counter;token=89ABCDEF;extra=x",
               response,
               sizeof(response)) == TumoFabricResultPayload);
    assert(call_open(
               &state,
               1U,
               "owner=iphone;owner=mac;pkg=counter;token=89ABCDEF",
               response,
               sizeof(response)) == TumoFabricResultPayload);

    puts("tumofabric_host_test: PASS");
    return 0;
}
