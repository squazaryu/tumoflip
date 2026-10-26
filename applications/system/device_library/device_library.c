#include "card_store.h"
#include "wallet_launch.h"
#include <toolbox/file_history.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/text_input.h>
#include <gui/view_dispatcher.h>
#include <dialogs/dialogs.h>
#include <loader/loader.h>
#include <stdio.h>
#include <furi_hal_rtc.h>

enum {
    LibraryMenuView,
    LibraryTextView,
    LibraryInputView
};
typedef enum {
    PageMain,
    PageCards,
    PageCard,
    PageManage,
    PagePins,
    PageRemovePins,
    PageConfirmUnpin,
    PageLink,
    PageHistory,
    PageSources,
    PageVersions,
    PageText,
    PageInput,
    PageDiscard
} LibraryPage;
enum {
    NewCard = 1,
    OpenCards,
    History,
    About,
    Note,
    Tags,
    AddLink,
    SaveCard,
    Details,
    OpenLink,
    RemoveLink,
    EnableHistory,
    DisableHistory,
    SelectHistory,
    Snapshot,
    NameDone,
    KeepEditing,
    Discard,
    MarkChecked,
    OpenPins,
    EditObject,
    RenameAction,
    TogglePin,
    RemovePins,
    ConfirmUnpin,
    OpenFieldTools,
    ExportObject,
    NextCards = 30,
    PreviousCards,
    NextHistories,
    PreviousHistories,
    CardBase = 100,
    LinkBase = 200,
    SourceBase = 300,
    VersionBase = 400,
    PinBase = 500,
    PinRemoveBase = 600
};
typedef struct {
    Storage* storage;
    DialogsApp* dialogs;
    Loader* loader;
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    TextBox* text_view;
    TextInput* input;
    FuriString* text;
    FuriString* path;
    LibraryPage page, back;
    DeviceCard card;
    LibraryPins pins;
    char names[32][32];
    char (*sources)[256];
    uint32_t count, edit, link;
    uint32_t card_offset, history_offset;
    uint32_t pin_edit;
    bool dirty;
    bool pins_ready;
    char edit_buffer[128];
    FileHistoryRecord records[4];
    PluginManager* history_manager;
    const FileHistoryApi* history;
} Library;

static void library_show(Library* app, LibraryPage page);
static void library_action(void* context, uint32_t event) {
    view_dispatcher_send_custom_event(((Library*)context)->dispatcher, event);
}
static void library_message(Library* app, const char* message, LibraryPage back) {
    if(message != furi_string_get_cstr(app->text)) furi_string_set(app->text, message);
    text_box_reset(app->text_view);
    text_box_set_text(app->text_view, furi_string_get_cstr(app->text));
    app->page = PageText;
    app->back = back;
    view_dispatcher_switch_to_view(app->dispatcher, LibraryTextView);
}
static void library_item(Library* app, const char* label, unsigned id) {
    submenu_add_item(app->menu, label, id, library_action, app);
}
static bool library_history_load(Library* app) {
    if(app->history) return true;
    app->history_manager =
        plugin_manager_alloc(FILE_HISTORY_APP_ID, FILE_HISTORY_ABI, firmware_api_interface);
    if(plugin_manager_load_single(app->history_manager, FILE_HISTORY_PLUGIN) ==
       PluginManagerErrorNone)
        app->history = plugin_manager_get_ep(app->history_manager, 0);
    if(!app->history) {
        plugin_manager_free(app->history_manager);
        app->history_manager = NULL;
        library_message(
            app, "History module missing\nor incompatible.\nUpdate FW Packages.", PageMain);
    }
    return app->history != NULL;
}
static void library_cards(Library* app) {
    app->count = 0;
    uint32_t seen = 0;
    bool more = false;
    File* dir = storage_file_alloc(app->storage);
    if(storage_dir_open(dir, LIBRARY_CARDS)) {
        FileInfo info;
        char name[64];
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(file_info_is_dir(&info) && library_name_valid(name)) {
                if(seen++ < app->card_offset) continue;
                if(app->count == 32) {
                    more = true;
                    break;
                }
                strlcpy(app->names[app->count], name, sizeof(app->names[0]));
                library_item(app, name, CardBase + app->count++);
            }
        }
    }
    storage_file_free(dir);
    if(!app->count) library_item(app, "No cards yet", 0);
    if(app->card_offset) library_item(app, "< Previous page", PreviousCards);
    if(more) library_item(app, "Next page >", NextCards);
}
static void library_pinned(Library* app, bool remove) {
    if(!app->pins_ready) {
        library_item(app, "Pins: read error", 0);
        return;
    }
    for(unsigned i = 0; i < app->pins.count; i++)
        library_item(app, app->pins.names[i], (remove ? PinRemoveBase : PinBase) + i);
    if(!app->pins.count) library_item(app, "No pinned objects", 0);
    if(!remove && app->pins.count) library_item(app, "Remove a pin...", RemovePins);
}
static void library_show(Library* app, LibraryPage page) {
    app->page = page;
    submenu_reset(app->menu);
    if(page == PageMain) {
        submenu_set_header(app->menu, "Device Library");
        library_item(app, "Pinned objects", OpenPins);
        library_item(app, "All objects", OpenCards);
        library_item(app, "New object", NewCard);
        library_item(app, "File History", History);
        library_item(app, "Field tools", OpenFieldTools);
        library_item(app, "About / limits", About);
    } else if(page == PageCards) {
        submenu_set_header(app->menu, "All objects");
        library_cards(app);
    } else if(page == PagePins || page == PageRemovePins) {
        submenu_set_header(app->menu, page == PagePins ? "Pinned objects" : "Remove a pin");
        library_pinned(app, page == PageRemovePins);
    } else if(page == PageConfirmUnpin) {
        submenu_set_header(app->menu, "Keep object files");
        library_item(app, app->pins.names[app->pin_edit], 0);
        library_item(app, "Remove pin only", ConfirmUnpin);
        library_item(app, "Keep pin", KeepEditing);
    } else if(page == PageCard) {
        submenu_set_header(app->menu, "Object actions");
        library_item(app, app->card.name, Details);
        for(unsigned i = 0; i < app->card.link_count; i++) {
            char label[48];
            if(library_action_label(&app->card, i, label, sizeof(label)))
                library_item(app, label, LinkBase + i);
        }
        if(!app->card.link_count) library_item(app, "Add first action", AddLink);
        if(app->dirty) library_item(app, "Save changes *", SaveCard);
        library_item(app, "Edit object...", EditObject);
    } else if(page == PageManage) {
        submenu_set_header(app->menu, "Edit object");
        library_item(app, app->card.name, Details);
        library_item(app, "Edit notes", Note);
        library_item(app, "Edit tags", Tags);
        library_item(app, "Mark checked", MarkChecked);
        library_item(app, "Export object copy", ExportObject);
        library_item(app, "Add file action", AddLink);
        library_item(app, app->dirty ? "Save changes *" : "Save card", SaveCard);
        library_item(
            app,
            app->pins_ready && library_pins_contains(&app->pins, app->card.name) ? "Unpin object" :
                                                                                   "Pin object",
            TogglePin);
        for(unsigned i = 0; i < app->card.link_count; i++) {
            char label[48];
            if(library_action_label(&app->card, i, label, sizeof(label)))
                library_item(app, label, LinkBase + i);
        }
    } else if(page == PageLink) {
        submenu_set_header(app->menu, "File action");
        library_item(app, "Show path / status", Details);
        library_item(app, "Open action", OpenLink);
        library_item(app, "Rename action", RenameAction);
        library_item(app, "Remove link only", RemoveLink);
    } else if(page == PageHistory) {
        submenu_set_header(app->menu, "File History");
        FS_Error enabled = storage_common_stat(app->storage, FILE_HISTORY_ENABLED, NULL);
        if(!file_history_core_available()) {
            library_item(app, "Auto: needs firmware", EnableHistory);
            if(enabled == FSE_OK) library_item(app, "Clear auto flag", DisableHistory);
        } else
            library_item(
                app,
                enabled == FSE_OK        ? "Auto snapshots: ON" :
                enabled == FSE_NOT_EXIST ? "Auto snapshots: OFF" :
                                           "History: read error",
                enabled == FSE_OK        ? DisableHistory :
                enabled == FSE_NOT_EXIST ? EnableHistory :
                                           0);
        library_item(app, "Browse saved versions", SelectHistory);
        library_item(app, "Snapshot a file now", Snapshot);
        library_item(app, "Limits / privacy", About);
    } else if(page == PageVersions) {
        submenu_set_header(app->menu, "Restore a copy");
        library_item(app, "Source file path", Details);
        bool found = false;
        if(library_history_load(app) &&
           app->history->list(app->storage, furi_string_get_cstr(app->path), app->records)) {
            uint32_t gen[4];
            for(unsigned i = 0; i < 4; i++)
                gen[i] = app->records[i].generation;
            for(unsigned n = 0; n < 4; n++) {
                int i = library_newest_slot(gen);
                if(i < 0) break;
                char label[48];
                snprintf(
                    label, sizeof(label), "Version %lu / %lu B", gen[i], app->records[i].size);
                library_item(app, label, VersionBase + i);
                gen[i] = 0;
                found = true;
            }
        }
        if(!found) library_item(app, "No valid versions", 0);
    } else if(page == PageDiscard) {
        submenu_set_header(app->menu, "Unsaved card");
        library_item(app, "Keep editing", KeepEditing);
        library_item(app, "Discard changes", Discard);
    }
    view_dispatcher_switch_to_view(app->dispatcher, LibraryMenuView);
}
static void library_input_done(void* context) {
    library_action(context, NameDone);
}
static void library_edit(Library* app, unsigned kind) {
    app->edit = kind;
    app->page = PageInput;
    strlcpy(
        app->edit_buffer,
        kind == Note         ? app->card.notes :
        kind == Tags         ? app->card.tags :
        kind == RenameAction ? app->card.labels[app->link] :
                               "",
        sizeof(app->edit_buffer));
    text_input_reset(app->input);
    text_input_set_minimum_length(app->input, kind == NewCard ? 1 : 0);
    text_input_set_header_text(
        app->input,
        kind == NewCard      ? "Device name" :
        kind == Note         ? "Device notes" :
        kind == RenameAction ? "Action label (optional)" :
                               "Tags: space separated");
    text_input_set_result_callback(
        app->input,
        library_input_done,
        app,
        app->edit_buffer,
        kind == NewCard      ? 25 :
        kind == Tags         ? 64 :
        kind == RenameAction ? 32 :
                               128,
        false);
    view_dispatcher_switch_to_view(app->dispatcher, LibraryInputView);
}
static bool library_select_file(Library* app, const char* extensions) {
    furi_string_set(app->path, "/ext");
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, extensions, NULL);
    options.base_path = "/ext";
    return dialog_file_browser_show(app->dialogs, app->path, app->path, &options) &&
           library_path_valid(furi_string_get_cstr(app->path));
}
static void library_open_link(Library* app) {
    LibraryPage back = app->page;
    if(app->link >= app->card.link_count) return;
    const char* path = app->card.links[app->link];
    LibraryFileKind kind = library_file_kind(path);
    const char* target = library_launch_target(kind);
    if(app->dirty)
        library_message(app, "Save card changes\nbefore opening a file.", back);
    else if(!target)
        library_message(
            app, "Reference-only file.\nOpen it in its own app.\nNo script is executed.", back);
    else if(!storage_file_exists(app->storage, path))
        library_message(app, "Linked file is missing.", back);
    else if(!library_native_inspect_ready(app->storage, kind))
        library_message(
            app,
            "Update native apps.\nInstall matching\nfirmware resources.\nNo action started.",
            back);
    else {
        loader_clear_launch_queue(app->loader);
        const bool inspect = kind == LibraryFileNfc || kind == LibraryFileRfid ||
                             kind == LibraryFileIbutton;
        FuriString* argument = furi_string_alloc_printf("%s%s", inspect ? "inspect:" : "", path);
        loader_enqueue_launch(
            app->loader, target, furi_string_get_cstr(argument), LoaderDeferredLaunchFlagGui);
        furi_string_free(argument);
        view_dispatcher_stop(app->dispatcher);
    }
}

static void library_set_pin(Library* app, const char* name, bool pinned, LibraryPage back) {
    if(app->dirty) {
        library_message(app, "Save object first.\nPin list unchanged.", back);
        return;
    }
    LibraryPins* next = malloc(sizeof(*next));
    bool ok = library_name_valid(name) && library_pins_load(app->storage, next);
    if(ok && library_pins_contains(next, name) != pinned)
        ok = library_pins_toggle(next, name) && library_pins_save(app->storage, next);
    if(ok) {
        app->pins = *next;
        app->pins_ready = true;
    }
    free(next);
    if(ok)
        library_show(app, back);
    else
        library_message(
            app, "Pin not saved.\nCheck SD / 16-pin limit.\nPrevious list retained.", back);
}

static bool library_event_allowed(const Library* app, uint32_t event) {
    switch(event) {
    case NewCard:
    case OpenCards:
    case OpenPins:
    case History:
    case OpenFieldTools:
        return app->page == PageMain;
    case NextCards:
    case PreviousCards:
        return app->page == PageCards;
    case EditObject:
        return app->page == PageCard;
    case Note:
    case Tags:
    case MarkChecked:
    case TogglePin:
    case ExportObject:
        return app->page == PageManage;
    case AddLink:
    case SaveCard:
        return app->page == PageCard || app->page == PageManage;
    case OpenLink:
    case RemoveLink:
    case RenameAction:
        return app->page == PageLink && app->link < app->card.link_count;
    case Details:
        return app->page == PageCard || app->page == PageManage || app->page == PageVersions ||
               (app->page == PageLink && app->link < app->card.link_count);
    case NameDone:
        return app->page == PageInput;
    case RemovePins:
        return app->page == PagePins && app->pins_ready;
    case ConfirmUnpin:
        return app->page == PageConfirmUnpin && app->pins_ready && app->pin_edit < app->pins.count;
    case KeepEditing:
        return app->page == PageDiscard || app->page == PageConfirmUnpin;
    case Discard:
        return app->page == PageDiscard;
    case About:
        return app->page == PageMain || app->page == PageHistory;
    case EnableHistory:
    case DisableHistory:
    case SelectHistory:
    case Snapshot:
        return app->page == PageHistory;
    case NextHistories:
    case PreviousHistories:
        return app->page == PageSources;
    default:
        if(event >= CardBase && event < CardBase + app->count) return app->page == PageCards;
        if(event >= LinkBase && event < LinkBase + app->card.link_count)
            return app->page == PageCard || app->page == PageManage;
        if(event >= SourceBase && event < SourceBase + app->count)
            return app->page == PageSources && app->sources;
        if(event >= VersionBase && event < VersionBase + LIBRARY_SLOTS)
            return app->page == PageVersions;
        if(event >= PinBase && event < PinBase + app->pins.count)
            return app->page == PagePins && app->pins_ready;
        if(event >= PinRemoveBase && event < PinRemoveBase + app->pins.count)
            return app->page == PageRemovePins && app->pins_ready;
        return false;
    }
}

static bool library_event(void* context, uint32_t event) {
    Library* app = context;
    if(!library_event_allowed(app, event)) return true;
    if(event == OpenFieldTools || event == ExportObject) {
        const char* target = "/ext/apps/Tools/field_tools.fap";
        if(event == ExportObject && app->dirty) {
            library_message(app, "Save object first.\nNo files exported.", PageManage);
        } else if(!storage_file_exists(app->storage, target)) {
            library_message(app, "Install Field Tools\nfrom FW Packages.\nOriginal files unchanged.", app->page);
        } else {
            FuriString* argument = furi_string_alloc_printf("%s%s", event == ExportObject ? "export:" : "", event == ExportObject ? app->card.name : "");
            loader_clear_launch_queue(app->loader);
            loader_enqueue_launch(app->loader, target, furi_string_get_cstr(argument), LoaderDeferredLaunchFlagGui);
            furi_string_free(argument);
            view_dispatcher_stop(app->dispatcher);
        }
    } else if(event == NewCard)
        library_edit(app, NewCard);
    else if(event == OpenPins && app->page == PageMain) {
        app->pins_ready = library_pins_load(app->storage, &app->pins);
        library_show(app, PagePins);
    } else if(event == RemovePins && app->page == PagePins) {
        library_show(app, PageRemovePins);
    } else if(
        event >= PinRemoveBase && event < PinRemoveBase + app->pins.count &&
        app->page == PageRemovePins) {
        app->pin_edit = event - PinRemoveBase;
        library_show(app, PageConfirmUnpin);
    } else if(
        event == ConfirmUnpin && app->page == PageConfirmUnpin &&
        app->pin_edit < app->pins.count) {
        library_set_pin(app, app->pins.names[app->pin_edit], false, PagePins);
    } else if(event == TogglePin && app->page == PageManage) {
        library_set_pin(
            app, app->card.name, !library_pins_contains(&app->pins, app->card.name), PageManage);
    } else if(event == EditObject && app->page == PageCard) {
        library_show(app, PageManage);
    } else if(event == OpenCards) {
        app->card_offset = 0;
        library_show(app, PageCards);
    } else if(event == NextCards || event == PreviousCards) {
        app->card_offset = event == NextCards     ? app->card_offset + 32 :
                           app->card_offset >= 32 ? app->card_offset - 32 :
                                                    0;
        library_show(app, PageCards);
    } else if(event == History)
        library_show(app, PageHistory);
    else if(
        (event >= CardBase && event < CardBase + app->count && app->page == PageCards) ||
        (event >= PinBase && event < PinBase + app->pins.count && app->page == PagePins)) {
        LibraryPage back = app->page;
        const char* name = app->page == PagePins ? app->pins.names[event - PinBase] :
                                                   app->names[event - CardBase];
        if(library_card_load(app->storage, name, &app->card)) {
            app->dirty = false;
            library_show(app, PageCard);
        } else
            library_message(app, "Card unreadable.\nNo valid revision found.", back);
    } else if(event == Note || event == Tags || event == RenameAction)
        library_edit(app, event);
    else if(event == NameDone) {
        if(app->edit == NewCard) {
            FuriString* dir = furi_string_alloc_printf(LIBRARY_CARDS "/%s", app->edit_buffer);
            bool valid = library_name_valid(app->edit_buffer) &&
                         storage_common_stat(app->storage, furi_string_get_cstr(dir), NULL) ==
                             FSE_NOT_EXIST;
            furi_string_free(dir);
            if(!valid) {
                library_message(
                    app,
                    "Use a new name.\nLetters, digits, spaces,\n- and _; up to 24 chars.\nCheck SD card.",
                    PageMain);
                return true;
            }
            memset(&app->card, 0, sizeof(app->card));
            strlcpy(app->card.name, app->edit_buffer, sizeof(app->card.name));
        } else if(app->edit == RenameAction)
            strlcpy(app->card.labels[app->link], app->edit_buffer, sizeof(app->card.labels[0]));
        else if(app->edit == Note)
            strlcpy(app->card.notes, app->edit_buffer, sizeof(app->card.notes));
        else
            strlcpy(app->card.tags, app->edit_buffer, sizeof(app->card.tags));
        app->dirty = true;
        library_show(
            app,
            app->edit == RenameAction ? PageLink :
            app->edit == NewCard      ? PageCard :
                                        PageManage);
    } else if(event == AddLink) {
        LibraryPage back = app->page;
        if(app->card.link_count >= LIBRARY_LINKS)
            library_message(app, "Eight links per card.\nRemove a link first.", back);
        else if(library_select_file(app, NULL)) {
            const char* path = furi_string_get_cstr(app->path);
            bool duplicate = false;
            for(unsigned i = 0; i < app->card.link_count; i++)
                if(!strcmp(path, app->card.links[i])) duplicate = true;
            if(!duplicate) {
                memset(app->card.labels[app->card.link_count], 0, sizeof(app->card.labels[0]));
                strlcpy(app->card.links[app->card.link_count++], path, 256);
                app->dirty = true;
            }
            library_show(app, back);
        }
    } else if(event == MarkChecked) {
        DateTime now;
        furi_hal_rtc_get_datetime(&now);
        snprintf(
            app->card.checked_at,
            sizeof(app->card.checked_at),
            "%04u-%02u-%02u %02u:%02u",
            now.year,
            now.month,
            now.day,
            now.hour,
            now.minute);
        app->dirty = true;
        library_message(
            app, "Marked by you.\nSave card to keep date.\nNot an automatic test.", PageManage);
    } else if(event == SaveCard) {
        bool ok = library_card_save(app->storage, &app->card);
        if(ok) app->dirty = false;
        library_message(
            app,
            ok ? "Card saved.\nLinked files unchanged." :
                 "Save failed.\nPrevious revision retained.",
            PageCard);
    } else if(
        event >= LinkBase && event < LinkBase + app->card.link_count &&
        (app->page == PageCard || app->page == PageManage)) {
        app->link = event - LinkBase;
        if(app->page == PageCard)
            library_open_link(app);
        else
            library_show(app, PageLink);
    } else if(event == RemoveLink && app->link < app->card.link_count) {
        if(library_remove_link(&app->card, app->link)) app->dirty = true;
        library_show(app, PageManage);
    } else if(event == Details) {
        LibraryPage back = app->page;
        if(back == PageVersions)
            furi_string_set(app->text, furi_string_get_cstr(app->path));
        else if(back == PageLink)
            furi_string_printf(
                app->text,
                "%s\n\n%s",
                app->card.links[app->link],
                storage_file_exists(app->storage, app->card.links[app->link]) ? "File available" :
                                                                                "File missing");
        else
            furi_string_printf(
                app->text,
                "%s\n\nNotes:\n%s\n\nTags:\n%s\nLinks: %lu\n\nManually checked:\n%s",
                app->card.name,
                app->card.notes,
                app->card.tags,
                app->card.link_count,
                app->card.checked_at[0] ? app->card.checked_at : "Not marked");
        library_message(app, furi_string_get_cstr(app->text), back);
    } else if(event == OpenLink) {
        library_open_link(app);
    } else if(event == EnableHistory || event == DisableHistory) {
        if(event == EnableHistory && !file_history_core_available()) {
            library_message(
                app,
                "Update firmware for\nautomatic snapshots.\nManual snapshots work.",
                PageHistory);
            return true;
        }
        bool ok = false;
        if(event == EnableHistory && library_history_load(app) &&
           storage_simply_mkdir(app->storage, LIBRARY_ROOT) &&
           storage_simply_mkdir(app->storage, FILE_HISTORY_ROOT)) {
            File* file = storage_file_alloc(app->storage);
            bool created =
                storage_file_open(file, FILE_HISTORY_ENABLED, FSAM_WRITE, FSOM_CREATE_NEW);
            ok = created ? storage_file_sync(file) :
                           storage_common_stat(app->storage, FILE_HISTORY_ENABLED, NULL) == FSE_OK;
            if(created) ok = storage_file_close(file) && ok;
            storage_file_free(file);
            if(created && !ok) storage_common_remove(app->storage, FILE_HISTORY_ENABLED);
        } else if(event == DisableHistory)
            ok = storage_common_remove(app->storage, FILE_HISTORY_ENABLED) == FSE_OK;
        library_message(
            app,
            ok ?
                (event == EnableHistory ?
                     "History enabled.\nNative SUB / IR / NFC.\n3 versions, 2 MiB max.\nBackups may be private." :
                     "Auto snapshots disabled.\nSaved versions retained.") :
                "History setting failed.\nCheck SD / FW Packages.",
            PageHistory);
    } else if(event == Snapshot) {
        if(library_history_load(app) && library_select_file(app, ".sub|.ir|.nfc")) {
            view_dispatcher_show_loading(app->dispatcher);
            bool ok = app->history->snapshot(app->storage, furi_string_get_cstr(app->path));
            library_message(
                app,
                ok ? "Snapshot verified.\nOriginal unchanged." :
                     "Snapshot failed.\nOriginal unchanged.\nCheck SD / 2 MiB limit.",
                PageHistory);
        }
    } else if(event == SelectHistory || event == NextHistories || event == PreviousHistories) {
        if(library_history_load(app)) {
            if(event == SelectHistory)
                app->history_offset = 0;
            else if(event == NextHistories)
                app->history_offset += 32;
            else
                app->history_offset = app->history_offset >= 32 ? app->history_offset - 32 : 0;
            free(app->sources);
            app->sources = calloc(32, 256);
            view_dispatcher_show_loading(app->dispatcher);
            bool more = false;
            bool ok = app->history->sources(
                app->storage, app->history_offset, app->sources, &app->count, &more);
            if(!ok || (!app->count && !app->history_offset))
                library_message(
                    app, ok ? "No saved versions yet." : "History read failed.", PageHistory);
            else {
                app->page = PageSources;
                submenu_reset(app->menu);
                submenu_set_header(app->menu, "Saved Files");
                for(unsigned i = 0; i < app->count; i++) {
                    const char* name = strrchr(app->sources[i], '/');
                    library_item(app, name ? name + 1 : app->sources[i], SourceBase + i);
                }
                if(app->history_offset) library_item(app, "< Previous page", PreviousHistories);
                if(more) library_item(app, "Next page >", NextHistories);
                view_dispatcher_switch_to_view(app->dispatcher, LibraryMenuView);
            }
        }
    } else if(
        event >= SourceBase && event < SourceBase + app->count && app->sources &&
        app->page == PageSources) {
        furi_string_set(app->path, app->sources[event - SourceBase]);
        library_show(app, PageVersions);
    } else if(event >= VersionBase && event < VersionBase + 4 && app->page == PageVersions) {
        FuriString* dest = furi_string_alloc();
        view_dispatcher_show_loading(app->dispatcher);
        bool ok = app->history->restore_copy(
            app->storage, furi_string_get_cstr(app->path), event - VersionBase, dest);
        if(ok)
            furi_string_printf(
                app->text,
                "Restored as new copy:\n%s\n\nCurrent file unchanged.",
                furi_string_get_cstr(dest));
        library_message(
            app,
            ok ?
                furi_string_get_cstr(app->text) :
                "Restore failed.\nBackup missing/corrupt,\nSD error or path too long.\nCurrent file unchanged.",
            PageVersions);
        furi_string_free(dest);
    } else if(event == KeepEditing)
        library_show(app, app->page == PageConfirmUnpin ? PagePins : PageCard);
    else if(event == Discard) {
        app->dirty = false;
        library_show(app, PageMain);
    } else if(event == About)
        library_message(
            app,
            "Device Library 1.1\n\nObjects link existing files: 8 named actions per object, 16 pinned objects. Removing a link or pin never deletes its file.\n\nNFC / RFID / iButton open a saved-file menu, not emulation. SUB / IR keep their native file flow. Unknown formats are references only.\n\nOld cards load without rewriting. Saving uses the new card format; older Device Library versions cannot read new revisions.\n\nHistory is opt-in for native SUB / IR / NFC saves. Three prior versions per file, 2 MiB each. Third-party writers and deletions are not intercepted.\n\nRestore creates a separate copy. Backups may contain private data; they stay on your SD. Lists use 32 entries per page.",
            app->page);
    return true;
}
static bool library_back(void* context) {
    Library* app = context;
    if(app->page == PageMain) {
        view_dispatcher_stop(app->dispatcher);
        return true;
    }
    if(app->page == PageText)
        library_show(app, app->back);
    else if(app->page == PageInput)
        library_show(
            app,
            app->edit == NewCard      ? PageMain :
            app->edit == RenameAction ? PageLink :
                                        PageManage);
    else if(app->page == PageCard && app->dirty)
        library_show(app, PageDiscard);
    else if(app->page == PageManage || app->page == PageDiscard)
        library_show(app, PageCard);
    else if(app->page == PageLink)
        library_show(app, PageManage);
    else if(app->page == PageRemovePins || app->page == PageConfirmUnpin)
        library_show(app, PagePins);
    else if(app->page == PageVersions || app->page == PageSources)
        library_show(app, PageHistory);
    else
        library_show(app, PageMain);
    return true;
}
int32_t device_library_app(void* context) {
    Library* app = calloc(1, sizeof(*app));
    app->storage = furi_record_open(RECORD_STORAGE);
    app->dialogs = furi_record_open(RECORD_DIALOGS);
    app->loader = furi_record_open(RECORD_LOADER);
    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->text_view = text_box_alloc();
    app->input = text_input_alloc();
    app->text = furi_string_alloc();
    app->path = furi_string_alloc();
    app->pins_ready = library_pins_load(app->storage, &app->pins);
    view_dispatcher_set_event_callback_context(app->dispatcher, app);
    view_dispatcher_set_custom_event_callback(app->dispatcher, library_event);
    view_dispatcher_set_navigation_event_callback(app->dispatcher, library_back);
    text_box_set_font(app->text_view, TextBoxFontText);
    view_dispatcher_add_view(app->dispatcher, LibraryMenuView, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, LibraryTextView, text_box_get_view(app->text_view));
    view_dispatcher_add_view(app->dispatcher, LibraryInputView, text_input_get_view(app->input));
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    const char* argument = context;
    if(argument && !strncmp(argument, "object:", 7) && library_name_valid(argument + 7) &&
       library_card_load(app->storage, argument + 7, &app->card))
        library_show(app, PageCard);
    else library_show(app, PageMain);
    view_dispatcher_run(app->dispatcher);
    if(app->history_manager) plugin_manager_free(app->history_manager);
    view_dispatcher_remove_view(app->dispatcher, LibraryInputView);
    view_dispatcher_remove_view(app->dispatcher, LibraryTextView);
    view_dispatcher_remove_view(app->dispatcher, LibraryMenuView);
    text_input_free(app->input);
    text_box_free(app->text_view);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);
    furi_string_free(app->text);
    furi_string_free(app->path);
    free(app->sources);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_LOADER);
    furi_record_close(RECORD_DIALOGS);
    furi_record_close(RECORD_STORAGE);
    free(app);
    return 0;
}
