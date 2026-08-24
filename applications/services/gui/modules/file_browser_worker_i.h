#pragma once

#include "file_browser_worker.h"

// Internal helpers shared by FileBrowser and Archive.  They are intentionally
// kept out of the public FAP API because external applications do not need
// access to the worker's current directory or its implementation details.
const char* file_browser_worker_get_path_current(BrowserWorker* browser);
