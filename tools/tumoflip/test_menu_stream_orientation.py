#!/usr/bin/env python3
"""Host-execute the menu draw and canvas commit path, including RPC frame metadata.

Rendering primitives are fakes: this checks orientation/lifecycle, not LCD pixels.
The production draw callback, both rotated renderers and canvas_commit are used
unchanged. GUI resets the orientation before each fullscreen viewport draw.
"""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

try:
    from .test_main_menu_layouts import function_definition
except ImportError:
    from test_main_menu_layouts import function_definition


ROOT = Path(__file__).resolve().parents[2]


class MenuStreamOrientationTest(unittest.TestCase):
    def test_fullscreen_frames_keep_draw_orientation_until_commit(self) -> None:
        menu = (ROOT / "applications/services/gui/modules/menu.c").read_text()
        canvas = (ROOT / "applications/services/gui/canvas.c").read_text()
        functions = "\n\n".join(
            function_definition(menu, name)
            for name in (
                "menu_draw_vertical",
                "menu_draw_wii_vertical",
                "menu_draw_callback",
            )
        ) + "\n" + function_definition(canvas, "canvas_commit")
        harness = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    CanvasOrientationHorizontal, CanvasOrientationHorizontalFlip,
    CanvasOrientationVertical, CanvasOrientationVerticalFlip,
} CanvasOrientation;
typedef enum {
    MenuStyleList, MenuStyleWii, MenuStyleDsi, MenuStyleVertical,
    MenuStyleWiiVertical, MenuStyleWiiClassic, MenuStyleCount,
} MenuStyle;
enum { FontSecondary, ColorWhite, ColorBlack, AlignCenter, AlignBottom, AlignLeft };
typedef struct {
    void (*callback)(uint8_t*, size_t, CanvasOrientation, void*);
    void* context;
} CanvasCallbackPair;
typedef struct {
    int fb;
    CanvasOrientation orientation;
    CanvasOrientation painted_orientation;
    size_t draw_calls;
    uint8_t buffer[1024];
    CanvasCallbackPair canvas_callback_pair[1];
} Canvas;
typedef struct { const char* label; } MenuItem;
typedef struct { MenuItem data[40]; size_t count; } MenuItemArray_t;
typedef struct {
    MenuItemArray_t items;
    size_t position;
    size_t vertical_offset;
    MenuStyle style;
} MenuModel;

#define furi_check(x) assert(x)
#define M_EACH(p, array, type) \
    (CanvasCallbackPair* p = (array); p < (array) + 1; ++p)
static size_t MenuItemArray_size(MenuItemArray_t items) { return items.count; }
static MenuItem* MenuItemArray_get(MenuItemArray_t* items, size_t pos) {
    assert(pos < items->count);
    return &items->data[pos];
}
#define MenuItemArray_get(items, pos) MenuItemArray_get(&(items), pos)
static void canvas_set_orientation(Canvas* c, CanvasOrientation o) { c->orientation = o; }
static CanvasOrientation canvas_get_orientation(Canvas* c) { return c->orientation; }
static void canvas_clear(Canvas* c) { c->draw_calls = 0; }
static void canvas_set_font(Canvas* c, int f) { (void)c; (void)f; }
static void canvas_set_color(Canvas* c, int v) { (void)c; (void)v; }
static void paint(Canvas* c) {
    if(c->draw_calls) assert(c->painted_orientation == c->orientation);
    c->painted_orientation = c->orientation;
    c->draw_calls++;
}
static void elements_slightly_rounded_box(Canvas* c, int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h; paint(c);
}
static void elements_frame(Canvas* c, int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h; paint(c);
}
static void elements_scrollbar(Canvas* c, size_t pos, size_t count) {
    (void)pos; (void)count; paint(c);
}
static void menu_centered_icon(Canvas* c, MenuItem* item, int x, int y, int w, int h) {
    assert(item); (void)x; (void)y; (void)w; (void)h; paint(c);
}
static const char* menu_compact_name(const char* name) { return name; }
static void menu_draw_fit_label(
    Canvas* c, MenuModel* m, const char* text, int x, int y, int a, int b, size_t width) {
    (void)m; assert(text); (void)x; (void)y; (void)a; (void)b; (void)width; paint(c);
}
static void menu_draw_list(Canvas* c, MenuModel* m) { (void)m; paint(c); }
static void menu_draw_signal_matrix(Canvas* c, MenuModel* m) { (void)m; paint(c); }
static void menu_draw_signal_rail(Canvas* c, MenuModel* m) { (void)m; paint(c); }
static void menu_draw_wii_classic(Canvas* c, MenuModel* m) { (void)m; paint(c); }
static void u8g2_SendBuffer(int* fb) { (void)fb; }
static void canvas_lock(Canvas* c) { (void)c; }
static void canvas_unlock(Canvas* c) { (void)c; }
static uint8_t* canvas_get_buffer(Canvas* c) { return c->buffer; }
static size_t canvas_get_buffer_size(Canvas* c) { return sizeof(c->buffer); }

@FUNCTIONS@

typedef struct { size_t frames; CanvasOrientation orientation; } Receiver;
static void receive(uint8_t* data, size_t size, CanvasOrientation orientation, void* ctx) {
    assert(data && size == 1024);
    Receiver* receiver = ctx;
    receiver->frames++;
    receiver->orientation = orientation;
}
int main(void) {
    Receiver receiver = {0};
    Canvas c = {.canvas_callback_pair = {{.callback = receive, .context = &receiver}}};
    MenuModel model = {0};
    for(size_t i = 0; i < 40; i++) model.items.data[i].label = "Test item";
    for(size_t count = 0; count <= 40; count++) {
        model.items.count = count;
        for(size_t pos = 0; pos < (count ? count : 1); pos++) {
            model.position = pos;
            for(int style = 0; style < MenuStyleCount; style++) {
                model.style = style;
                for(int flipped = 0; flipped < 2; flipped++) {
                    // gui_redraw_fs + view_port_setup_canvas_orientation reset each frame.
                    CanvasOrientation initial = flipped ? CanvasOrientationHorizontalFlip :
                                                          CanvasOrientationHorizontal;
                    canvas_set_orientation(&c, initial);
                    menu_draw_callback(&c, &model);
                    assert(c.draw_calls > 0);
                    size_t previous = receiver.frames;
                    canvas_commit(&c);
                    assert(receiver.frames == previous + 1);
                    CanvasOrientation expected = count &&
                        (style == MenuStyleVertical || style == MenuStyleWiiVertical) ?
                        CanvasOrientationVertical : initial;
                    if(receiver.orientation != expected ||
                       receiver.orientation != c.painted_orientation) {
                        fprintf(stderr, "frame orientation mismatch: style=%d count=%zu "
                                "position=%zu flipped=%d drawn=%d sent=%d expected=%d\n",
                                style, count, pos, flipped, c.painted_orientation,
                                receiver.orientation, expected);
                        return 1;
                    }
                    // Leaving a rotated menu must not rotate the next ordinary viewport.
                    canvas_set_orientation(&c, initial);
                    model.style = MenuStyleList;
                    menu_draw_callback(&c, &model);
                    canvas_commit(&c);
                    assert(receiver.orientation == initial);
                    model.style = style;
                }
            }
        }
    }
    printf("orientation frames checked: %zu\n", receiver.frames);
    return 0;
}
""".replace("@FUNCTIONS@", functions)
        compiler = shutil.which("cc")
        self.assertIsNotNone(compiler, "host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="menu-stream-orientation-") as temp:
            path = Path(temp)
            source = path / "orientation.c"
            binary = path / "orientation"
            source.write_text(harness)
            compiled = subprocess.run(
                [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(binary)],
                capture_output=True,
                text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
