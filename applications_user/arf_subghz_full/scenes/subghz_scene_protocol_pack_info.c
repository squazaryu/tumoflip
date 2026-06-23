#include "../subghz_i.h"
#include "../helpers/subghz_txrx_i.h"

static void
    subghz_scene_protocol_pack_info_append_filename(FuriString* text, const char* filename) {
    const char* name = filename;
    const char* prefix = "protocol_";
    const char* suffix = ".fal";
    if(strncmp(name, prefix, strlen(prefix)) == 0) name += strlen(prefix);

    size_t name_length = strlen(name);
    const size_t suffix_length = strlen(suffix);
    if(name_length >= suffix_length && strcmp(name + name_length - suffix_length, suffix) == 0) {
        name_length -= suffix_length;
    }

    furi_string_cat_printf(text, "%.*s", (int)name_length, name);
}

void subghz_scene_protocol_pack_info_on_enter(void* context) {
    SubGhz* subghz = context;
    const SubGhzProtocolPackReport* report = subghz_txrx_get_protocol_pack_report(subghz->txrx);
    FuriString* text = furi_string_alloc();

    furi_string_cat_printf(
        text,
        "\e#Protocol Pack\n"
        "Group: %s\n"
        "Plugins: %zu/%zu\n"
        "Protocols: %zu (%zu core)\n"
        "FW API: %u.%u\n"
        "Plugin API: %lu\n",
        subghz_protocol_pack_group_get_name(report->group),
        report->loaded_plugin_count,
        report->expected_plugin_count,
        report->registered_protocol_count,
        report->base_protocol_count,
        report->firmware_api_major,
        report->firmware_api_minor,
        report->plugin_api_version);

    if(report->heap_used >= 1024) {
        furi_string_cat_printf(
            text,
            "Pack RAM: %zu.%zu KB\n",
            report->heap_used / 1024,
            (report->heap_used % 1024) * 10 / 1024);
    } else {
        furi_string_cat_printf(text, "Pack RAM: %zu B\n", report->heap_used);
    }

    if(report->expected_plugin_count == 0) {
        furi_string_cat_str(text, "\nCore decoders only.\nNo external plugins expected.");
    } else {
        furi_string_cat_str(text, "\n");
        for(size_t i = 0; i < report->expected_plugin_count; i++) {
            const SubGhzProtocolPackReportEntry* entry = &report->entries[i];
            furi_string_cat_str(
                text, entry->status == SubGhzProtocolPackStatusLoaded ? "[OK] " : "[ERR] ");
            subghz_scene_protocol_pack_info_append_filename(text, entry->filename);
            furi_string_cat_str(text, "\n  ");

            if(entry->status == SubGhzProtocolPackStatusLoaded && entry->protocol_name) {
                furi_string_cat_str(text, entry->protocol_name);
            } else {
                furi_string_cat_str(text, subghz_protocol_pack_status_get_name(entry->status));
            }
            if(i + 1 < report->expected_plugin_count) furi_string_cat_str(text, "\n");
        }
    }

    widget_add_text_scroll_element(subghz->widget, 2, 0, 126, 64, furi_string_get_cstr(text));
    furi_string_free(text);
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewIdWidget);
}

bool subghz_scene_protocol_pack_info_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void subghz_scene_protocol_pack_info_on_exit(void* context) {
    SubGhz* subghz = context;
    widget_reset(subghz->widget);
}
