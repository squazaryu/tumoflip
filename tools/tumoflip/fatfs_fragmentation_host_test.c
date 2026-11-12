/* Only a synthetic RAM disk is accessed. Never opens a host/device filesystem. */
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SECTORS (131072u)
#define OK(expr) do { FRESULT r = (expr); if(r != FR_OK) { \
    fprintf(stderr, "%s line %d: FatFs=%d\n", #expr, __LINE__, r); abort(); } } while(0)
static uint8_t* image;
DSTATUS disk_initialize(BYTE drive) { return drive == 0 ? 0 : STA_NODISK; }
DSTATUS disk_status(BYTE drive) { return disk_initialize(drive); }
DRESULT disk_read(BYTE drive, BYTE* data, DWORD sector, UINT count) {
    if(drive || sector >= SECTORS || count > SECTORS - sector) return RES_PARERR;
    memcpy(data, image + sector * 512u, count * 512u); return RES_OK;
}
DRESULT disk_write(BYTE drive, const BYTE* data, DWORD sector, UINT count) {
    if(drive || sector >= SECTORS || count > SECTORS - sector) return RES_PARERR;
    memcpy(image + sector * 512u, data, count * 512u); return RES_OK;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void* out) {
    if(drive) return RES_PARERR;
    switch(command) {
    case CTRL_SYNC: return RES_OK;
    case GET_SECTOR_COUNT: *(DWORD*)out = SECTORS; return RES_OK;
    case GET_SECTOR_SIZE: *(WORD*)out = 512; return RES_OK;
    case GET_BLOCK_SIZE: *(DWORD*)out = 1; return RES_OK;
    default: return RES_PARERR;
    }
}
DWORD get_fattime(void) { return (46u << 25) | (10u << 21) | (24u << 16); }

static void open_new(FIL* file, const char* name) {
    memset(file, 0xA5, sizeof(*file));
    OK(f_open(file, name, FA_CREATE_NEW | FA_WRITE));
}

static void exercise(BYTE format, int mode) {
    image = calloc(SECTORS, 512); assert(image);
    BYTE scratch[4096]; FATFS fs; FIL file; UINT written, read;
    OK(f_mkfs("0:", format | FM_SFD, format == FM_FAT ? 0 : 512, scratch, sizeof(scratch)));
    OK(f_mount(&fs, "0:", 1));
    OK(f_mkdir("0:/saved"));
    uint8_t payload[512]; memset(payload, 0x59, sizeof(payload));
    char path[128];
    for(unsigned i = 0; i < 80; ++i) {
        snprintf(path, sizeof(path), "0:/saved/capture_with_long_filename_%03u.sub", i);
        open_new(&file, path);
        payload[0] = i;
        OK(f_write(&file, payload, sizeof(payload), &written)); assert(written == sizeof(payload));
        OK(f_sync(&file)); OK(f_close(&file));
        FILINFO info; OK(f_stat(path, &info));
        assert(info.fsize == sizeof(payload));
        /* Occupy interleaving clusters and also exercise fragmented root growth. */
        snprintf(path, sizeof(path), "0:/spacer_%03u.bin", i);
        open_new(&file, path);
        OK(f_write(&file, payload, sizeof(payload), &written)); OK(f_close(&file));
    }
    OK(f_mount(NULL, "0:", 0));
    memset(&fs, 0xA5, sizeof(fs)); OK(f_mount(&fs, "0:", 1));
    for(unsigned i = 0; i < 80; ++i) {
        snprintf(path, sizeof(path), "0:/saved/capture_with_long_filename_%03u.sub", i);
        memset(&file, 0xA5, sizeof(file));
        OK(f_open(&file, path, FA_READ | FA_WRITE));
        if(format == FM_EXFAT && mode == 1) assert(file.obj.n_frag == 0);
        assert(f_size(&file) == sizeof(payload));
        uint8_t got[512]; OK(f_read(&file, got, sizeof(got), &read));
        assert(read == sizeof(got) && got[0] == i && got[511] == 0x59);
        if(mode == 1) {
            OK(f_write(&file, "tail", 4, &written)); assert(written == 4);
            OK(f_sync(&file));
        }
        OK(f_close(&file));
    }
    if(mode == 2) {
        /* Fragment a file, retain its first contiguous extent, and append again. */
        open_new(&file, "0:/fragmented.bin");
        for(unsigned i = 0; i < 12; ++i) {
            OK(f_write(&file, payload, sizeof(payload), &written)); OK(f_sync(&file));
            FIL barrier; snprintf(path, sizeof(path), "0:/barrier_%u.bin", i);
            open_new(&barrier, path); OK(f_write(&barrier, payload, 512, &written)); OK(f_close(&barrier));
        }
        OK(f_close(&file));
        memset(&file, 0xA5, sizeof(file)); OK(f_open(&file, "0:/fragmented.bin", FA_READ | FA_WRITE));
        OK(f_lseek(&file, 512)); OK(f_truncate(&file)); OK(f_sync(&file));
        if(format == FM_EXFAT) assert(file.obj.stat == 2);
        OK(f_write(&file, payload, 512, &written)); OK(f_close(&file));
        FILINFO info; OK(f_stat("0:/fragmented.bin", &info)); assert(info.fsize == 1024);
    }
    OK(f_mount(NULL, "0:", 0)); free(image);
}

int main(int argc, char** argv) {
    assert(argc == 3);
    int format = atoi(argv[1]), mode = atoi(argv[2]);
    exercise(format == 0 ? FM_FAT : format == 1 ? FM_FAT32 : FM_EXFAT, mode);
    return 0;
}
