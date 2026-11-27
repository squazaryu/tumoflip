#include "desktop_view_panel.h"
#include "../desktop_panel_i.h"
#include <furi.h>
#include <gui/canvas_i.h>
#include <gui/elements.h>
#include <assets_icons.h>

static void desktop_panel_badge(
    Canvas* canvas,
    uint8_t center,
    const Icon* icon,
    const char* label,
    const char* value) {
    canvas_draw_rframe(canvas, center - 21, 25, 42, 26, 3);
    canvas_set_font(canvas, strlen(value) > 4 ? FontBatteryPercent : FontPrimary);
    canvas_draw_str_aligned(canvas, center, 36, AlignCenter, AlignBottom, value);
    canvas_set_font(canvas, FontSecondary);
    const int32_t x = center - (icon_get_width(icon) + 2 + canvas_string_width(canvas, label)) / 2;
    canvas_draw_icon(canvas, x, 47 - icon_get_height(icon), icon);
    canvas_draw_str(canvas, x + icon_get_width(icon) + 2, 47, label);
}

static void desktop_panel_hero(
    Canvas* canvas,
    const Icon* icon,
    const char* title,
    const char* line1,
    const char* line2) {
    canvas_draw_icon(
        canvas, (36 - icon_get_width(icon)) / 2, 38 - icon_get_height(icon) / 2, icon);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 40, 32, title);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 40, 41, line1);
    canvas_draw_str(canvas, 40, 49, line2);
}

static void desktop_panel_draw_page(Canvas* canvas, const DesktopPanelModel* model) {
    const DesktopPanelSnapshot* status = &model->snapshot;
    const uint8_t page = model->page < 3 ? model->page : 0;
    static const char* const titles[] = {"Status", "Idle & sleep", "Last fault"};
    char value[24];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 21, titles[page]);
    canvas_set_font(canvas, FontSecondary);
    snprintf(value, sizeof(value), "%u/3", page + 1U);
    canvas_draw_str_aligned(canvas, 126, 21, AlignRight, AlignBottom, value);

    if(page == 0) {
        desktop_panel_badge(
            canvas,
            21,
            &I_SDcardMounted_11x8,
            "SD",
            status->sd_state == 2 ? "Ready" :
            status->sd_state == 1 ? "Not ready" :
                                    "?");
        /* Active means radio enabled, not a verified phone/RPC connection. */
        desktop_panel_badge(canvas, 64, &I_Bluetooth_Idle_5x8, "BT", status->bt_on ? "On" : "Off");
        snprintf(value, sizeof(value), "%uK", status->heap_kb);
        desktop_panel_badge(canvas, 107, &I_Rpc_active_7x8, "RAM", value);
    } else if(page == 1) {
        char lock[24];
        if(status->auto_lock_s)
            snprintf(lock, sizeof(lock), "Auto-lock: %us", status->auto_lock_s);
        else
            strlcpy(lock, "Auto-lock: Off", sizeof(lock));
        snprintf(value, sizeof(value), "Sleep holds: %u", status->sleep_holds);
        desktop_panel_hero(canvas, &A_Clock_14, "Desktop idle", lock, value);
    } else if(status->fault_valid) {
        char title[sizeof(status->fault_name)];
        memcpy(title, status->fault_name, sizeof(title));
        title[sizeof(title) - 1] = '\0';
        canvas_set_font(canvas, FontPrimary);
        size_t length = strlen(title);
        while(length && canvas_string_width(canvas, title) > 86)
            title[--length] = '\0';
        snprintf(value, sizeof(value), "Commit: %08lX", (unsigned long)status->fault_commit);
        desktop_panel_hero(canvas, &I_Warning_30x23, title, value, "Read-only record");
    } else {
        desktop_panel_hero(
            canvas, &I_FaceNormal_29x14, "No record", "Not available", "Read-only status");
    }
}

void desktop_panel_draw(Canvas* canvas, void* context) {
    DesktopPanelModel model;
    {
        FURI_CRITICAL_ENTER();
        model = *(const DesktopPanelModel*)context;
        FURI_CRITICAL_EXIT();
    }
    canvas_clear(canvas);
    const int32_t x = canvas->offset_x, y = canvas->offset_y;
    const size_t width = canvas_width(canvas), height = canvas_height(canvas);
    const int16_t offset = model.offset < -112 ? -112 : model.offset > 112 ? 112 : model.offset;
    if(offset) canvas_frame_set(canvas, x + offset, y, width, height);
    desktop_panel_draw_page(canvas, &model);
    if(offset) canvas_frame_set(canvas, x, y, width, height);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_icon(canvas, 4, 56, &I_ButtonLeftSmall_3x5);
    canvas_draw_icon(canvas, 121, 56, &I_ButtonRightSmall_3x5);
    elements_button_center(canvas, "Apps");
}
