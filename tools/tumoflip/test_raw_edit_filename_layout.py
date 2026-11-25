"""Exercise RAW Edit's filename layout without touching captures or a device."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


APP = Path(__file__).resolve().parents[2] / "applications_user/subghz_raw_edit"

LAYOUT_TEST = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "raw_edit_filename_layout.h"

/* Check that the renderer never receives part of a UTF-8 character.
   Give some glyphs different widths to catch character-count clipping. */
static size_t width(void *context, const char *text)
{
    (void)context;
    size_t pixels = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p)
    {
        const unsigned char lead = *p++;
        unsigned extra = 0;
        if (lead >= 0xF0 && lead <= 0xF4) extra = 3;
        else if (lead >= 0xE0 && lead <= 0xEF) extra = 2;
        else if (lead >= 0xC2 && lead <= 0xDF) extra = 1;
        else assert(lead < 0x80);
        while (extra--)
        {
            assert((*p & 0xC0) == 0x80);
            p++;
        }
        pixels += lead == 'W' ? 8 : lead == 'i' ? 2 : lead == '.' ? 2 : 5;
    }
    return pixels;
}

int main(void)
{
    char label[64];
    assert(raw_edit_filename_width(128, 30) == 94);
    assert(raw_edit_filename_width(128, 127) == 0);
    assert(raw_edit_filename_width(3, 0) == 0);
    raw_edit_fit_filename(label, sizeof(label), "/ext/subghz/WiFi.Sub", 50, width, NULL);
    assert(strcmp(label, "WiFi") == 0);
    raw_edit_fit_filename(label, sizeof(label), "merged", 50, width, NULL);
    assert(strcmp(label, "merged") == 0);

    static const char first[] =
        "/ext/subghz/Bench-WWWWW-2026-11-25-extra-notes-repeat-identical-take-001.sub";
    static const char second[] =
        "/ext/subghz/Bench-WWWWW-2026-11-25-extra-notes-repeat-identical-take-002.sub";
    raw_edit_fit_filename(label, sizeof(label), first, 75, width, NULL);
    assert(strncmp(label, "Bench", 5) == 0);
    assert(strstr(label, "...") != NULL);
    assert(strcmp(label + strlen(label) - 3, "001") == 0);
    assert(width(NULL, label) <= 75);
    raw_edit_fit_filename(label, sizeof(label), second, 75, width, NULL);
    assert(strcmp(label + strlen(label) - 3, "002") == 0);

    raw_edit_fit_filename(label, sizeof(label), "/ext/Жёлтая лампа пример_жёлтый_37.sub", 58, width, NULL);
    assert(strncmp(label, "Жё", strlen("Жё")) == 0);
    assert(strcmp(label + strlen(label) - 3, "_37") == 0);
    assert(width(NULL, label) <= 58);

    raw_edit_fit_filename(label, sizeof(label), first, 6, width, NULL);
    assert(strcmp(label, "...") == 0);
    raw_edit_fit_filename(label, sizeof(label), first, 5, width, NULL);
    assert(label[0] == '\0');
    raw_edit_fit_filename(label, sizeof(label), first, 0, width, NULL);
    assert(label[0] == '\0');
    raw_edit_fit_filename(label, sizeof(label), "/ext/subghz/", 120, width, NULL);
    assert(label[0] == '\0');

    char tiny[5] = {'?', '?', '?', '?', '!'};
    raw_edit_fit_filename(tiny, sizeof(tiny) - 1, first, 120, width, NULL);
    assert(strcmp(tiny, "...") == 0 && tiny[4] == '!');
    raw_edit_fit_filename(tiny, 1, "x", 120, width, NULL);
    assert(tiny[0] == '\0' && tiny[1] == '.');

    for (size_t pixels = 0; pixels <= 128; pixels++)
    {
        raw_edit_fit_filename(label, sizeof(label), first, pixels, width, NULL);
        assert(width(NULL, label) <= pixels);
    }
}
"""


class RawEditFilenameLayoutTest(unittest.TestCase):
    def test_respects_pixels_and_keeps_the_distinguishing_suffix(self) -> None:
        with tempfile.TemporaryDirectory(prefix="raw-edit-layout-") as directory:
            binary = Path(directory) / "layout_test"
            subprocess.run(
                [
                    os.environ.get("CC", "cc"),
                    "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined",
                    "-x", "c", "-I", str(APP), "-o", str(binary), "-",
                ],
                input=LAYOUT_TEST,
                text=True,
                check=True,
                timeout=30,
            )
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_first_editor_frame_is_drawn_after_the_view_is_ready(self) -> None:
        source = (APP / "subghz_raw_edit.c").read_text(encoding="utf-8")
        session = source.split("static void run_editor_session(", 1)[1].split(
            "    bool running = true;", 1
        )[0]
        self.assertLess(session.index("recompute_activity(app);"), session.index("app->loading = false;"))
        self.assertLess(session.index("app->loading = false;"), session.index("view_port_update(vp);"))


if __name__ == "__main__":
    unittest.main()
