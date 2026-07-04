/*!
 *  @file flipper-xremote/views/xremote_common_view.h
    @license This project is released under the GNU GPLv3 License
 *  @copyright (c) 2023 Sandro Kalatozishvili (s.kalatoz@gmail.com)
 *
 * @brief Common view and canvas functionality shared between the pages.
 */

#pragma once

#include <furi.h>
#include <gui/view.h>
#include <gui/elements.h>

#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <infrared_transmit.h>
#include <xc_icons.h>

#include <dolphin/dolphin.h>

#include "../infrared/infrared_remote.h"

#define XREMOTE_BUTTON_COUNT 60
#define XREMOTE_NAME_MAX     32

#define XREMOTE_COMMAND_POWER         "Power"
#define XREMOTE_COMMAND_EJECT         "Eject"
#define XREMOTE_COMMAND_SETUP         "Setup"
#define XREMOTE_COMMAND_INPUT         "Input"
#define XREMOTE_COMMAND_MENU          "Menu"
#define XREMOTE_COMMAND_LIST          "List"
#define XREMOTE_COMMAND_INFO          "Info"
#define XREMOTE_COMMAND_BACK          "Back"
#define XREMOTE_COMMAND_OK            "Ok"
#define XREMOTE_COMMAND_UP            "Up"
#define XREMOTE_COMMAND_DOWN          "Down"
#define XREMOTE_COMMAND_LEFT          "Left"
#define XREMOTE_COMMAND_RIGHT         "Right"
#define XREMOTE_COMMAND_JUMP_FORWARD  "Next"
#define XREMOTE_COMMAND_JUMP_BACKWARD "Prev"
#define XREMOTE_COMMAND_FAST_FORWARD  "Fast_fo"
#define XREMOTE_COMMAND_FAST_BACKWARD "Fast_ba"
#define XREMOTE_COMMAND_PLAY_PAUSE    "Play_pa"
#define XREMOTE_COMMAND_PAUSE         "Pause"
#define XREMOTE_COMMAND_PLAY          "Play"
#define XREMOTE_COMMAND_STOP          "Stop"
#define XREMOTE_COMMAND_MUTE          "Mute"
#define XREMOTE_COMMAND_MODE          "Mode"
#define XREMOTE_COMMAND_VOL_UP        "Vol_up"
#define XREMOTE_COMMAND_VOL_DOWN      "Vol_dn"
#define XREMOTE_COMMAND_NEXT_CHAN     "Ch_next"
#define XREMOTE_COMMAND_PREV_CHAN     "Ch_prev"
#define XREMOTE_COMMAND_OFF           "Off"
#define XREMOTE_COMMAND_DRY           "Dh"
#define XREMOTE_COMMAND_COOL_HI       "Cool_hi"
#define XREMOTE_COMMAND_COOL_LO       "Cool_lo"
#define XREMOTE_COMMAND_HEAT_HI       "Heat_hi"
#define XREMOTE_COMMAND_HEAT_LO       "Heat_lo"
#define XREMOTE_COMMAND_POWER_ON      "Power_on"
#define XREMOTE_COMMAND_POWER_OFF     "Power_off"
#define XREMOTE_COMMAND_BRIGHTNESS_UP "Brightness_up"
#define XREMOTE_COMMAND_BRIGHTNESS_DN "Brightness_dn"
#define XREMOTE_COMMAND_RED           "Red"
#define XREMOTE_COMMAND_GREEN         "Green"
#define XREMOTE_COMMAND_BLUE          "Blue"
#define XREMOTE_COMMAND_WHITE         "White"
#define XREMOTE_COMMAND_SPEED_UP      "Speed_up"
#define XREMOTE_COMMAND_SPEED_DN      "Speed_dn"
#define XREMOTE_COMMAND_ROTATE        "Rotate"
#define XREMOTE_COMMAND_TIMER         "Timer"
#define XREMOTE_COMMAND_SMART         "Smart"
#define XREMOTE_COMMAND_TEMP_UP       "Temp_up"
#define XREMOTE_COMMAND_TEMP_DOWN     "Temp_dn"
#define XREMOTE_COMMAND_SUPER         "Super"
#define XREMOTE_COMMAND_FAN_SPEED     "Fan_speed"
#define XREMOTE_COMMAND_I_FEEL        "I_feel"
#define XREMOTE_COMMAND_SLEEP         "Sleep"
#define XREMOTE_COMMAND_SWING_V       "Swing_v"
#define XREMOTE_COMMAND_SWING_H       "Swing_h"
#define XREMOTE_COMMAND_CLOCK         "Clock"
#define XREMOTE_COMMAND_TIMER_ON      "Timer_on"
#define XREMOTE_COMMAND_TIMER_OFF     "Timer_off"
#define XREMOTE_COMMAND_QUIET         "Quiet"
#define XREMOTE_COMMAND_DIMMER        "Dimmer"
#define XREMOTE_COMMAND_ECONOMY       "Economy"

typedef enum {
    XRemoteRemoteTypeGeneric,
    XRemoteRemoteTypeTV,
    XRemoteRemoteTypeAudio,
    XRemoteRemoteTypeProjector,
    XRemoteRemoteTypeLEDs,
    XRemoteRemoteTypeFan,
    XRemoteRemoteTypeAC,
    XRemoteRemoteTypeCount,
} XRemoteRemoteType;

typedef enum {
    XRemoteEventReserved = 200,
    XRemoteEventSignalReceived,
    XRemoteEventSignalFinish,
    XRemoteEventSignalSave,
    XRemoteEventSignalRetry,
    XRemoteEventSignalSend,
    XRemoteEventSignalSkip,
    XRemoteEventSignalAskExit,
    XRemoteEventSignalExit
} XRemoteEvent;

typedef enum {
    /* Navigation */
    XRemoteIconOk,
    XRemoteIconEnter,
    XRemoteIconBack,
    XRemoteIconArrowUp,
    XRemoteIconArrowDown,
    XRemoteIconArrowLeft,
    XRemoteIconArrowRight,

    /* Playback */
    XRemoteIconPlay,
    XRemoteIconPause,
    XRemoteIconStop,
    XRemoteIconPlayPause,
    XRemoteIconFastForward,
    XRemoteIconFastBackward,
    XRemoteIconJumpForward,
    XRemoteIconJumpBackward
} XRemoteIcon;

typedef struct {
    void* context;
    bool ok_pressed;
    bool back_pressed;
    bool up_pressed;
    bool down_pressed;
    bool left_pressed;
    bool right_pressed;
    bool hold;
} XRemoteViewModel;

typedef enum {
    XRemoteViewNone,
    XRemoteViewSignal,
    XRemoteViewTextInput,
    XRemoteViewDialogExit,

    /* Main page */
    XRemoteViewSubmenu,
    XRemoteViewLearn,
    XRemoteViewLearnCapture,
    XRemoteViewDesigner,
    XRemoteViewDesignerMap,
    XRemoteViewSaved,
    XRemoteViewAnalyzer,
    XRemoteViewSettings,
    XRemoteViewAbout,

    /* Remote app */
    XRemoteViewIRSubmenu,
    XRemoteViewIRGeneral,
    XRemoteViewIRControl,
    XRemoteViewIRPlayback,
    XRemoteViewIRNavigation,
    XRemoteViewIRCustomPage,
    XRemoteViewIRCustomEditPage,
    XRemoteViewIRAllButtons
} XRemoteViewID;

typedef struct XRemoteView XRemoteView;
typedef void (*XRemoteClearCallback)(void* context);
typedef void (*XRemoteViewDrawFunction)(Canvas*, XRemoteViewModel*);

typedef XRemoteView* (*XRemoteViewAllocator)(void* app_ctx);
typedef XRemoteView* (*XRemoteViewAllocator2)(void* app_ctx, void* model_ctx);

const char* xremote_button_get_name(int index);
int xremote_button_get_index(const char* name);

const char* xremote_remote_type_get_name(XRemoteRemoteType type);
const char* xremote_remote_type_get_menu_name(XRemoteRemoteType type);
XRemoteRemoteType xremote_remote_type_from_name(const char* name);
uint32_t xremote_remote_type_get_button_count(XRemoteRemoteType type);
const char* xremote_remote_type_get_button_name(XRemoteRemoteType type, uint32_t index);
int32_t xremote_remote_type_get_button_index(XRemoteRemoteType type, const char* name);

void xremote_canvas_draw_header(Canvas* canvas, ViewOrientation orient, const char* section);
void xremote_canvas_draw_exit_footer(Canvas* canvas, ViewOrientation orient, const char* text);

void xremote_canvas_draw_icon(Canvas* canvas, uint8_t x, uint8_t y, XRemoteIcon icon);
void xremote_canvas_draw_button(
    Canvas* canvas,
    bool pressed,
    uint8_t x,
    uint8_t y,
    XRemoteIcon icon);
void xremote_canvas_draw_button_png(
    Canvas* canvas,
    bool pressed,
    uint8_t x,
    uint8_t y,
    const Icon* icon);
void xremote_canvas_draw_button_wide(
    Canvas* canvas,
    bool pressed,
    uint8_t x,
    uint8_t y,
    const char* text,
    XRemoteIcon icon);
void xremote_canvas_draw_button_size(
    Canvas* canvas,
    bool pressed,
    uint8_t x,
    uint8_t y,
    uint8_t xy,
    char* text,
    XRemoteIcon icon);
void xremote_canvas_draw_frame(
    Canvas* canvas,
    bool pressed,
    uint8_t x,
    uint8_t y,
    uint8_t xl,
    const char* text);

XRemoteView*
    xremote_view_alloc(void* app_ctx, ViewInputCallback input_cb, ViewDrawCallback draw_cb);
XRemoteView* xremote_view_alloc_empty();
void xremote_view_free(XRemoteView* rview);

InfraredRemoteButton* xremote_view_get_button_by_name(XRemoteView* rview, const char* name);
bool xremote_view_press_button(XRemoteView* rview, InfraredRemoteButton* button);
bool xremote_view_send_ir_msg_by_name(XRemoteView* rview, const char* name);

void xremote_view_model_context_set(XRemoteView* rview, void* model_ctx);
void xremote_view_clear_context(XRemoteView* rview);

void xremote_view_set_app_context(XRemoteView* rview, void* app_ctx);
void xremote_view_set_context(XRemoteView* rview, void* context, XRemoteClearCallback on_clear);
void xremote_view_set_view(XRemoteView* rview, View* view);

void* xremote_view_get_app_context(XRemoteView* rview);
void* xremote_view_get_context(XRemoteView* rview);
View* xremote_view_get_view(XRemoteView* rview);
