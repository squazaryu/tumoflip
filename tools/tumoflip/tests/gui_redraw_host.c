#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define malloc(n) calloc(1, (n)) /* Furi allocations are zero-filled. */
#define furi_check assert
#define furi_assert assert
#define FURI_LOG_W(tag, format, file, line) log_timeout(line)
#define FURI_LOG_I(...) ((void)0)
#define FuriWaitForever 0xffffffffu
enum { FuriStatusOk, FuriStatusError, FuriMutexTypeRecursive, ViewPortOrientationHorizontal,
       ViewOrientationVertical, ViewModelTypeLocking, FuriTimerTypePeriodic, FuriTimerTypeOnce };
typedef struct { unsigned updates; } Gui;
typedef struct { unsigned depth; } FuriMutex;
typedef int Canvas;
typedef int ViewPortOrientation;
typedef void (*ViewPortDrawCallback)(Canvas*, void*);
typedef struct ViewPort {
    Gui* gui; FuriMutex* mutex; bool is_enabled, was_drawn; int orientation;
    uint8_t width, height; ViewPortDrawCallback draw_callback; void* draw_callback_context;
} ViewPort;
static Gui gui;
static bool lock_fails;
static unsigned model_depth, draws;
static unsigned timeout_logs;
static void log_timeout(int line) { assert(line > 0); timeout_logs++; }
static FuriMutex* furi_mutex_alloc(int type) { assert(type == FuriMutexTypeRecursive); return malloc(sizeof(FuriMutex)); }
static int furi_mutex_acquire(FuriMutex* m, unsigned timeout) { (void)timeout; if(lock_fails) return FuriStatusError; m->depth++; return FuriStatusOk; }
static int furi_mutex_release(FuriMutex* m) { assert(m->depth); m->depth--; return FuriStatusOk; }
static void furi_mutex_free(FuriMutex* m) { assert(!m->depth); free(m); }
static void gui_update(Gui* g) { assert(g == &gui); g->updates++; }
static void view_port_setup_canvas_orientation(const ViewPort* p, Canvas* c) { assert(p && c); }
void view_port_free(ViewPort* p);
void view_port_gui_set(ViewPort* p, Gui* g);
static void draw(Canvas* c, void* context) { assert(c); draws++; if(context) { extern void view_port_update(ViewPort*); view_port_update(context); } }
static void free_port(ViewPort* p) { assert(!p->mutex->depth); view_port_gui_set(p, NULL); view_port_free(p); }

typedef struct FuriTimer { void (*callback)(void*); void* context; bool active, freed; unsigned interval; } FuriTimer;
static FuriTimer* timers[8];
static unsigned timer_count;
static FuriTimer* furi_timer_alloc(void (*cb)(void*), int type, void* context) {
    (void)type; assert(timer_count < 8);
    FuriTimer* t = malloc(sizeof(*t)); t->callback = cb; t->context = context; timers[timer_count++] = t; return t;
}
static void furi_timer_start(FuriTimer* t, unsigned interval) { assert(!model_depth && !t->freed); t->active = true; t->interval = interval; }
static void furi_timer_stop(FuriTimer* t) { assert(!model_depth && !t->freed); t->active = false; }
static void furi_timer_free(FuriTimer* t) { assert(!model_depth && !t->active && !t->freed); t->freed = true; }
static void tick(FuriTimer* t) { if(t->active && !t->freed) t->callback(t->context); }
typedef struct View { void* model; void* context; void (*enter)(void*); void (*exit)(void*); unsigned updates; } View;
static View* view_alloc(void) { return malloc(sizeof(View)); }
static void view_free(View* v) { free(v->model); free(v); }
static void view_set_context(View* v, void* context) { v->context = context; }
static void view_allocate_model(View* v, int type, size_t n) { (void)type; v->model = malloc(n); }
static void view_set_orientation(View* v, int o) { (void)v; (void)o; }
static __attribute__((unused)) void view_set_enter_callback(View* v, void (*cb)(void*)) { v->enter = cb; }
static __attribute__((unused)) void view_set_exit_callback(View* v, void (*cb)(void*)) { v->exit = cb; }
#define view_set_draw_callback(...) ((void)0)
#define view_set_input_callback(...) ((void)0)
#define with_view_model(v, declaration, body, update) do { declaration = (v)->model; model_depth++; body; model_depth--; if(update) (v)->updates++; } while(0)
typedef struct { char* current_value_text; char* locked_message; } VariableItem;
typedef int VariableItemArray_t;
typedef struct VariableItemList VariableItemList;
typedef void (*VariableItemListEnterCallback)(void*, uint32_t);
/* VARIABLEITEMLIST_TYPES */
typedef unsigned VariableItemArray_it_t[1];
static VariableItem empty_item;
#define VariableItemArray_init(a) ((a) = 0)
#define VariableItemArray_clear(a) ((a) = 0)
#define VariableItemArray_it(it, a) ((it)[0] = (unsigned)(a))
#define VariableItemArray_end_p(it) ((it)[0] == 0)
#define VariableItemArray_next(it) ((it)[0]--)
#define VariableItemArray_ref(it) ((void)(it), &empty_item)
static void furi_string_free(char* text) { free(text); }
typedef int ButtonMenuItemArray_t;
typedef struct ButtonMenu ButtonMenu;
/* BUTTONMENU_TYPES */
static FuriTimer* button_timer(ButtonMenu* menu) { return /* BUTTON_TIMER */; }
#define ButtonMenuItemArray_init(a) ((a) = 0)
#define ButtonMenuItemArray_clear(a) ((a) = 0)
#define SCROLL_INTERVAL 333

static const int I_BLE_beacon_7x8 = 7, I_Bluetooth_Idle_5x8 = 5, I_Bluetooth_Connected_16x8 = 16;
static uint8_t icon_get_width(const int* icon) { return (uint8_t)*icon; }
#define ICON_SPACER 2
#define BT_TRANSFER_ICON_WIDTH 11
enum { BtStatusOff, BtStatusAdvertising, BtStatusConnected };
typedef struct { bool beacon_active, transfer_active; int status; ViewPort* statusbar_view_port; } Bt;

enum { ST_EXT, ST_INT, STORAGE_COUNT, StorageStatusOK = 0, StorageStatusNotReady,
       StorageStatusNotMounted, StorageStatusNoFS, StorageStatusNotAccessible,
       StorageStatusErrorInternal, StorageEventTypeCardUnmount, StorageEventTypeCardMount,
       StorageEventTypeCardMountError, GuiLayerStatusBarLeft };
typedef int StorageStatus;
typedef struct StorageData StorageData;
typedef struct { void (*tick)(StorageData*); } StorageApi;
struct StorageData { StorageStatus status; StorageApi api; };
typedef struct { int type; } StorageEvent;
typedef struct { ViewPort* view_port; bool enabled; StorageStatus icon_status; } StorageSDGui;
typedef struct { void* message_queue; StorageData storage[STORAGE_COUNT]; StorageSDGui sd_gui; void* pubsub; } Storage;
static int ICON_SD_MOUNTED_VALUE = 11;
#define ICON_SD_MOUNTED (&ICON_SD_MOUNTED_VALUE)
static int initial_status;
static unsigned mount_events, unmount_events, error_events, data_ticks;
static void* furi_message_queue_alloc(unsigned count, size_t size) { assert(count && size); return &gui; }
typedef int StorageMessage;
static void* furi_pubsub_alloc(void) { return &gui; }
static void furi_pubsub_publish(void* pubsub, const StorageEvent* event) { assert(pubsub == &gui); if(event->type == StorageEventTypeCardMount) mount_events++; else if(event->type == StorageEventTypeCardUnmount) unmount_events++; else { assert(event->type == StorageEventTypeCardMountError); error_events++; } }
static void storage_data_init(StorageData* data) { data->status = StorageStatusNotReady; }
static void storage_data_timestamp(StorageData* data) { (void)data; }
static void storage_ext_init(StorageData* data) { data->status = initial_status; }
static void storage_app_sd_icon_draw_callback(Canvas* c, void* context) { (void)c; (void)context; }
#define RECORD_GUI "gui"
static Gui* furi_record_open(const char* record) { assert(!strcmp(record, RECORD_GUI)); return &gui; }
static void furi_record_close(const char* record) { assert(!strcmp(record, RECORD_GUI)); }
static void gui_add_view_port(Gui* g, ViewPort* p, int layer) { assert(g == &gui && layer == GuiLayerStatusBarLeft); p->gui = g; }
static void data_tick(StorageData* data) { assert(data); data_ticks++; }

/* PRODUCTION */

static ViewPort* port(void) { ViewPort* p = view_port_alloc(); p->gui = &gui; p->draw_callback = draw; return p; }
static void viewport_case(void) {
    ViewPort* p = port(); Canvas c = 0;
    assert(p->was_drawn);
    view_port_update(p); assert(gui.updates == 1);
    for(unsigned i = 0; i < 100; i++) view_port_update(p);
    assert(gui.updates == 1);
    view_port_draw(p, &c); assert(draws == 1 && p->was_drawn);
    view_port_update(p); assert(gui.updates == 2);
    /* An uncover path forces a draw independently of suppressed timer ticks. */
    view_port_draw(p, &c); view_port_update(p); assert(gui.updates == 3);
    p->is_enabled = false; view_port_update(p); assert(gui.updates == 3);
    p->is_enabled = true; p->gui = NULL; view_port_update(p); assert(gui.updates == 3);
    p->gui = &gui; p->was_drawn = false; p->draw_callback_context = p;
    view_port_draw(p, &c); assert(gui.updates == 3 && p->was_drawn);
    view_port_update(p); assert(gui.updates == 4);
    p->draw_callback = NULL; view_port_draw(p, &c); assert(p->was_drawn);
    free_port(p);
}
static void mutex_case(void) {
    ViewPort* p = port(); Canvas c = 0; p->was_drawn = false;
    lock_fails = true;
    view_port_update(p); view_port_update(p);
    assert(gui.updates == 2 && !p->was_drawn && !p->mutex->depth);
    assert(view_port_get_orientation(p) == ViewPortOrientationHorizontal);
    view_port_enabled_set(p, false);
    view_port_enabled_set(p, true);
    assert(gui.updates == 4 && !p->was_drawn && !p->mutex->depth);
    view_port_draw(p, &c);
    assert(gui.updates == 5 && !p->was_drawn && !p->mutex->depth);
    lock_fails = false; view_port_draw(p, &c); assert(p->was_drawn);
    view_port_update(p); assert(gui.updates == 6);
    assert(timeout_logs == 6);
    free_port(p);
}
static void getters_case(void) {
    ViewPort* p = port();
    assert(view_port_get_width(p) == 0 && view_port_get_height(p) == 0 && view_port_is_enabled(p));
    view_port_set_width(p, 19); view_port_set_height(p, 8);
    assert(view_port_get_width(p) == 19 && view_port_get_height(p) == 8);
    assert(view_port_get_orientation(p) == ViewPortOrientationHorizontal && !p->mutex->depth);
    view_port_enabled_set(p, false); assert(!view_port_is_enabled(p));
    free_port(p);
}
static void list_case(void) {
    VariableItemList* list = variable_item_list_alloc();
    VariableItemListModel* model = list->view->model;
    assert(!list->scroll_timer->active && list->view->enter && list->view->exit);
    model->scroll_counter = 20;
    list->view->enter(list); assert(list->scroll_timer->active && list->scroll_timer->interval == 333 && !model->scroll_counter);
    tick(list->scroll_timer); assert(model->scroll_counter == 1);
    list->view->exit(list); tick(list->scroll_timer); assert(model->scroll_counter == 1);
    list->view->enter(list); assert(!model->scroll_counter);
    tick(list->locked_timer);
    variable_item_list_locked_timer_callback(list);
    variable_item_list_free(list);
}
static void buttons_case(void) {
    ButtonMenu* menu = button_menu_alloc(); ButtonMenuModel* model = menu->view->model;
    FuriTimer* timer = button_timer(menu);
    assert(!timer->active && menu->view->enter && menu->view->exit);
    model->scroll_counter = 4;
    menu->view->enter(menu); tick(timer); assert(model->scroll_counter == 1);
    menu->view->exit(menu); tick(timer); assert(model->scroll_counter == 1);
    menu->view->enter(menu); assert(!model->scroll_counter);
    button_menu_free(menu);
}
static void bt_case(void) {
    ViewPort* p = port();
    for(int status = BtStatusOff; status <= BtStatusConnected; status++) {
        for(unsigned flags = 0; flags < 4; flags++) {
            Bt bt = {.status = status, .beacon_active = flags & 1, .transfer_active = flags & 2, .statusbar_view_port = p};
            p->is_enabled = p->was_drawn = true; unsigned before = gui.updates;
            bt_statusbar_update(&bt);
            unsigned width = bt.beacon_active ? 9 : 0;
            if(status == BtStatusAdvertising) width += 5;
            if(status == BtStatusConnected) width += 16;
            if(bt.transfer_active) width += (width ? 2 : 0) + 11;
            assert(p->is_enabled == (width != 0));
            assert(gui.updates == before + 1);
            if(width) assert(p->width == width);
        }
    }
    free_port(p);
}
static void storage_case(void) {
    initial_status = StorageStatusNoFS; Storage* s = storage_app_alloc();
    assert(s->sd_gui.icon_status == initial_status);
    free_port(s->sd_gui.view_port); free(s);
    initial_status = StorageStatusOK; s = storage_app_alloc();
    s->storage[ST_INT].api.tick = data_tick;
    s->sd_gui.view_port->was_drawn = true;
    s->storage[ST_EXT].status = StorageStatusErrorInternal;
    unsigned before = gui.updates; storage_tick(s);
    assert(gui.updates == before + 1 && !mount_events && !error_events);
    assert(s->sd_gui.icon_status == StorageStatusErrorInternal && data_ticks == 1);
    s->sd_gui.view_port->was_drawn = true; s->storage[ST_EXT].status = StorageStatusOK;
    storage_tick(s); assert(gui.updates == before + 2 && !mount_events);
    s->storage[ST_EXT].status = StorageStatusNotReady; storage_tick(s); assert(unmount_events == 1);
    s->sd_gui.view_port->was_drawn = true; s->storage[ST_EXT].status = StorageStatusNoFS;
    storage_tick(s); assert(error_events == 1);
    s->storage[ST_EXT].status = StorageStatusNotReady; storage_tick(s);
    s->storage[ST_EXT].status = StorageStatusOK; storage_tick(s); assert(mount_events == 1);
    free_port(s->sd_gui.view_port); free(s);
}
int main(int argc, char** argv) {
    assert(argc == 2);
    if(!strcmp(argv[1], "viewport")) viewport_case();
    else if(!strcmp(argv[1], "mutex")) mutex_case();
    else if(!strcmp(argv[1], "getters")) getters_case();
    else if(!strcmp(argv[1], "list")) list_case();
    else if(!strcmp(argv[1], "buttons")) buttons_case();
    else if(!strcmp(argv[1], "bt")) bt_case();
    else if(!strcmp(argv[1], "storage")) storage_case();
    else assert(false);
    for(unsigned i = 0; i < timer_count; i++) { assert(timers[i]->freed); free(timers[i]); }
    return 0;
}
