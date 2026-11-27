"""Exercise ViewStack redraws with a deterministic timer/desktop preemption."""
from pathlib import Path
import unittest

from tools.tumoflip.native_optional_memory import native
from tools.tumoflip.test_hotplug_assets import function

GUI = Path(__file__).resolve().parents[2] / "applications/services/gui"

HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#define furi_check(c) assert(c)
#define furi_assert(c) ((void)(c)) /* Release firmware. */
#define MAX_VIEWS 3
typedef struct View View;
typedef struct ViewStack ViewStack;
typedef struct { View* views[MAX_VIEWS]; } ViewStackModel;
typedef void (*ViewUpdateCallback)(View*, void*);
struct View {
    ViewUpdateCallback update_callback;
    void* update_callback_context;
    void (*enter_callback)(void*);
    void (*exit_callback)(void*);
    void* context;
    void* model;
};
struct ViewStack { View view; };
static View child;
static ViewStack stack;
static ViewStackModel model;
static bool armed;
static unsigned critical_depth, callbacks;
static void (*scheduled)(void);
void view_commit_model(View*, bool);
void view_stack_remove_view(ViewStack*, View*);
static void* view_get_model(View* view) { return view->model; }
static void view_unlock_model(View* view) { (void)view; }

static void run_scheduled(void) {
    if(critical_depth || !scheduled) return;
    void (*pending)(void) = scheduled;
    scheduled = NULL;
    pending();
}
#define FURI_CRITICAL_ENTER() (++critical_depth)
#define FURI_CRITICAL_EXIT() do { --critical_depth; run_scheduled(); } while(0)
static void preempt(void (*next)(void)) {
    if(!armed) return;
    armed = false;
    assert(!scheduled);
    scheduled = next;
    run_scheduled();
}
static void timer_redraw(void) { view_commit_model(&child, true); }
static void remove_child(void) { view_stack_remove_view(&stack, &child); }
static void parent_redraw(View* view, void* context) {
    assert(!critical_depth && view == &stack.view && context == &stack);
    ++callbacks;
}
static void setup(void) {
    stack.view.model = &model;
    stack.view.update_callback = parent_redraw;
    stack.view.update_callback_context = &stack;
}
"""


def production():
    source = (GUI / "view.c").read_text()
    stack = (GUI / "view_stack.c").read_text()
    parts = []
    for signature in (
        "void view_set_update_callback(",
        "void view_set_update_callback_context(",
        "void view_set_update_handler(",
        "void view_request_update(",
        "void view_commit_model(",
    ):
        if signature not in source:
            continue
        body = function(source, signature)
        # The scheduler can run just after publishing a callback, or just
        # after the consumer has taken its function but before taking context.
        body = body.replace(
            "view->update_callback = callback;",
            "view->update_callback = callback; preempt(timer_redraw);",
        ).replace(
            "ViewUpdateCallback callback = view->update_callback;",
            "ViewUpdateCallback callback = view->update_callback; preempt(remove_child);",
        ).replace(
            "if(update && view->update_callback) {",
            "if(update && view->update_callback) { preempt(remove_child);",
        )
        parts.append(body)
    parts.extend(function(stack, signature) for signature in (
        "static void view_stack_update_callback(",
        "void view_stack_add_view(",
        "void view_stack_remove_view(",
    ))
    return HARNESS + "\n".join(parts)


class ViewUpdatePairTests(unittest.TestCase):
    def test_timer_observes_complete_attachment(self):
        native(production() + r"""
int main(void) {
    setup();
    armed = true;
    view_stack_add_view(&stack, &child);
    assert(!armed && !scheduled && callbacks == 2);
    return 0;
}
""")

    def test_pending_timer_uses_context_from_same_binding(self):
        native(production() + r"""
int main(void) {
    setup();
    view_stack_add_view(&stack, &child);
    armed = true;
    timer_redraw();
    assert(!armed && !scheduled && !model.views[0]);
    assert(!child.update_callback && !child.update_callback_context);
    assert(callbacks >= 2);
    return 0;
}
""")

