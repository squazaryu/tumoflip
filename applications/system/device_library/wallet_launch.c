#include "wallet_launch.h"
#include "../../../lib/fap_metadata/fap_metadata.h"
#include <string.h>

static bool library_fap_read(void* context, uint32_t offset, void* output, size_t size) {
    File* file = context;
    return storage_file_seek(file, offset, true) && storage_file_read(file, output, size) == size;
}

bool library_native_inspect_ready(Storage* storage, LibraryFileKind kind) {
    if(storage_sd_status(storage) != FSE_OK) return false;
    const char* path;
    if(kind == LibraryFileRfid)
        path = EXT_PATH("apps/RFID/lfrfid.fap");
    else if(kind == LibraryFileIbutton)
        path = EXT_PATH("apps/iButton/ibutton.fap");
    else
        return kind == LibraryFileSub || kind == LibraryFileIr || kind == LibraryFileNfc;
    File* file = storage_file_alloc(storage);
    bool open = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    ArfElfMetadata metadata;
    bool ok = open &&
              arf_elf_metadata_read(library_fap_read, file, storage_file_size(file), &metadata) == ArfElfOk &&
              metadata.target == 7 && metadata.api_major == 88 && metadata.app_version >= 0x00010001U &&
              !strcmp(metadata.name, library_launch_target(kind));
    if(open) ok = storage_file_close(file) && ok;
    storage_file_free(file);
    return ok;
}
