"""Specter is receive-only and must not acknowledge failed SD writes."""
from pathlib import Path
import json
import unittest
import subprocess
import tempfile
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/specter"


class SpecterPackageTest(unittest.TestCase):
    def test_native_survey_and_wrapped_log_filter(self):
        for name, dependencies in (
            ("test_survey", [APP / "helpers/survey_verdict.c"]),
            ("test_logwrap", []),
        ):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                executable = str(Path(directory) / name)
                subprocess.run(
                    ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1",
                     "-fsanitize=address,undefined", str(APP / "test" / (name + ".c")),
                     *map(str, dependencies), "-o", executable],
                    check=True, capture_output=True, text=True,
                )
                subprocess.run([executable], check=True, capture_output=True, text=True)

    def test_specter_source_ownership_is_explicit(self):
        imports = json.loads((ROOT / "tools/tumoflip/protected_app_imports.json").read_text())
        entries = [item for item in imports["imports"] if item["appId"] == "specter"]
        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0]["localSourcePath"], "applications_user/specter")
        self.assertEqual(entries[0]["upstreamCommit"], "8970b6ba0e48a9c63b4bfaf07ca121f5b08e5b34")

    def test_capacity_failure_is_not_misreported_as_missing_sd(self):
        source = (APP / "helpers/specter_log.c").read_text()
        append = function(source, "bool specter_log_append(")
        self.assertIn("&specter_log_last_full", append)
        self.assertIn("specter_log_last_full = false", append)
        self.assertIn("specter_log_last_full ||", function(source, "bool specter_log_is_full("))

    def test_adaptation_version_is_visible_in_two_component_fap_metadata(self):
        # FAP manifests encode major/minor only: 3.0.1 would still appear as 3.0.
        self.assertIn('fap_version="3.2.0"', (APP / "application.fam").read_text())

    def test_specter_is_in_paired_packages_and_ci(self):
        from tools.tumoflip import validate_release as release
        target = "apps/NFC/specter.fap"
        self.assertIn(target, release.PACKAGE_ONLY_PACKAGE_FILES)
        self.assertEqual(release.PACKAGE_ONLY_PACKAGE_GROUPS[target], "base")
        for name in ("pr-build.yml", "release.yml"):
            self.assertIn("fap_specter", (ROOT / ".github/workflows" / name).read_text())

    def test_package_boundary_and_passive_receiver(self):
        self.assertTrue((APP / "application.fam").exists(), "Specter adaptation missing")
        self.assertIn("fap_package_only=True", (APP / "application.fam").read_text())
        self.assertTrue((APP / "LICENSE").exists())
        source = (APP / "helpers/field_detector.c").read_text()
        self.assertIn("furi_hal_nfc_field_detect_start", source)
        self.assertNotIn("furi_hal_nfc_tx", source)
        self.assertNotIn("furi_hal_nfc_poller_tx", source)

    def test_append_is_bounded_and_reports_sync_close_failure(self):
        path = APP / "helpers/specter_log.c"
        self.assertTrue(path.exists(), "Specter logger missing")
        production = function(path.read_text(), "static bool\n    append_to(")
        native(r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
typedef int Storage;typedef int File;
enum{FSAM_WRITE,FSOM_OPEN_APPEND};
#define SPECTER_LOG_MAX_BYTES (1024u*1024u)
static File f;static uint64_t bytes;static bool opened=true,synced=true,closed=true,short_write;static unsigned writes;static uint32_t position;
static File* storage_file_alloc(Storage*s){(void)s;return &f;}
static bool storage_file_open(File*f,const char*p,int a,int b){(void)f;(void)p;(void)a;(void)b;return opened;}
static uint64_t storage_file_size(File*f){(void)f;return bytes;}
static size_t storage_file_write(File*f,const void*d,size_t n){(void)f;(void)d;writes++;if(short_write)n--;bytes+=n;return n;}
static bool __attribute__((unused)) storage_file_sync(File*f){(void)f;return synced;}
static bool __attribute__((unused)) storage_file_seek(File*f,uint32_t p,bool absolute){(void)f;(void)absolute;position=p;return true;}
static bool __attribute__((unused)) storage_file_truncate(File*f){(void)f;bytes=position;return true;}
static bool storage_file_close(File*f){(void)f;return closed;}
static void storage_file_free(File*f){(void)f;}
''' + production + r'''
int main(void){bool full=false;
 closed=false;assert(!append_to(NULL,"log",NULL,"record\n",&full));closed=true;
 synced=false;assert(!append_to(NULL,"log",NULL,"record\n",&full));synced=true;
 bytes=10;short_write=true;assert(!append_to(NULL,"log",NULL,"record\n",&full));assert(bytes==10);short_write=false;
 bytes=SPECTER_LOG_MAX_BYTES-1;writes=0;assert(!append_to(NULL,"log",NULL,"record\n",&full));assert(full&&writes==0);
 bytes=0;full=false;assert(append_to(NULL,"log","head\n","record\n",&full));assert(bytes==12);
 opened=false;assert(!append_to(NULL,"log",NULL,"record\n",&full));return 0;}
''')


if __name__ == "__main__":
    unittest.main()
