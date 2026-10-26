#include "field_round.h"
#include <stdio.h>
#include <string.h>

#define FIELD_ROUNDS LIBRARY_ROOT "/round"
#define FIELD_ROUND_MAGIC 0x31524C46UL

const char* field_round_result_name(FieldRoundResult result) {
    static const char* const names[] = {"PENDING", "PASS", "PROBLEM", "SKIPPED"};
    return result <= FieldRoundSkipped ? names[result] : "INVALID";
}
static bool field_round_valid(const FieldRound* round) {
    if(!round || !round->started || !round->count || round->count > LIBRARY_PINS) return false;
    for(unsigned i = 0; i < round->count; i++) {
        const FieldRoundItem* item = &round->items[i];
        if(!library_name_valid(item->object) || !field_text_valid(item->note, sizeof(item->note)) ||
           item->result > FieldRoundSkipped ||
           (item->result != FieldRoundPending && item->timestamp < round->started) ||
           (item->result == FieldRoundPending && (item->timestamp || item->note[0]))) return false;
        for(unsigned j = 0; j < i; j++)
            if(!strcmp(item->object, round->items[j].object)) return false;
    }
    return true;
}
bool field_round_begin(FieldRound* round, const LibraryPins* pins, uint32_t timestamp) {
    if(!round || !library_pins_valid(pins) || !pins->count || !timestamp) return false;
    memset(round, 0, sizeof(*round));
    round->started = timestamp;
    round->count = pins->count;
    for(unsigned i = 0; i < pins->count; i++)
        memcpy(round->items[i].object, pins->names[i], sizeof(round->items[i].object));
    return true;
}
bool field_round_mark(FieldRound* round, unsigned index, FieldRoundResult result,
                       const char* note, uint32_t timestamp) {
    if(!field_round_valid(round) || index >= round->count || result < FieldRoundPassed ||
       result > FieldRoundSkipped || timestamp < round->started ||
       !field_text_valid(note, sizeof(round->items[0].note))) return false;
    FieldRoundItem* item = &round->items[index];
    item->result = result;
    item->timestamp = timestamp;
    memset(item->note, 0, sizeof(item->note));
    memcpy(item->note, note, strlen(note));
    return true;
}
bool field_round_complete(const FieldRound* round) {
    if(!field_round_valid(round)) return false;
    for(unsigned i = 0; i < round->count; i++)
        if(round->items[i].result == FieldRoundPending) return false;
    return true;
}
bool field_round_load(Storage* storage, FieldRound* round) {
    return field_record_load(storage, FIELD_ROUNDS, FIELD_ROUND_MAGIC, round, sizeof(*round)) &&
           field_round_valid(round);
}
bool field_round_save(Storage* storage, const FieldRound* round) {
    return field_round_valid(round) &&
           field_record_save(storage, FIELD_ROUNDS, FIELD_ROUND_MAGIC, round, sizeof(*round));
}
bool field_round_export(Storage* storage, const FieldRound* round, char* path, size_t size) {
    if(!storage || !field_round_valid(round) || !path ||
       !storage_simply_mkdir(storage, LIBRARY_ROOT "/reports")) return false;
    int length = snprintf(path, size, LIBRARY_ROOT "/reports/round-%lu.txt", (unsigned long)round->started);
    if(length < 0 || (size_t)length >= size) return false;
    File* file = storage_file_alloc(storage);
    bool created = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_NEW);
    bool ok = created;
    char line[192];
    int n = snprintf(line, sizeof(line), "Field round\nStarted UTC epoch: %lu\nExplicit user results; opening a file is not a test.\nObject\tResult\tUTC epoch\tNote\n", (unsigned long)round->started);
    if(ok) ok = n > 0 && (size_t)n < sizeof(line) && storage_file_write(file, line, n) == (size_t)n;
    for(unsigned i = 0; ok && i < round->count; i++) {
        const FieldRoundItem* item = &round->items[i];
        n = snprintf(line, sizeof(line), "%s\t%s\t%lu\t%s\n", item->object,
                     field_round_result_name(item->result), (unsigned long)item->timestamp, item->note);
        ok = n > 0 && (size_t)n < sizeof(line) && storage_file_write(file, line, n) == (size_t)n;
    }
    if(ok) ok = storage_file_sync(file);
    if(created) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    if(!ok && created) storage_common_remove(storage, path);
    return ok;
}
