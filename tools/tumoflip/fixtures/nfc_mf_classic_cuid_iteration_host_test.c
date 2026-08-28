#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CuidKeyOutcomeKnown,
    CuidKeyOutcomeDictionaryExhausted,
    CuidKeyOutcomeAuthFailed,
    CuidKeyOutcomeAuthSucceeded,
} CuidKeyOutcome;

typedef enum {
    CuidActionRetryCurrentKey,
    CuidActionAdvanceCursor,
    CuidActionReadSector,
} CuidAction;

typedef struct {
    uint16_t key_index;
    uint8_t current_sector;
} CuidAppCursor;

static void cuid_app_advance(CuidAppCursor* cursor) {
    cursor->key_index++;
    cursor->current_sector = cursor->key_index / 2U;
}

static CuidAction cuid_action(CuidKeyOutcome outcome) {
    switch(outcome) {
    case CuidKeyOutcomeKnown:
    case CuidKeyOutcomeDictionaryExhausted:
        return CuidActionAdvanceCursor;
    case CuidKeyOutcomeAuthFailed:
        return CuidActionRetryCurrentKey;
    case CuidKeyOutcomeAuthSucceeded:
        return CuidActionReadSector;
    default:
        assert(false);
        return CuidActionRetryCurrentKey;
    }
}

static bool cuid_cursor_complete(uint8_t current_sector, uint8_t sectors_total) {
    return current_sector >= sectors_total;
}

static void test_key_outcomes(void) {
    assert(cuid_action(CuidKeyOutcomeKnown) == CuidActionAdvanceCursor);
    assert(cuid_action(CuidKeyOutcomeDictionaryExhausted) == CuidActionAdvanceCursor);
    assert(cuid_action(CuidKeyOutcomeAuthFailed) == CuidActionRetryCurrentKey);
    assert(cuid_action(CuidKeyOutcomeAuthSucceeded) == CuidActionReadSector);
}

static void test_final_sector_key_b_is_visited(void) {
    enum {
        sectors_total = 16,
        keys_total = sectors_total * 2,
    };
    bool visited[keys_total] = {false};
    CuidAppCursor cursor = {0};

    while(!cuid_cursor_complete(cursor.current_sector, sectors_total)) {
        assert(cursor.key_index < keys_total);
        visited[cursor.key_index] = true;
        assert(cuid_action(CuidKeyOutcomeKnown) == CuidActionAdvanceCursor);
        cuid_app_advance(&cursor);
    }

    assert(cursor.key_index == keys_total);
    assert(visited[keys_total - 1]);
}

static void test_mixed_known_and_failed_auth_stays_on_same_key(void) {
    CuidAppCursor cursor = {
        .key_index = 4,
        .current_sector = 2,
    };

    assert(cuid_action(CuidKeyOutcomeKnown) == CuidActionAdvanceCursor);
    cuid_app_advance(&cursor);
    assert(cursor.key_index == 5);
    assert(cursor.current_sector == 2);

    assert(cuid_action(CuidKeyOutcomeAuthFailed) == CuidActionRetryCurrentKey);
    assert(cursor.key_index == 5);
    assert(cursor.current_sector == 2);

    assert(cuid_action(CuidKeyOutcomeAuthSucceeded) == CuidActionReadSector);
    assert(cursor.key_index == 5);
    assert(cursor.current_sector == 2);

    assert(cuid_action(CuidKeyOutcomeKnown) == CuidActionAdvanceCursor);
    cuid_app_advance(&cursor);
    assert(cursor.key_index == 6);
    assert(cursor.current_sector == 3);
}

int main(void) {
    test_key_outcomes();
    test_final_sector_key_b_is_visited();
    test_mixed_known_and_failed_auth_stays_on_same_key();
    return 0;
}
