#pragma once
#include "field_io.h"

typedef enum {
    FieldRoundPending,
    FieldRoundPassed,
    FieldRoundProblem,
    FieldRoundSkipped,
} FieldRoundResult;
typedef struct {
    char object[32], note[64];
    uint32_t result, timestamp;
} FieldRoundItem;
typedef struct {
    uint32_t started, count;
    FieldRoundItem items[LIBRARY_PINS];
} FieldRound;
bool field_round_begin(FieldRound* round, const LibraryPins* pins, uint32_t timestamp);
bool field_round_mark(FieldRound* round, unsigned index, FieldRoundResult result,
                       const char* note, uint32_t timestamp);
bool field_round_complete(const FieldRound* round);
bool field_round_load(Storage* storage, FieldRound* round);
bool field_round_save(Storage* storage, const FieldRound* round);
bool field_round_export(Storage* storage, const FieldRound* round, char* path, size_t size);
const char* field_round_result_name(FieldRoundResult result);
