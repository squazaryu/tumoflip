#include "tumocard_router.h"

#include <string.h>

#define TUMOCARD_SW_SUCCESS          0x9000U
#define TUMOCARD_SW_WRONG_LENGTH     0x6700U
#define TUMOCARD_SW_NOT_SELECTED     0x6985U
#define TUMOCARD_SW_APPLET_NOT_FOUND 0x6A82U

static size_t tumocard_write_status(uint8_t* output, size_t output_capacity, uint16_t status) {
    if(!output || output_capacity < 2U) return 0U;
    output[0] = (uint8_t)(status >> 8U);
    output[1] = (uint8_t)status;
    return 2U;
}

static uint16_t tumocard_response_status(const uint8_t* response, size_t response_size) {
    if(response_size < 2U) return 0U;
    return ((uint16_t)response[response_size - 2U] << 8U) | response[response_size - 1U];
}

static bool tumocard_valid_id(const char* id) {
    if(!id || id[0] == '\0') return false;
    const size_t length = strlen(id);
    if(length >= TUMOCARD_APPLET_ID_MAX) return false;
    for(size_t i = 0; i < length; i++) {
        const char c = id[i];
        if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return true;
}

void tumocard_registry_init(TumoCardRegistry* registry) {
    if(registry) memset(registry, 0, sizeof(*registry));
}

TumoCardAddResult tumocard_registry_add(
    TumoCardRegistry* registry,
    const char* id,
    bool enabled,
    const TumoVmProgram* program,
    const uint8_t state[TUMOVM_STATE_MAX]) {
    if(!registry || !state || !tumocard_valid_id(id) || !tumovm_program_validate(program))
        return TumoCardAddResultInvalid;
    if(registry->applet_count >= TUMOCARD_APPLET_MAX) return TumoCardAddResultFull;

    for(size_t i = 0; i < registry->applet_count; i++) {
        const TumoCardApplet* applet = &registry->applets[i];
        if(strcmp(applet->id, id) == 0) return TumoCardAddResultDuplicateId;
        if(applet->vm.program.aid_size == program->aid_size &&
           memcmp(applet->vm.program.aid, program->aid, program->aid_size) == 0)
            return TumoCardAddResultDuplicateAid;
    }

    TumoCardApplet* applet = &registry->applets[registry->applet_count++];
    memset(applet, 0, sizeof(*applet));
    strlcpy(applet->id, id, sizeof(applet->id));
    applet->enabled = enabled;
    tumovm_init(&applet->vm, program, state);
    return TumoCardAddResultOk;
}

bool tumocard_registry_set_enabled(TumoCardRegistry* registry, size_t index, bool enabled) {
    if(!registry || index >= registry->applet_count) return false;
    registry->applets[index].enabled = enabled;
    return true;
}

void tumocard_session_reset(TumoCardSession* session) {
    if(!session) return;
    session->selected_index = -1;
    tumovm_session_reset(&session->vm_session);
}

static int8_t tumocard_find_applet(const TumoCardRegistry* registry, const TumoVmApdu* apdu) {
    for(size_t i = 0; i < registry->applet_count; i++) {
        const TumoCardApplet* applet = &registry->applets[i];
        if(!applet->enabled) continue;
        if(applet->vm.program.aid_size == apdu->data_size &&
           memcmp(applet->vm.program.aid, apdu->data, apdu->data_size) == 0)
            return (int8_t)i;
    }
    return -1;
}

size_t tumocard_process_apdu(
    TumoCardRegistry* registry,
    TumoCardSession* session,
    TumoVmTransport transport,
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t output_capacity) {
    if(!registry || !session || !output || output_capacity < 2U) return 0U;

    TumoVmApdu apdu;
    if(!tumovm_apdu_parse(input, input_size, &apdu)) {
        tumocard_session_reset(session);
        return tumocard_write_status(output, output_capacity, TUMOCARD_SW_WRONG_LENGTH);
    }

    if(apdu.cla == 0x00U && apdu.ins == 0xA4U && apdu.p1 == 0x04U) {
        const int8_t index = tumocard_find_applet(registry, &apdu);
        tumocard_session_reset(session);
        if(index < 0)
            return tumocard_write_status(output, output_capacity, TUMOCARD_SW_APPLET_NOT_FOUND);

        TumoCardApplet* applet = &registry->applets[index];
        const size_t response_size = tumovm_process_apdu(
            &applet->vm,
            &session->vm_session,
            transport,
            input,
            input_size,
            output,
            output_capacity);
        if(tumocard_response_status(output, response_size) == TUMOCARD_SW_SUCCESS) {
            session->selected_index = index;
        } else {
            tumocard_session_reset(session);
        }
        return response_size;
    }

    if(session->selected_index < 0 || (size_t)session->selected_index >= registry->applet_count)
        return tumocard_write_status(output, output_capacity, TUMOCARD_SW_NOT_SELECTED);

    TumoCardApplet* applet = &registry->applets[session->selected_index];
    if(!applet->enabled) {
        tumocard_session_reset(session);
        return tumocard_write_status(output, output_capacity, TUMOCARD_SW_NOT_SELECTED);
    }

    return tumovm_process_apdu(
        &applet->vm, &session->vm_session, transport, input, input_size, output, output_capacity);
}
