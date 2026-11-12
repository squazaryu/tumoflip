#pragma once
#include "field_round.h"
#include <gui/modules/submenu.h>
typedef enum {
    FieldPageHome,
    FieldPageRound,
    FieldPageItem,
    FieldPageNewConfirm,
    FieldPageCards,
    FieldPageBundle,
    FieldPageBusy,
    FieldPageMessage,
    FieldPageInput,
} FieldPage;
enum {
    FieldStartRound = 1,
    FieldResumeRound,
    FieldExportRound,
    FieldExportObject,
    FieldChooseBundle,
    FieldInventoryCreate,
    FieldInventoryCompare,
    FieldAbout,
    FieldConfirmNew,
    FieldPass,
    FieldProblem,
    FieldSkip,
    FieldNote,
    FieldOpenObject,
    FieldImport,
    FieldNextCards,
    FieldPreviousCards,
    FieldCardBase = 100,
    FieldRoundBase = 200,
};
typedef struct {
    FieldPage page;
    const FieldRound* round;
    const DeviceCard* card;
    const char (*names)[32];
    uint32_t count, offset, selected;
    bool more;
} FieldMenuState;
void field_menu_build(
    Submenu* menu,
    const FieldMenuState* state,
    void (*callback)(void*, uint32_t),
    void* context);
bool field_menu_allows(const FieldMenuState* state, uint32_t action);
