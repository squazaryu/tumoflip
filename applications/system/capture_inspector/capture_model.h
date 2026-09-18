#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CI_FIELD_CAP 24U
#define CI_KEY_CAP   32U
#define CI_VALUE_CAP 160U
#define CI_LINE_CAP  (CI_KEY_CAP + CI_VALUE_CAP + 4U)
#define CI_FILE_CAP  (64U * 1024U)

typedef enum {
    CiParseOk,
    CiParseMalformed,
    CiParseLimit,
    CiParseUnsupported,
    CiParseMissingField,
    CiParseDuplicateHeader,
} CiParseStatus;

typedef struct {
    char key[CI_KEY_CAP];
    char value[CI_VALUE_CAP];
} CiField;

typedef struct {
    CiField fields[CI_FIELD_CAP];
    char line[CI_LINE_CAP];
    size_t count;
    size_t line_used;
    uint32_t bytes;
    CiParseStatus status;
    bool finished;
} CiSnapshot;

typedef enum {
    CiDiffSame,
    CiDiffChanged,
    CiDiffOnlyA,
    CiDiffOnlyB
} CiDiffKind;
typedef struct {
    const char* key;
    const CiField* a;
    const CiField* b;
    size_t occurrence;
    CiDiffKind kind;
} CiDiffRow;

void ci_snapshot_reset(CiSnapshot* snapshot);
CiParseStatus ci_snapshot_feed(CiSnapshot* snapshot, const void* bytes, size_t size);
CiParseStatus ci_snapshot_finish(CiSnapshot* snapshot);
const CiField* ci_snapshot_find(const CiSnapshot* snapshot, const char* key, size_t occurrence);
const char* ci_parse_status_text(CiParseStatus status);
size_t ci_diff_count(const CiSnapshot* a, const CiSnapshot* b);
bool ci_diff_row(const CiSnapshot* a, const CiSnapshot* b, size_t index, CiDiffRow* row);
