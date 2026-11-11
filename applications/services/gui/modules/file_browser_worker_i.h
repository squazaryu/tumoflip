#pragma once

#include "file_browser_worker.h"

// Internal helpers shared by FileBrowser and Archive.  They are intentionally
// kept out of the public FAP API because external applications do not need
// access to the worker's current directory or its implementation details.
const char* file_browser_worker_get_path_current(BrowserWorker* browser);

typedef void (*BrowserWorkerFolderErrorCallback)(void* context);
void file_browser_worker_set_folder_error_callback(
    BrowserWorker* browser,
    BrowserWorkerFolderErrorCallback callback);

// Native FileBrowser registers every callback before starting its first scan.
// The public alloc API retains its historical auto-start behavior for FAPs.
BrowserWorker* file_browser_worker_alloc_deferred(
    FuriString* path,
    const char* base_path,
    const char* ext_filter,
    bool skip_assets,
    bool hide_dot_files);
void file_browser_worker_start(BrowserWorker* browser);
