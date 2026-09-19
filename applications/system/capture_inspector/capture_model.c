#include "capture_model.h"
#include <string.h>

static char* ci_trim(char* value) {
    while(*value == ' ' || *value == '\t')
        value++;
    size_t length = strlen(value);
    while(length &&
          (value[length - 1] == ' ' || value[length - 1] == '\t' || value[length - 1] == '\r'))
        value[--length] = 0;
    return value;
}

static void ci_parse_line(CiSnapshot* snapshot) {
    snapshot->line[snapshot->line_used] = 0;
    snapshot->line_used = 0;
    char* key = ci_trim(snapshot->line);
    if(!*key || *key == '#') return;
    char* colon = strchr(key, ':');
    if(!colon) {
        snapshot->status = CiParseMalformed;
        return;
    }
    *colon = 0;
    char* value = ci_trim(colon + 1);
    key = ci_trim(key);
    if(!*key || !*value) {
        snapshot->status = CiParseMalformed;
        return;
    }
    if(strlen(key) >= CI_KEY_CAP || strlen(value) >= CI_VALUE_CAP ||
       snapshot->count >= CI_FIELD_CAP) {
        snapshot->status = CiParseLimit;
        return;
    }
    CiField* field = &snapshot->fields[snapshot->count++];
    strcpy(field->key, key);
    strcpy(field->value, value);
}

void ci_snapshot_reset(CiSnapshot* snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
}

CiParseStatus ci_snapshot_feed(CiSnapshot* snapshot, const void* bytes, size_t size) {
    if(snapshot->status != CiParseOk || snapshot->finished) return snapshot->status;
    if(size > CI_FILE_CAP - snapshot->bytes) return snapshot->status = CiParseLimit;
    if(!bytes && size) return snapshot->status = CiParseMalformed;
    snapshot->bytes += size;
    const uint8_t* input = bytes;
    for(size_t i = 0; i < size && snapshot->status == CiParseOk; i++) {
        const uint8_t ch = input[i];
        if(!ch || (ch < 32 && ch != '\n' && ch != '\r' && ch != '\t') || ch == 127) {
            snapshot->status = CiParseMalformed;
        } else if(ch == '\n') {
            ci_parse_line(snapshot);
        } else if(snapshot->line_used + 1 >= CI_LINE_CAP) {
            snapshot->status = CiParseLimit;
        } else {
            snapshot->line[snapshot->line_used++] = ch;
        }
    }
    return snapshot->status;
}

const CiField* ci_snapshot_find(const CiSnapshot* snapshot, const char* key, size_t occurrence) {
    for(size_t i = 0; i < snapshot->count; i++) {
        if(strcmp(snapshot->fields[i].key, key) == 0) {
            if(!occurrence) return &snapshot->fields[i];
            occurrence--;
        }
    }
    return NULL;
}

static bool ci_positive_u32(const char* value) {
    uint32_t result = 0;
    for(; *value; value++) {
        if(*value < '0' || *value > '9') return false;
        uint32_t digit = (uint32_t)(*value - '0');
        if(result > (UINT32_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
    }
    return result != 0;
}

CiParseStatus ci_snapshot_finish(CiSnapshot* snapshot) {
    if(snapshot->finished) return snapshot->status;
    snapshot->finished = true;
    if(snapshot->status != CiParseOk) return snapshot->status;
    if(snapshot->line_used) ci_parse_line(snapshot);
    if(snapshot->status != CiParseOk) return snapshot->status;
    const char* unique[] = {
        "Filetype", "Version", "Frequency", "Preset", "Protocol", "Key", "Bit"};
    for(size_t i = 0; i < sizeof(unique) / sizeof(unique[0]); i++) {
        if(ci_snapshot_find(snapshot, unique[i], 1))
            return snapshot->status = CiParseDuplicateHeader;
    }
    const CiField* type = ci_snapshot_find(snapshot, "Filetype", 0);
    const CiField* version = ci_snapshot_find(snapshot, "Version", 0);
    if(!type || !version || strcmp(type->value, "Flipper SubGhz Key File") ||
       strcmp(version->value, "1"))
        return snapshot->status = CiParseUnsupported;
    const CiField* frequency = ci_snapshot_find(snapshot, "Frequency", 0);
    if(!frequency || !ci_positive_u32(frequency->value) ||
       !ci_snapshot_find(snapshot, "Preset", 0) || !ci_snapshot_find(snapshot, "Protocol", 0) ||
       !ci_snapshot_find(snapshot, "Key", 0))
        return snapshot->status = CiParseMissingField;
    return snapshot->status;
}

const char* ci_parse_status_text(CiParseStatus status) {
    switch(status) {
    case CiParseOk:
        return "Fields loaded";
    case CiParseMalformed:
        return "Malformed text";
    case CiParseLimit:
        return "Capture limit exceeded";
    case CiParseUnsupported:
        return "Unsupported header";
    case CiParseMissingField:
        return "Incomplete metadata";
    case CiParseDuplicateHeader:
        return "Duplicate required field";
    default:
        return "Unknown parse error";
    }
}

static size_t ci_occurrence(const CiSnapshot* snapshot, size_t index) {
    size_t count = 0;
    for(size_t i = 0; i < index; i++)
        if(!strcmp(snapshot->fields[i].key, snapshot->fields[index].key)) count++;
    return count;
}

static bool ci_ready(const CiSnapshot* a, const CiSnapshot* b) {
    return a->finished && b->finished && a->status == CiParseOk && b->status == CiParseOk;
}

size_t ci_diff_count(const CiSnapshot* a, const CiSnapshot* b) {
    if(!ci_ready(a, b)) return 0;
    size_t count = a->count;
    for(size_t i = 0; i < b->count; i++)
        if(!ci_snapshot_find(a, b->fields[i].key, ci_occurrence(b, i))) count++;
    return count;
}

bool ci_diff_row(const CiSnapshot* a, const CiSnapshot* b, size_t index, CiDiffRow* row) {
    if(!ci_ready(a, b)) return false;
    memset(row, 0, sizeof(*row));
    if(index < a->count) {
        row->a = &a->fields[index];
        row->key = row->a->key;
        row->occurrence = ci_occurrence(a, index);
        row->b = ci_snapshot_find(b, row->key, row->occurrence);
        row->kind = !row->b ? CiDiffOnlyA :
                              (strcmp(row->a->value, row->b->value) ? CiDiffChanged : CiDiffSame);
        return true;
    }
    index -= a->count;
    for(size_t i = 0; i < b->count; i++) {
        const size_t occurrence = ci_occurrence(b, i);
        if(ci_snapshot_find(a, b->fields[i].key, occurrence)) continue;
        if(index--) continue;
        row->b = &b->fields[i];
        row->key = row->b->key;
        row->occurrence = occurrence;
        row->kind = CiDiffOnlyB;
        return true;
    }
    return false;
}
