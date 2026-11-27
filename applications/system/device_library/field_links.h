#pragma once

#include "library_model.h"
#include <storage/storage.h>

// Display-only snapshot of file metadata. Does not open the linked files or
// modify the card, round, or any filesystem entry.
void field_links_report(Storage* storage, const DeviceCard* card, FuriString* report);
