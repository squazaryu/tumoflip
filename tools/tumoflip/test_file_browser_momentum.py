"""Source-contract checks for the Momentum FileBrowser RAM/cursor port.

The implementation is firmware C code, so these checks deliberately validate
the ownership and navigation invariants that can regress without a device:
entries retain only their basename, while selection always reconstructs a
path from the worker's current directory.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
FILE_BROWSER = ROOT / "applications/services/gui/modules/file_browser.c"
WORKER = ROOT / "applications/services/gui/modules/file_browser_worker.c"
WORKER_INTERNAL = ROOT / "applications/services/gui/modules/file_browser_worker_i.h"
ARCHIVE = ROOT / "applications/main/archive/helpers/archive_browser.c"


class FileBrowserMomentumPortTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.file_browser = FILE_BROWSER.read_text()
        cls.worker = WORKER.read_text()
        cls.worker_internal = WORKER_INTERNAL.read_text()
        cls.archive = ARCHIVE.read_text()

    def test_browser_items_store_names_not_full_paths(self):
        item_struct = re.search(
            r"typedef struct \{(?P<body>.*?)\} BrowserItem_t;",
            self.file_browser,
            re.DOTALL,
        )
        self.assertIsNotNone(item_struct)
        self.assertIn("FuriString* name;", item_struct.group("body"))
        self.assertNotIn("FuriString* path;", item_struct.group("body"))
        self.assertIn("path_extract_filename(item_path, item.name, false);", self.file_browser)

    def test_selection_reconstructs_current_path(self):
        self.assertIn("file_browser_worker_get_path_current", self.file_browser)
        self.assertGreaterEqual(self.file_browser.count("path_concat("), 2)
        self.assertIn("furi_string_get_cstr(selected_item->name)", self.file_browser)

    def test_current_path_is_internal_only(self):
        self.assertIn("file_browser_worker_get_path_current(BrowserWorker* browser)", self.worker)
        self.assertIn("file_browser_worker_get_path_current", self.worker_internal)
        self.assertNotIn("file_browser_worker_get_path_current", "\n".join(
            line for line in (ROOT / "targets/f7/api_symbols.csv").read_text().splitlines()
            if line.startswith("Function,")
        ))

    def test_large_directory_cursor_is_clamped(self):
        self.assertIn("model->item_idx = (file_idx > 0) ? file_idx : 0;", self.archive)
        self.assertIn("#define BROWSER_SORT_THRESHOLD 220", (
            ROOT / "applications/services/gui/modules/file_browser_worker.h"
        ).read_text())


if __name__ == "__main__":
    unittest.main()
