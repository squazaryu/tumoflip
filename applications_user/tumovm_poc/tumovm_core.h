#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TUMOVM_NAME_MAX        24U
#define TUMOVM_AID_MAX         16U
#define TUMOVM_STATE_MAX       64U
#define TUMOVM_ROUTES_MAX      8U
#define TUMOVM_BYTECODE_MAX    128U
#define TUMOVM_APDU_DATA_MAX   255U
#define TUMOVM_RESPONSE_MAX    (TUMOVM_STATE_MAX + 2U)
#define TUMOVM_INSTRUCTION_MAX 32U

typedef enum {
    TumoVmTransportNone,
    TumoVmTransportNfc,
    TumoVmTransportUsb,
} TumoVmTransport;

typedef enum {
    TumoVmOpcodeRequireSelected = 0x01,
    TumoVmOpcodeCheckP1 = 0x02,
    TumoVmOpcodeCheckDataAid = 0x03,
    TumoVmOpcodeSetSelected = 0x04,
    TumoVmOpcodeReturnStatus = 0x05,
    TumoVmOpcodeReadState = 0x06,
    TumoVmOpcodeWriteState = 0x07,
} TumoVmOpcode;

typedef struct {
    uint8_t cla;
    uint8_t ins;
    uint8_t entry;
} TumoVmRoute;

typedef struct {
    char name[TUMOVM_NAME_MAX];
    uint8_t state_size;
    uint8_t aid_size;
    uint8_t aid[TUMOVM_AID_MAX];
    uint8_t route_count;
    TumoVmRoute routes[TUMOVM_ROUTES_MAX];
    uint8_t bytecode_size;
    uint8_t bytecode[TUMOVM_BYTECODE_MAX];
    bool capability_nfc_type4;
    bool capability_usb_ccid;
} TumoVmProgram;

typedef struct {
    bool selected;
} TumoVmSession;

typedef struct {
    TumoVmProgram program;
    uint8_t state[TUMOVM_STATE_MAX];
    bool dirty;
    uint32_t apdu_count;
    uint32_t write_count;
    TumoVmTransport last_transport;
    uint16_t last_status;
    uint8_t last_ins;
} TumoVm;

typedef struct {
    uint8_t cla;
    uint8_t ins;
    uint8_t p1;
    uint8_t p2;
    const uint8_t* data;
    size_t data_size;
    size_t le;
    bool has_le;
} TumoVmApdu;

void tumovm_init(TumoVm* vm, const TumoVmProgram* program, const uint8_t* state);
void tumovm_session_reset(TumoVmSession* session);

bool tumovm_program_validate(const TumoVmProgram* program);
bool tumovm_apdu_parse(const uint8_t* input, size_t input_size, TumoVmApdu* apdu);

size_t tumovm_process_apdu(
    TumoVm* vm,
    TumoVmSession* session,
    TumoVmTransport transport,
    const uint8_t* input,
    size_t input_size,
    uint8_t* output,
    size_t output_capacity);

const char* tumovm_transport_name(TumoVmTransport transport);
