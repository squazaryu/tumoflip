#include "tumovm_core.h"

#include <string.h>

#define SW_SUCCESS                       0x9000U
#define SW_WRONG_LENGTH                  0x6700U
#define SW_SECURITY_STATUS_NOT_SATISFIED 0x6985U
#define SW_FILE_NOT_FOUND                0x6A82U
#define SW_WRONG_PARAMETERS              0x6A86U
#define SW_WRONG_OFFSET                  0x6B00U
#define SW_INS_NOT_SUPPORTED             0x6D00U
#define SW_INTERNAL_ERROR                0x6F00U

static bool tumovm_append_status(
    uint8_t* output,
    size_t output_capacity,
    size_t* output_size,
    uint16_t status) {
    if(*output_size + 2U > output_capacity) return false;
    output[(*output_size)++] = (uint8_t)(status >> 8U);
    output[(*output_size)++] = (uint8_t)status;
    return true;
}

static size_t tumovm_fail(
    TumoVm* vm,
    TumoVmTransport transport,
    uint8_t ins,
    uint16_t status,
    uint8_t* output,
    size_t output_capacity) {
    size_t output_size = 0;
    if(!tumovm_append_status(output, output_capacity, &output_size, status)) return 0;
    vm->apdu_count++;
    vm->last_transport = transport;
    vm->last_ins = ins;
    vm->last_status = status;
    return output_size;
}

void tumovm_init(TumoVm* vm, const TumoVmProgram* program, const uint8_t* state) {
    memset(vm, 0, sizeof(*vm));
    memcpy(&vm->program, program, sizeof(*program));
    memcpy(vm->state, state, program->state_size);
    vm->last_status = SW_SUCCESS;
}

void tumovm_session_reset(TumoVmSession* session) {
    session->selected = false;
}

static bool tumovm_validate_entry(const TumoVmProgram* program, uint8_t entry) {
    size_t pc = entry;
    size_t instruction_count = 0;

    while(pc < program->bytecode_size && instruction_count++ < TUMOVM_INSTRUCTION_MAX) {
        const uint8_t opcode = program->bytecode[pc++];
        switch(opcode) {
        case TumoVmOpcodeRequireSelected:
        case TumoVmOpcodeCheckDataAid:
        case TumoVmOpcodeSetSelected:
        case TumoVmOpcodeReadState:
        case TumoVmOpcodeWriteState:
            break;
        case TumoVmOpcodeCheckP1:
            if(pc + 1U > program->bytecode_size) return false;
            pc += 1U;
            break;
        case TumoVmOpcodeReturnStatus:
            if(pc + 2U > program->bytecode_size) return false;
            return true;
        default:
            return false;
        }
    }

    return false;
}

bool tumovm_program_validate(const TumoVmProgram* program) {
    if(!program) return false;
    if(program->name[0] == '\0') return false;
    if(program->state_size == 0U || program->state_size > TUMOVM_STATE_MAX) return false;
    if(program->aid_size == 0U || program->aid_size > TUMOVM_AID_MAX) return false;
    if(program->route_count == 0U || program->route_count > TUMOVM_ROUTES_MAX) return false;
    if(program->bytecode_size == 0U || program->bytecode_size > TUMOVM_BYTECODE_MAX) return false;
    if(!program->capability_nfc_type4 || !program->capability_usb_ccid) return false;

    for(size_t i = 0; i < program->route_count; i++) {
        const TumoVmRoute* route = &program->routes[i];
        if(route->entry >= program->bytecode_size) return false;
        if(!tumovm_validate_entry(program, route->entry)) return false;
        for(size_t j = i + 1U; j < program->route_count; j++) {
            if(route->cla == program->routes[j].cla && route->ins == program->routes[j].ins)
                return false;
        }
    }

    return true;
}

bool tumovm_apdu_parse(const uint8_t* input, size_t input_size, TumoVmApdu* apdu) {
    if(!input || !apdu || input_size < 4U) return false;

    memset(apdu, 0, sizeof(*apdu));
    apdu->cla = input[0];
    apdu->ins = input[1];
    apdu->p1 = input[2];
    apdu->p2 = input[3];

    if(input_size == 4U) return true;
    if(input_size == 5U) {
        apdu->has_le = true;
        apdu->le = input[4] == 0U ? 256U : input[4];
        return true;
    }

    const size_t lc = input[4];
    if(lc == 0U || lc > TUMOVM_APDU_DATA_MAX) return false;
    if(input_size != 5U + lc && input_size != 6U + lc) return false;

    apdu->data = &input[5];
    apdu->data_size = lc;
    if(input_size == 6U + lc) {
        apdu->has_le = true;
        apdu->le = input[5U + lc] == 0U ? 256U : input[5U + lc];
    }

    return true;
}

static const TumoVmRoute* tumovm_find_route(const TumoVmProgram* program, const TumoVmApdu* apdu) {
    for(size_t i = 0; i < program->route_count; i++) {
        if(program->routes[i].cla == apdu->cla && program->routes[i].ins == apdu->ins)
            return &program->routes[i];
    }
    return NULL;
}

size_t tumovm_process_apdu(
    TumoVm* vm,
    TumoVmSession* session,
    TumoVmTransport transport,
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t output_capacity) {
    if(!vm || !session || !output || output_capacity < 2U) return 0;

    TumoVmApdu apdu;
    if(!tumovm_apdu_parse(input, input_size, &apdu)) {
        const uint8_t ins = input_size > 1U ? input[1] : 0U;
        return tumovm_fail(vm, transport, ins, SW_WRONG_LENGTH, output, output_capacity);
    }

    const TumoVmRoute* route = tumovm_find_route(&vm->program, &apdu);
    if(!route)
        return tumovm_fail(vm, transport, apdu.ins, SW_INS_NOT_SUPPORTED, output, output_capacity);

    size_t pc = route->entry;
    size_t output_size = 0;
    size_t instruction_count = 0;
    bool state_changed = false;

    while(pc < vm->program.bytecode_size && instruction_count++ < TUMOVM_INSTRUCTION_MAX) {
        const uint8_t opcode = vm->program.bytecode[pc++];
        switch(opcode) {
        case TumoVmOpcodeRequireSelected:
            if(!session->selected)
                return tumovm_fail(
                    vm,
                    transport,
                    apdu.ins,
                    SW_SECURITY_STATUS_NOT_SATISFIED,
                    output,
                    output_capacity);
            break;

        case TumoVmOpcodeCheckP1:
            if(pc >= vm->program.bytecode_size)
                return tumovm_fail(
                    vm, transport, apdu.ins, SW_INTERNAL_ERROR, output, output_capacity);
            if(apdu.p1 != vm->program.bytecode[pc++])
                return tumovm_fail(
                    vm, transport, apdu.ins, SW_WRONG_PARAMETERS, output, output_capacity);
            break;

        case TumoVmOpcodeCheckDataAid:
            if(apdu.data_size != vm->program.aid_size ||
               memcmp(apdu.data, vm->program.aid, vm->program.aid_size) != 0) {
                return tumovm_fail(
                    vm, transport, apdu.ins, SW_FILE_NOT_FOUND, output, output_capacity);
            }
            break;

        case TumoVmOpcodeSetSelected:
            session->selected = true;
            break;

        case TumoVmOpcodeReadState: {
            if(!apdu.has_le)
                return tumovm_fail(
                    vm, transport, apdu.ins, SW_WRONG_LENGTH, output, output_capacity);
            const size_t offset = ((size_t)apdu.p1 << 8U) | apdu.p2;
            if(offset >= vm->program.state_size)
                return tumovm_fail(
                    vm, transport, apdu.ins, SW_WRONG_OFFSET, output, output_capacity);
            const size_t read_size = apdu.le < vm->program.state_size - offset ?
                                         apdu.le :
                                         vm->program.state_size - offset;
            if(read_size + 2U > output_capacity)
                return tumovm_fail(
                    vm, transport, apdu.ins, SW_WRONG_LENGTH, output, output_capacity);
            memcpy(output, &vm->state[offset], read_size);
            output_size = read_size;
            break;
        }

        case TumoVmOpcodeWriteState: {
            const size_t offset = ((size_t)apdu.p1 << 8U) | apdu.p2;
            if(apdu.data_size == 0U || offset > vm->program.state_size ||
               apdu.data_size > vm->program.state_size - offset) {
                return tumovm_fail(
                    vm, transport, apdu.ins, SW_WRONG_OFFSET, output, output_capacity);
            }
            memcpy(&vm->state[offset], apdu.data, apdu.data_size);
            state_changed = true;
            break;
        }

        case TumoVmOpcodeReturnStatus: {
            if(pc + 2U > vm->program.bytecode_size)
                return tumovm_fail(
                    vm, transport, apdu.ins, SW_INTERNAL_ERROR, output, output_capacity);
            const uint16_t status = ((uint16_t)vm->program.bytecode[pc] << 8U) |
                                    vm->program.bytecode[pc + 1U];
            if(!tumovm_append_status(output, output_capacity, &output_size, status)) return 0;

            vm->apdu_count++;
            vm->last_transport = transport;
            vm->last_ins = apdu.ins;
            vm->last_status = status;
            if(state_changed) {
                vm->dirty = true;
                vm->write_count++;
            }
            return output_size;
        }

        default:
            return tumovm_fail(
                vm, transport, apdu.ins, SW_INTERNAL_ERROR, output, output_capacity);
        }
    }

    return tumovm_fail(vm, transport, apdu.ins, SW_INTERNAL_ERROR, output, output_capacity);
}

const char* tumovm_transport_name(TumoVmTransport transport) {
    switch(transport) {
    case TumoVmTransportNfc:
        return "NFC";
    case TumoVmTransportUsb:
        return "USB";
    default:
        return "--";
    }
}
