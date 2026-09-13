"""Host-execute both analyzer exit paths with a borrowed notification record."""
import pathlib
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class AnalyzerOwnershipTest(unittest.TestCase):
    def test_repeated_exit_preserves_app_notification_record(self):
        for app in ("applications/main/subghz", "applications_user/arf_subghz_full"):
            with self.subTest(app=app):
                source = (ROOT / app / "views/subghz_frequency_analyzer.c").read_text()
                match = re.search(
                    r"void subghz_frequency_analyzer_exit\(void\* context\) \{.*?^\}",
                    source, re.M | re.S,
                )
                self.assertIsNotNone(match)
                harness = r'''
#include <assert.h>
#include <stdbool.h>
#define furi_assert assert
#define RECORD_NOTIFICATION "notification"
typedef struct { bool running; unsigned stops, frees; } Worker;
typedef struct { Worker* worker; } SubGhzFrequencyAnalyzer;
static unsigned notification_holders = 1;
void furi_record_close(const char* name) { (void)name; notification_holders--; }
bool subghz_frequency_analyzer_worker_is_running(Worker* w) { return w->running; }
void subghz_frequency_analyzer_worker_stop(Worker* w) { w->running=false; w->stops++; }
void subghz_frequency_analyzer_worker_free(Worker* w) { assert(!w->running); w->frees++; }
''' + match.group(0) + r'''
int main(void) {
    for(unsigned i=0; i<100; i++) {
        Worker w = {.running = (i % 2 == 0)};
        SubGhzFrequencyAnalyzer app = {.worker=&w};
        subghz_frequency_analyzer_exit(&app);
        assert(w.frees == 1);
        assert(w.stops == (i % 2 == 0));
        assert(notification_holders == 1);
    }
    furi_record_close(RECORD_NOTIFICATION);
    assert(notification_holders == 0);
    return 0;
}
'''
                with tempfile.TemporaryDirectory() as directory:
                    path = pathlib.Path(directory)
                    (path / "test.c").write_text(harness)
                    subprocess.run([shutil.which("cc") or "cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(path / "test.c"), "-o", str(path / "test")], check=True)
                    result = subprocess.run([str(path / "test")], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
