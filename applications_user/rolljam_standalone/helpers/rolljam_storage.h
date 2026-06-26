// helpers/rolljam_storage.h
#pragma once

#include <furi.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>

#define ROLLJAM_APP_FOLDER       APP_DATA_PATH("saved")
#define ROLLJAM_APP_EXTENSION    ".psf"
#define ROLLJAM_APP_FILE_VERSION 1
#define ROLLJAM_TEMP_FILE        APP_DATA_PATH("saved/.temp.psf")
#define ROLLJAM_CACHE_FOLDER     APP_DATA_PATH("cache")
#define ROLLJAM_HISTORY_FOLDER   APP_DATA_PATH("cache/history")

// Initialize storage (create folder if needed)
bool rolljam_storage_init(void);

// Save a capture to a new file (auto-generated name)
bool rolljam_storage_save_capture(
    FlipperFormat* flipper_format,
    const char* protocol_name,
    FuriString* out_path);

// Save a capture to a specific file path (user-chosen name)
bool rolljam_storage_save_capture_to_path(FlipperFormat* flipper_format, const char* full_path);

// Save to temp file for emulation
bool rolljam_storage_save_temp(FlipperFormat* flipper_format);

// Delete temp file
void rolljam_storage_delete_temp(void);

// Get next available filename for a protocol
bool rolljam_storage_get_next_filename(const char* protocol_name, FuriString* out_filename);

// Delete a file
bool rolljam_storage_delete_file(const char* file_path);

// Load a file (caller must close with rolljam_storage_close_file)
FlipperFormat* rolljam_storage_load_file(const char* file_path);

// Close a loaded file (by rolljam_storage_load_file only)
void rolljam_storage_close_file(FlipperFormat* flipper_format);

// Check if file exists
bool rolljam_storage_file_exists(const char* file_path);

bool rolljam_storage_ensure_history_folder(void);

void rolljam_storage_purge_temp_history_at_startup(void);

void rolljam_storage_wipe_history_cache(void);

bool rolljam_storage_save_history_capture(
    FlipperFormat* flipper_format,
    uint32_t seq,
    FuriString* out_path);

void rolljam_storage_build_history_path(uint32_t seq, FuriString* out);
