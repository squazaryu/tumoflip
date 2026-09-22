#include "corpus_ui.h"
#include "corpus_runner.h"
#include <dialogs/dialogs.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_box.h>
#include <gui/elements.h>
#include <stdio.h>

enum {
    CorpusMenu = 50,
    CorpusText,
    CorpusProgress
};
enum {
    CorpusAdd = 1000,
    CorpusCheck,
    CorpusSave,
    CorpusExport,
    CorpusAbout,
    CorpusCancel,
    CorpusPack = 1020
};
typedef struct {
    uint8_t progress;
} CorpusProgressModel;
struct CorpusUi {
    CorpusRunner* runner;
    ViewDispatcher* dispatcher;
    DialogsApp* dialogs;
    Submenu* menu;
    TextBox* text;
    View* progress;
    FuriString* path;
    uint32_t parent;
    bool running;
    bool choosing;
};

static void corpus_ui_result(CorpusUi* ui, const char* text) {
    text_box_set_text(ui->text, text);
    text_box_set_focus(ui->text, TextBoxFocusStart);
    view_dispatcher_switch_to_view(ui->dispatcher, CorpusText);
}
static void corpus_ui_action(void* context, uint32_t event) {
    CorpusUi* ui = context;
    view_dispatcher_send_custom_event(ui->dispatcher, event);
}
static uint32_t corpus_ui_previous(void* context) {
    UNUSED(context);
    return CorpusMenu;
}
static uint32_t corpus_ui_parent(void* context) {
    UNUSED(context);
    return 0;
}

void corpus_ui_show(CorpusUi* ui) {
    ui->choosing = false;
    submenu_reset(ui->menu);
    submenu_set_header(ui->menu, "Decoder References");
    submenu_add_item(ui->menu, "Add RAW reference", CorpusAdd, corpus_ui_action, ui);
    submenu_add_item(ui->menu, "Check all references", CorpusCheck, corpus_ui_action, ui);
    if(corpus_runner_can_save(ui->runner))
        submenu_add_item(ui->menu, "Save as reference", CorpusSave, corpus_ui_action, ui);
    submenu_add_item(ui->menu, "Export last report", CorpusExport, corpus_ui_action, ui);
    submenu_add_item(ui->menu, "About / limits", CorpusAbout, corpus_ui_action, ui);
    view_dispatcher_switch_to_view(ui->dispatcher, CorpusMenu);
}

static void corpus_ui_draw(Canvas* canvas, void* model) {
    CorpusProgressModel* m = model;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Offline decode");
    canvas_set_font(canvas, FontSecondary);
    char progress[32];
    snprintf(progress, sizeof(progress), "Current file: %u%%", m->progress);
    canvas_draw_str(canvas, 2, 27, progress);
    elements_progress_bar(canvas, 2, 34, 124, m->progress / 100.0f);
    canvas_draw_str(canvas, 2, 59, "Back: cancel  /  No RF");
}
static bool corpus_ui_input(InputEvent* input, void* context) {
    if(input->key == InputKeyBack && input->type == InputTypeShort)
        corpus_ui_action(context, CorpusCancel);
    return true;
}
static void corpus_ui_start(CorpusUi* ui, bool running) {
    ui->running = running;
    if(running) {
        with_view_model(ui->progress, CorpusProgressModel * model, { model->progress = 0; }, true);
        view_dispatcher_switch_to_view(ui->dispatcher, CorpusProgress);
    } else
        corpus_ui_result(ui, corpus_runner_text(ui->runner));
}
void corpus_ui_tick(CorpusUi* ui) {
    if(!ui->running) return;
    ui->running = corpus_runner_step(ui->runner);
    if(ui->running) {
        with_view_model(
            ui->progress,
            CorpusProgressModel * model,
            { model->progress = corpus_runner_progress(ui->runner); },
            true);
    } else {
        // Rebuild now so Back from results exposes Save only for an intact, decoded candidate.
        corpus_ui_show(ui);
        corpus_ui_result(ui, corpus_runner_text(ui->runner));
    }
}
bool corpus_ui_event(CorpusUi* ui, uint32_t event) {
    if(event < CorpusAdd) return false;
    if(ui->running) {
        if(event == CorpusCancel) {
            corpus_runner_cancel(ui->runner);
            ui->running = false;
            corpus_ui_show(ui);
            corpus_ui_result(ui, corpus_runner_text(ui->runner));
        }
        return true;
    }
    if(event == CorpusAdd) {
        furi_string_set(ui->path, EXT_PATH("subghz"));
        DialogsFileBrowserOptions options;
        dialog_file_browser_set_basic_options(&options, ".sub", NULL);
        options.base_path = EXT_PATH("subghz");
        if(dialog_file_browser_show(ui->dialogs, ui->path, ui->path, &options)) {
            submenu_reset(ui->menu);
            submenu_set_header(ui->menu, "Choose Protocol Pack");
            for(unsigned i = 0; i < SubGhzProtocolPackGroupCount; i++)
                submenu_add_item(
                    ui->menu,
                    subghz_protocol_pack_group_get_name(i),
                    CorpusPack + i,
                    corpus_ui_action,
                    ui);
            ui->choosing = true;
        }
    } else if(
        event >= CorpusPack && event < CorpusPack + SubGhzProtocolPackGroupCount && ui->choosing) {
        ui->choosing = false;
        corpus_ui_start(
            ui, corpus_runner_add(ui->runner, furi_string_get_cstr(ui->path), event - CorpusPack));
    } else if(event == CorpusCheck)
        corpus_ui_start(ui, corpus_runner_check(ui->runner));
    else if(event == CorpusSave) {
        bool ok = corpus_runner_save(ui->runner);
        corpus_ui_show(ui);
        corpus_ui_result(
            ui,
            ok ?
                corpus_runner_text(ui->runner) :
                "Cannot save reference.\nCheck SD and the 32-reference limit.\nExisting references were not replaced.");
    } else if(event == CorpusExport)
        corpus_ui_result(
            ui,
            corpus_runner_export(ui->runner) ?
                "Report saved to\napps_data/tumo_acceptance_suite/decoder_*.txt" :
                "No report / export failed.");
    else if(event == CorpusAbout)
        corpus_ui_result(
            ui,
            "Decoder References\n\n1. Add a RAW .sub file.\n2. Choose its Protocol Pack.\n3. Inspect the decoded protocols and frame count.\n4. Back > Save as reference.\n5. After updating: Check all references.\n\n"
            "32 references; 4 MiB / 500k pulses per file; 120 seconds per file.\n"
            "Input and serialized frames use SHA-256. Changed files are not decoder regressions.\n"
            "Keep original files and keystores. No RF is activated. Hardware reception/transmission is NOT tested.");
    return true;
}
CorpusUi* corpus_ui_alloc(Storage* storage, ViewDispatcher* dispatcher, uint32_t parent_view) {
    CorpusUi* ui = calloc(1, sizeof(*ui));
    ui->runner = corpus_runner_alloc(storage);
    ui->dispatcher = dispatcher;
    ui->parent = parent_view;
    ui->dialogs = furi_record_open(RECORD_DIALOGS);
    ui->path = furi_string_alloc();
    ui->menu = submenu_alloc();
    // Submenu owns the View context; replacing it would corrupt its input handler.
    view_set_previous_callback(submenu_get_view(ui->menu), corpus_ui_parent);
    ui->text = text_box_alloc();
    text_box_set_font(ui->text, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(ui->text), corpus_ui_previous);
    ui->progress = view_alloc();
    view_allocate_model(ui->progress, ViewModelTypeLocking, sizeof(CorpusProgressModel));
    view_set_context(ui->progress, ui);
    view_set_draw_callback(ui->progress, corpus_ui_draw);
    view_set_input_callback(ui->progress, corpus_ui_input);
    view_dispatcher_add_view(dispatcher, CorpusMenu, submenu_get_view(ui->menu));
    view_dispatcher_add_view(dispatcher, CorpusText, text_box_get_view(ui->text));
    view_dispatcher_add_view(dispatcher, CorpusProgress, ui->progress);
    return ui;
}
void corpus_ui_free(CorpusUi* ui) {
    view_dispatcher_remove_view(ui->dispatcher, CorpusProgress);
    view_dispatcher_remove_view(ui->dispatcher, CorpusText);
    view_dispatcher_remove_view(ui->dispatcher, CorpusMenu);
    corpus_runner_free(ui->runner);
    view_free(ui->progress);
    text_box_free(ui->text);
    submenu_free(ui->menu);
    furi_string_free(ui->path);
    furi_record_close(RECORD_DIALOGS);
    free(ui);
}
