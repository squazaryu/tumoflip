#include "field_links.h"

typedef enum {
    FieldLinkPresent,
    FieldLinkMissing,
    FieldLinkEmpty,
    FieldLinkFolder,
    FieldLinkUnchecked,
} FieldLinkStatus;

static FieldLinkStatus field_link_status(FS_Error error, const FileInfo* info) {
    if(error == FSE_NOT_EXIST) return FieldLinkMissing;
    if(error != FSE_OK) return FieldLinkUnchecked;
    if(file_info_is_dir(info)) return FieldLinkFolder;
    return info->size ? FieldLinkPresent : FieldLinkEmpty;
}

void field_links_report(Storage* storage, const DeviceCard* card, FuriString* report) {
    if(!report) return;
    const char* unavailable = "SD unavailable.\nNo files checked.\nRound unchanged.";
    if(!storage) {
        furi_string_set(report, unavailable);
        return;
    }
    if(!library_card_valid(card)) {
        furi_string_set(report, "Invalid object.\nNo files checked.\nRound unchanged.");
        return;
    }
    if(!card->link_count) {
        furi_string_set(report, "No linked files.\nRound unchanged.");
        return;
    }
    if(storage_sd_status(storage) != FSE_OK) {
        furi_string_set(report, unavailable);
        return;
    }

    FieldLinkStatus status[LIBRARY_LINKS];
    unsigned present = 0;
    for(unsigned i = 0; i < card->link_count; i++) {
        FileInfo info = {0};
        FS_Error error = storage_common_stat(storage, card->links[i], &info);
        if(error == FSE_NOT_READY) {
            furi_string_set(report, unavailable);
            return;
        }
        status[i] = field_link_status(error, &info);
        if(status[i] == FieldLinkPresent) present++;
    }
    if(storage_sd_status(storage) != FSE_OK) {
        furi_string_set(report, unavailable);
        return;
    }

    static const char* const labels[] = {
        "Present", "Missing", "Empty", "Folder", "Not checked",
    };
    furi_string_printf(report, "Linked files\n%s\n%u/%lu present\n\n", card->name, present,
                       (unsigned long)card->link_count);
    for(unsigned i = 0; i < card->link_count; i++) {
        char label[32] = {0};
        library_action_label(card, i, label, sizeof(label));
        furi_string_cat_printf(report, "%u. %s\n%s\n\n", i + 1, labels[status[i]], label);
    }
    furi_string_cat(report, "Contents not checked.\nRound unchanged.");
}
