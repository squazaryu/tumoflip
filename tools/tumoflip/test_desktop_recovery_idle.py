"""Exercise the first idle animation with recovery's intentionally absent profile."""
from pathlib import Path
import unittest

from tools.tumoflip.native_optional_memory import native
from tools.tumoflip.test_hotplug_assets import function


ANIM = Path(__file__).resolve().parents[2] / "applications/services/desktop/animations"


class DesktopRecoveryIdleTests(unittest.TestCase):
    def test_first_idle_uses_builtin_timing_without_a_profile(self):
        profile = (ANIM / "desktop_profile.c").read_text()
        manager = (ANIM / "animation_manager.c").read_text()
        # Exercise the real predicate and timing code. The selector-only tests
        # stop before start_new_idle computes a duration; a mock predicate
        # returning false would also mask the release-mode NULL dereference.
        body = r"""
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define furi_assert(condition) ((void)(condition)) /* release build */
typedef enum {DesktopProfileOrderRandom, DesktopProfileOrderSequential} DesktopProfileOrder;
typedef enum {DesktopProfileTimingOriginal, DesktopProfileTimingCustom} DesktopProfileTiming;
typedef struct DesktopProfile DesktopProfile;
"""
        body += profile[profile.index("struct DesktopProfile {"):profile.index("};") + 2]
        for name in ("bool desktop_profile_is_active(", "DesktopProfileTiming desktop_profile_get_timing(",
                     "uint32_t desktop_profile_get_duration_seconds("):
            body += "\n" + function(profile, name)
        body += r"""
typedef struct { uint16_t duration; } BubbleAnimation;
typedef struct { BubbleAnimation frames; } StorageAnimation;
typedef struct {
    DesktopProfile* desktop_profile;
    StorageAnimation* current_animation;
    bool quiet;
    uint32_t paused_idle_ticks;
    int state;
    unsigned* idle_animation_timer;
} AnimationManager;
enum { AnimationManagerStateIdle = 1 };
static StorageAnimation builtin = {.frames = {.duration = 17}};
static StorageAnimation* animation_manager_select_idle_animation(AnimationManager* m) {
    (void)m; return &builtin;
}
static void animation_manager_replace_current_animation(AnimationManager* m, StorageAnimation* a) {
    m->current_animation = a;
}
static const BubbleAnimation* animation_storage_get_bubble_animation(StorageAnimation* a) {
    return &a->frames;
}
static void furi_timer_start(unsigned* timer, uint32_t ticks) { *timer = ticks; }
"""
        for signature in ("static void animation_manager_idle_start(",
                          "static uint32_t animation_manager_get_idle_duration(",
                          "static void animation_manager_start_new_idle("):
            # There are also forward declarations in animation_manager.c.
            body += "\n" + function(manager[manager.rindex(signature):], signature)
        body += r"""
int main(void) {
    unsigned timer = 0;
    AnimationManager m = {.quiet = true, .idle_animation_timer = &timer};
    animation_manager_start_new_idle(&m);
    assert(m.current_animation == &builtin && m.state == AnimationManagerStateIdle);
    assert(m.paused_idle_ticks == 17000 && timer == 0);
    DesktopProfile p = {.timing = DesktopProfileTimingCustom, .duration_seconds = 9};
    m.desktop_profile = &p;
    m.quiet = false;
    animation_manager_start_new_idle(&m); assert(timer == 17000);
    p.enabled = true;
    animation_manager_start_new_idle(&m); assert(timer == 17000); /* empty selection */
    p.animation_count = 1;
    animation_manager_start_new_idle(&m); assert(timer == 9000);
    p.animation_count = 0;
    p.selection_all = true;
    animation_manager_start_new_idle(&m); assert(timer == 9000);
    p.timing = DesktopProfileTimingOriginal;
    animation_manager_start_new_idle(&m); assert(timer == 17000);
    return 0;
}
"""
        native(body)
