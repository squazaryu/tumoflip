#include "card_store.h"
#include <toolbox/file_history.h>
#include <gui/gui.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/text_input.h>
#include <gui/view_dispatcher.h>
#include <dialogs/dialogs.h>
#include <loader/loader.h>
#include <stdio.h>

enum { LibraryMenuView, LibraryTextView, LibraryInputView };
typedef enum { PageMain, PageCards, PageCard, PageLink, PageHistory, PageVersions, PageText, PageInput, PageDiscard } LibraryPage;
enum { NewCard=1, OpenCards, History, About, Note, Tags, AddLink, SaveCard, Details, OpenLink, RemoveLink,
    EnableHistory, DisableHistory, SelectHistory, Snapshot, NameDone, KeepEditing, Discard, CardBase=100, LinkBase=200, SourceBase=300, VersionBase=400 };
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
    char names[32][32];
    char (*sources)[256];
    uint32_t count, edit, link;
    bool dirty;
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
    app->page = PageText; app->back = back;
    view_dispatcher_switch_to_view(app->dispatcher, LibraryTextView);
}
static void library_item(Library* app, const char* label, unsigned id) {
    submenu_add_item(app->menu, label, id, library_action, app);
}
static bool library_history_load(Library* app) {
    if(app->history) return true;
    app->history_manager = plugin_manager_alloc(FILE_HISTORY_APP_ID, FILE_HISTORY_ABI, firmware_api_interface);
    if(plugin_manager_load_single(app->history_manager, FILE_HISTORY_PLUGIN) == PluginManagerErrorNone)
        app->history = plugin_manager_get_ep(app->history_manager, 0);
    if(!app->history) {
        plugin_manager_free(app->history_manager); app->history_manager = NULL;
        library_message(app, "History module missing\nor incompatible.\nUpdate FW Packages.", PageMain);
    }
    return app->history != NULL;
}
static void library_cards(Library* app) {
    app->count = 0;
    File* dir = storage_file_alloc(app->storage);
    if(storage_dir_open(dir, LIBRARY_CARDS)) {
        FileInfo info; char name[64];
        while(app->count < 32 && storage_dir_read(dir, &info, name, sizeof(name))) {
            if(file_info_is_dir(&info) && library_name_valid(name)) {
                strlcpy(app->names[app->count], name, sizeof(app->names[0]));
                library_item(app, name, CardBase + app->count++);
            }
        }
    }
    storage_file_free(dir);
    if(!app->count) library_item(app, "No cards yet", 0);
}
static void library_show(Library* app, LibraryPage page) {
    app->page = page;
    submenu_reset(app->menu);
    if(page == PageMain) {
        submenu_set_header(app->menu, "Device Library");
        library_item(app, "New device card", NewCard);
        library_item(app, "Open device card", OpenCards);
        library_item(app, "File History", History);
        library_item(app, "About / limits", About);
    } else if(page == PageCards) {
        submenu_set_header(app->menu, "Device cards (32 max)");
        library_cards(app);
    } else if(page == PageCard) {
        submenu_set_header(app->menu, app->card.name);
        library_item(app, "Card details", Details);
        library_item(app, "Edit notes", Note);
        library_item(app, "Edit tags", Tags);
        library_item(app, "Add file link", AddLink);
        library_item(app, app->dirty ? "Save changes *" : "Save card", SaveCard);
        for(unsigned i = 0; i < app->card.link_count; i++) {
            const char* name = strrchr(app->card.links[i], '/');
            library_item(app, name ? name + 1 : app->card.links[i], LinkBase + i);
        }
    } else if(page == PageLink) {
        submenu_set_header(app->menu, "Linked file");
        library_item(app, "Show path / status", Details);
        library_item(app, "Open in standard app", OpenLink);
        library_item(app, "Remove link only", RemoveLink);
    } else if(page == PageHistory) {
        submenu_set_header(app->menu, "File History");
        FS_Error enabled = storage_common_stat(app->storage, FILE_HISTORY_ENABLED, NULL);
        library_item(app, enabled == FSE_OK ? "Auto snapshots: ON" : "Auto snapshots: OFF",
            enabled == FSE_OK ? DisableHistory : EnableHistory);
        library_item(app, "Browse saved versions", SelectHistory);
        library_item(app, "Snapshot a file now", Snapshot);
        library_item(app, "Limits / privacy", About);
    } else if(page == PageVersions) {
        submenu_set_header(app->menu, "Restore a copy");
        bool found = false;
        if(library_history_load(app) && app->history->list(app->storage, furi_string_get_cstr(app->path), app->records)) {
            uint32_t gen[4]; for(unsigned i=0;i<4;i++) gen[i]=app->records[i].generation;
            for(unsigned n=0;n<4;n++) {
                int i = library_newest_slot(gen); if(i<0) break;
                char label[48]; snprintf(label,sizeof(label),"Version %lu / %lu B",gen[i],app->records[i].size);
                library_item(app,label,VersionBase+i); gen[i]=0; found=true;
            }
        }
        if(!found) library_item(app,"No valid versions",0);
    } else if(page == PageDiscard) {
        submenu_set_header(app->menu,"Unsaved card");
        library_item(app,"Keep editing",KeepEditing);
        library_item(app,"Discard changes",Discard);
    }
    view_dispatcher_switch_to_view(app->dispatcher, LibraryMenuView);
}
static void library_input_done(void* context) { library_action(context, NameDone); }
static void library_edit(Library* app, unsigned kind) {
    app->edit=kind; app->page=PageInput;
    strlcpy(app->edit_buffer,kind==Note?app->card.notes:kind==Tags?app->card.tags:"",sizeof(app->edit_buffer));
    text_input_reset(app->input);
    text_input_set_header_text(app->input,kind==NewCard?"Device name":kind==Note?"Device notes":"Tags: space separated");
    text_input_set_result_callback(app->input,library_input_done,app,app->edit_buffer,kind==NewCard?25:kind==Tags?64:128,false);
    view_dispatcher_switch_to_view(app->dispatcher,LibraryInputView);
}
static bool library_select_file(Library* app, const char* extensions) {
    furi_string_set(app->path,"/ext");
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options,extensions,NULL);
    options.base_path="/ext";
    return dialog_file_browser_show(app->dialogs,app->path,app->path,&options) && library_path_valid(furi_string_get_cstr(app->path));
}
static bool library_event(void* context, uint32_t event) {
    Library* app=context;
    if(event==NewCard) library_edit(app,NewCard);
    else if(event==OpenCards) library_show(app,PageCards);
    else if(event==History) library_show(app,PageHistory);
    else if(event>=CardBase && event<CardBase+app->count && app->page==PageCards) {
        if(library_card_load(app->storage,app->names[event-CardBase],&app->card)) { app->dirty=false;library_show(app,PageCard); }
        else library_message(app,"Card unreadable.\nNo valid revision found.",PageCards);
    } else if(event==Note || event==Tags) library_edit(app,event);
    else if(event==NameDone) {
        if(app->edit==NewCard) {
            FuriString* dir=furi_string_alloc_printf(LIBRARY_CARDS "/%s",app->edit_buffer);
            bool valid=library_name_valid(app->edit_buffer) && storage_common_stat(app->storage,furi_string_get_cstr(dir),NULL)==FSE_NOT_EXIST;
            furi_string_free(dir);
            if(!valid) { library_message(app,"Use a new name.\nLetters, digits, spaces,\n- and _; up to 24 chars.\nCheck SD card.",PageMain);return true; }
            memset(&app->card,0,sizeof(app->card));strlcpy(app->card.name,app->edit_buffer,sizeof(app->card.name));
        } else if(app->edit==Note) strlcpy(app->card.notes,app->edit_buffer,sizeof(app->card.notes));
        else strlcpy(app->card.tags,app->edit_buffer,sizeof(app->card.tags));
        app->dirty=true;library_show(app,PageCard);
    } else if(event==AddLink) {
        if(app->card.link_count>=LIBRARY_LINKS) library_message(app,"Eight links per card.\nRemove a link first.",PageCard);
        else if(library_select_file(app,NULL)) {
            const char* path=furi_string_get_cstr(app->path);bool duplicate=false;
            for(unsigned i=0;i<app->card.link_count;i++) if(!strcmp(path,app->card.links[i]))duplicate=true;
            if(!duplicate){strlcpy(app->card.links[app->card.link_count++],path,256);app->dirty=true;}
            library_show(app,PageCard);
        }
    } else if(event==SaveCard) {
        bool ok=library_card_save(app->storage,&app->card);if(ok)app->dirty=false;
        library_message(app,ok?"Card saved.\nLinked files unchanged.":"Save failed.\nPrevious revision retained.",PageCard);
    } else if(event>=LinkBase && event<LinkBase+app->card.link_count) {app->link=event-LinkBase;library_show(app,PageLink);}
    else if(event==RemoveLink && app->link<app->card.link_count) {
        memmove(app->card.links[app->link],app->card.links[app->link+1],(app->card.link_count-app->link-1)*256);
        memset(app->card.links[--app->card.link_count],0,256);app->dirty=true;library_show(app,PageCard);
    } else if(event==Details) {
        LibraryPage back=app->page;
        if(back==PageLink) furi_string_printf(app->text,"%s\n\n%s",app->card.links[app->link],
            storage_file_exists(app->storage,app->card.links[app->link])?"File available":"File missing");
        else furi_string_printf(app->text,"%s\n\nNotes:\n%s\n\nTags:\n%s\nLinks: %lu",app->card.name,app->card.notes,app->card.tags,app->card.link_count);
        library_message(app,furi_string_get_cstr(app->text),back);
    } else if(event==OpenLink) {
        const char* path=app->card.links[app->link];const char* ext=strrchr(path,'.');const char* target=NULL;
        if(ext && !strcmp(ext,".sub"))target="Sub-GHz";
        if(ext && !strcmp(ext,".ir"))target="Infrared";
        if(ext && !strcmp(ext,".nfc"))target="NFC";
        if(app->dirty) library_message(app,"Save card changes\nbefore opening a file.",PageLink);
        else if(!target) library_message(app,"Reference-only file.\nOpen it in its own app.\nNo script is executed.",PageLink);
        else if(!storage_file_exists(app->storage,path))library_message(app,"Linked file is missing.",PageLink);
        else {
            loader_clear_launch_queue(app->loader);
            loader_enqueue_launch(app->loader,target,path,LoaderDeferredLaunchFlagGui);
            view_dispatcher_stop(app->dispatcher);
        }
    } else if(event==EnableHistory || event==DisableHistory) {
        bool ok=false;
        if(event==EnableHistory && library_history_load(app) && storage_simply_mkdir(app->storage,LIBRARY_ROOT) && storage_simply_mkdir(app->storage,FILE_HISTORY_ROOT)) {
            File* file=storage_file_alloc(app->storage);
            ok=storage_file_open(file,FILE_HISTORY_ENABLED,FSAM_WRITE,FSOM_CREATE_NEW) && storage_file_sync(file);
            ok=storage_file_close(file) && ok;storage_file_free(file);
            if(!ok)storage_common_remove(app->storage,FILE_HISTORY_ENABLED);
        } else if(event==DisableHistory)ok=storage_common_remove(app->storage,FILE_HISTORY_ENABLED)==FSE_OK;
        library_message(app,ok?(event==EnableHistory?"History enabled.\nNative SUB / IR / NFC.\n3 versions, 2 MiB max.\nBackups may be private.":"Auto snapshots disabled.\nSaved versions retained."):"History setting failed.\nCheck SD / FW Packages.",PageHistory);
    } else if(event==Snapshot) {
        if(library_history_load(app) && library_select_file(app,".sub|.ir|.nfc")) {
            view_dispatcher_show_loading(app->dispatcher);
            bool ok=app->history->snapshot(app->storage,furi_string_get_cstr(app->path));
            library_message(app,ok?"Snapshot verified.\nOriginal unchanged.":"Snapshot failed.\nOriginal unchanged.\nCheck SD / 2 MiB limit.",PageHistory);
        }
    } else if(event==SelectHistory) {
        if(library_history_load(app)) {
            free(app->sources);app->sources=calloc(32,256);
            view_dispatcher_show_loading(app->dispatcher);
            bool ok=app->history->sources(app->storage,app->sources,&app->count);
            if(!ok || !app->count)library_message(app,ok?"No saved versions yet.":"History read failed.",PageHistory);
            else {
                app->page=PageHistory;submenu_reset(app->menu);submenu_set_header(app->menu,"Saved files (32 max)");
                for(unsigned i=0;i<app->count;i++){const char* name=strrchr(app->sources[i],'/');library_item(app,name?name+1:app->sources[i],SourceBase+i);}
                view_dispatcher_switch_to_view(app->dispatcher,LibraryMenuView);
            }
        }
    } else if(event>=SourceBase && event<SourceBase+app->count && app->sources) {
        furi_string_set(app->path,app->sources[event-SourceBase]);library_show(app,PageVersions);
    } else if(event>=VersionBase && event<VersionBase+4 && app->page==PageVersions) {
        FuriString* dest=furi_string_alloc();view_dispatcher_show_loading(app->dispatcher);
        bool ok=app->history->restore_copy(app->storage,furi_string_get_cstr(app->path),event-VersionBase,dest);
        if(ok)furi_string_printf(app->text,"Restored as new copy:\n%s\n\nCurrent file unchanged.",furi_string_get_cstr(dest));
        library_message(app,ok?furi_string_get_cstr(app->text):"Restore failed.\nBackup missing/corrupt,\nSD error or path too long.\nCurrent file unchanged.",PageVersions);
        furi_string_free(dest);
    } else if(event==KeepEditing)library_show(app,PageCard);
    else if(event==Discard){app->dirty=false;library_show(app,PageMain);}
    else if(event==About)library_message(app,"Device Library\n\nCards link existing files. Removing a link never deletes its file.\n\nHistory is opt-in for native SUB / IR / NFC saves. Three prior versions per file, 2 MiB each. Third-party writers and deletions are not intercepted.\n\nRestore always creates a separate copy. Backups may contain private NFC or remote data; they stay on your SD.\n\n32 cards / history sources per list. No automatic radio actions.",app->page);
    return true;
}
static bool library_back(void* context) {
    Library* app=context;
    if(app->page==PageMain){view_dispatcher_stop(app->dispatcher);return true;}
    if(app->page==PageText)library_show(app,app->back);
    else if(app->page==PageInput)library_show(app,app->edit==NewCard?PageMain:PageCard);
    else if(app->page==PageCard && app->dirty)library_show(app,PageDiscard);
    else if(app->page==PageLink || app->page==PageDiscard)library_show(app,PageCard);
    else if(app->page==PageVersions)library_show(app,PageHistory);
    else library_show(app,PageMain);
    return true;
}
int32_t device_library_app(void* context) {
    UNUSED(context);Library* app=calloc(1,sizeof(*app));
    app->storage=furi_record_open(RECORD_STORAGE);app->dialogs=furi_record_open(RECORD_DIALOGS);
    app->loader=furi_record_open(RECORD_LOADER);app->gui=furi_record_open(RECORD_GUI);
    app->dispatcher=view_dispatcher_alloc();app->menu=submenu_alloc();app->text_view=text_box_alloc();app->input=text_input_alloc();
    app->text=furi_string_alloc();app->path=furi_string_alloc();
    view_dispatcher_set_event_callback_context(app->dispatcher,app);
    view_dispatcher_set_custom_event_callback(app->dispatcher,library_event);
    view_dispatcher_set_navigation_event_callback(app->dispatcher,library_back);
    text_box_set_font(app->text_view,TextBoxFontText);
    view_dispatcher_add_view(app->dispatcher,LibraryMenuView,submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher,LibraryTextView,text_box_get_view(app->text_view));
    view_dispatcher_add_view(app->dispatcher,LibraryInputView,text_input_get_view(app->input));
    view_dispatcher_attach_to_gui(app->dispatcher,app->gui,ViewDispatcherTypeFullscreen);
    library_show(app,PageMain);view_dispatcher_run(app->dispatcher);
    if(app->history_manager)plugin_manager_free(app->history_manager);
    view_dispatcher_remove_view(app->dispatcher,LibraryInputView);view_dispatcher_remove_view(app->dispatcher,LibraryTextView);view_dispatcher_remove_view(app->dispatcher,LibraryMenuView);
    text_input_free(app->input);text_box_free(app->text_view);submenu_free(app->menu);view_dispatcher_free(app->dispatcher);
    furi_string_free(app->text);furi_string_free(app->path);free(app->sources);
    furi_record_close(RECORD_GUI);furi_record_close(RECORD_LOADER);furi_record_close(RECORD_DIALOGS);furi_record_close(RECORD_STORAGE);free(app);return 0;
}
