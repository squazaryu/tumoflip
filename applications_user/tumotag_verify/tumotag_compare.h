#pragma once

#include <furi.h>
#include <ibutton/ibutton_key.h>
#include <ibutton/ibutton_protocols.h>
#include <nfc/nfc_device.h>
#include <toolbox/protocols/protocol_dict.h>

typedef enum {
    TumoTagMediumNfc,
    TumoTagMediumLfRfid,
    TumoTagMediumIButton,
} TumoTagMedium;

typedef enum {
    TumoTagVerdictVerified,
    TumoTagVerdictPartial,
    TumoTagVerdictDifferent,
    TumoTagVerdictUnsupported,
} TumoTagVerdict;

typedef struct {
    TumoTagMedium medium;
    TumoTagVerdict verdict;
    char expected_protocol[32];
    char observed_protocol[32];
    char expected_id[48];
    char observed_id[48];
    char detail[96];
    char expected_path[256];
    char match_path[256];
    uint16_t compared_units;
    uint16_t missing_units;
    uint16_t mismatched_units;
    uint16_t duplicate_matches;
    bool search_truncated;
} TumoTagResult;

void tumotag_result_reset(TumoTagResult* result, TumoTagMedium medium);
const char* tumotag_medium_name(TumoTagMedium medium);
const char* tumotag_verdict_name(TumoTagVerdict verdict);

bool tumotag_compare_nfc_files(
    const char* expected_path,
    const char* observed_path,
    TumoTagResult* result);

bool tumotag_compare_lfrfid(
    ProtocolDict* expected,
    ProtocolId expected_protocol,
    ProtocolDict* observed,
    ProtocolId observed_protocol,
    TumoTagResult* result);

bool tumotag_compare_ibutton(
    iButtonProtocols* protocols,
    const iButtonKey* expected,
    const iButtonKey* observed,
    TumoTagResult* result);
