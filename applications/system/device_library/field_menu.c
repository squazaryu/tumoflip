#include "field_menu.h"
#include <stdio.h>

static bool field_menu_has_item(const FieldMenuState* state) {
    return state->round && state->round->count <= LIBRARY_PINS &&
           state->selected < state->round->count;
}

bool field_menu_allows(const FieldMenuState* state, uint32_t action) {
    if(!state || !action) return false;
    switch(state->page) {
    case FieldPageHome:
        return action >= FieldStartRound && action <= FieldAbout;
    case FieldPageNewConfirm:
        return action == FieldConfirmNew;
    case FieldPageBundle:
        return action == FieldImport;
    case FieldPageCards:
        return (action >= FieldCardBase && action < FieldCardBase + state->count &&
                state->count <= 32) ||
               (action == FieldNextCards && state->more) ||
               (action == FieldPreviousCards && state->offset >= 32);
    case FieldPageRound: {
        unsigned pending;
        return field_round_pending_count(state->round, &pending) &&
               ((action >= FieldRoundBase && action < FieldRoundBase + state->round->count) ||
                action == FieldExportRound || (action == FieldNextPending && pending > 0));
    }
    case FieldPageItem:
        return field_menu_has_item(state) && ((action >= FieldPass && action <= FieldOpenObject) ||
                                              action == FieldCheckLinkedFiles);
    default:
        return false;
    }
}
bool field_menu_select_next_pending(FieldMenuState* state) {
    if(!field_menu_allows(state, FieldNextPending)) return false;
    const unsigned start =
        state->selected < state->round->count ? (state->selected + 1) % state->round->count : 0;
    unsigned selected;
    if(!field_round_next_pending(state->round, start, &selected)) return false;
    state->selected = selected;
    return true;
}
void field_menu_build(
    Submenu* menu,
    const FieldMenuState* state,
    void (*callback)(void*, uint32_t),
    void* context) {
    submenu_reset(menu);
#define FIELD_ROW(label, action) submenu_add_item(menu, label, action, callback, context)
    switch(state->page) {
    case FieldPageHome:
        submenu_set_header(menu, "Field tools");
        FIELD_ROW("Start object round", FieldStartRound);
        FIELD_ROW("Resume object round", FieldResumeRound);
        FIELD_ROW("Export object", FieldExportObject);
        FIELD_ROW("Import object copy", FieldChooseBundle);
        FIELD_ROW("Snapshot captures", FieldInventoryCreate);
        FIELD_ROW("Compare captures", FieldInventoryCompare);
        FIELD_ROW("Export round report", FieldExportRound);
        FIELD_ROW("About / limits", FieldAbout);
        break;
    case FieldPageNewConfirm:
        submenu_set_header(menu, "New object round");
        FIELD_ROW("Export old report first", 0);
        FIELD_ROW("Use current pinned list", FieldConfirmNew);
        FIELD_ROW("Back: keep current round", 0);
        break;
    case FieldPageRound: {
        unsigned pending;
        if(!field_round_pending_count(state->round, &pending)) {
            submenu_set_header(menu, "Object round");
            FIELD_ROW("No readable round", 0);
            break;
        }
        char header[32];
        snprintf(
            header,
            sizeof(header),
            "Round: %u/%lu left",
            pending,
            (unsigned long)state->round->count);
        submenu_set_header(menu, header);
        if(pending > 0) FIELD_ROW("Next pending object", FieldNextPending);
        for(unsigned i = 0; i < state->round->count; i++) {
            char label[64];
            snprintf(
                label,
                sizeof(label),
                "%s %s",
                field_round_result_name(state->round->items[i].result),
                state->round->items[i].object);
            FIELD_ROW(label, FieldRoundBase + i);
        }
        FIELD_ROW("Export round report", FieldExportRound);
        break;
    }
    case FieldPageItem:
        submenu_set_header(menu, "Object check");
        if(!field_menu_has_item(state)) {
            FIELD_ROW("No selected object", 0);
            break;
        }
        FIELD_ROW(state->round->items[state->selected].object, 0);
        FIELD_ROW("Open object actions", FieldOpenObject);
        FIELD_ROW("Check linked files", FieldCheckLinkedFiles);
        FIELD_ROW("Mark PASS", FieldPass);
        FIELD_ROW("Mark PROBLEM", FieldProblem);
        FIELD_ROW("Mark SKIPPED", FieldSkip);
        FIELD_ROW("Edit result note", FieldNote);
        break;
    case FieldPageCards:
        submenu_set_header(menu, "Export object");
        if(state->count <= 32 && state->names)
            for(unsigned i = 0; i < state->count; i++)
                FIELD_ROW(state->names[i], FieldCardBase + i);
        if(!state->count) FIELD_ROW("No objects", 0);
        if(state->offset) FIELD_ROW("< Previous page", FieldPreviousCards);
        if(state->more) FIELD_ROW("Next page >", FieldNextCards);
        break;
    case FieldPageBundle:
        submenu_set_header(menu, "Verified object");
        if(state->card) FIELD_ROW(state->card->name, 0);
        FIELD_ROW("Import as new object...", FieldImport);
        FIELD_ROW("Original files unchanged", 0);
        break;
    case FieldPageBusy:
        submenu_set_header(menu, "Working...");
        FIELD_ROW("Reading / verifying files", 0);
        FIELD_ROW("Back: cancel operation", 0);
        break;
    default:
        submenu_set_header(menu, "Field tools");
        break;
    }
#undef FIELD_ROW
}
