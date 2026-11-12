#include "library_io_stubs.h"
#include <setjmp.h>
#define furi_assert assert
#define furi_check assert
static void frame_log(const char*format,...){(void)format;}
#define FURI_LOG_E(tag,...) frame_log(__VA_ARGS__)
#define FURI_LOG_W(tag,...) frame_log(__VA_ARGS__)
#define FURI_CONST_ASSIGN(dst,value) ((dst)=(value))
#define FURI_CONST_ASSIGN_PTR(dst,value) ((dst)=(value))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ROUND_UP_TO(value,divisor) (((value)+(divisor)-1)/(divisor))
#define ANIMATION_DIR "/ext/dolphin"
#define ANIMATION_FRAMES_FILE "frames.bin"
#define ANIMATION_FRAME_BUNDLE_VERSION 1U
typedef struct{uint8_t frame_count,frame_rate,height,width;const uint8_t**frames;}Icon;
typedef struct{Icon icon_animation;uint8_t passive_frames,active_frames;uint8_t*frame_order;
 uint32_t active_cycles,duration,active_cooldown;}BubbleAnimation;
static const uint8_t animation_frame_bundle_magic[4]={'T','F','A','B'};
static jmp_buf allocation_fault;
static size_t largest_allowed=SIZE_MAX,allocation_calls,live_frames;
static int64_t allocation_fail_at=-1;
static unsigned fatal_allocations;
static void*owned_frames[512];
static void* memmgr_heap_alloc_optional(size_t n){
 allocation_calls++;
 if(!n||n>largest_allowed||(int64_t)allocation_calls==allocation_fail_at)return NULL;
 void*p=calloc(1,n);assert(p);
 for(unsigned i=0;i<512;i++)if(!owned_frames[i]){owned_frames[i]=p;live_frames++;return p;}
 assert(!"fixture allocation inventory full");return NULL;
}
static void* __attribute__((unused)) frame_malloc(size_t n){void*p=memmgr_heap_alloc_optional(n);if(!p){fatal_allocations++;longjmp(allocation_fault,1);}return p;}
static void* __attribute__((unused)) frame_calloc(size_t n,size_t size){assert(n<=SIZE_MAX/size);return frame_malloc(n*size);}
static void frame_free(void*p){
 if(!p)return;
 for(unsigned i=0;i<512;i++)if(owned_frames[i]==p){owned_frames[i]=NULL;assert(live_frames);live_frames--;free(p);return;}
 assert(!"free of an unowned frame or double free");
}
static void setup_storage(Storage*s,const char*root){
 (void)firmware_api_interface;snprintf(s->root,sizeof(s->root),"%s",root);
 assert(storage_simply_mkdir(s,"/ext/dolphin"));assert(storage_simply_mkdir(s,"/ext/dolphin/Fixture"));
}
static void blob(Storage*s,const char*path,const void*data,size_t size){
 FS_Error removed=storage_common_remove(s,path);assert(removed==FSE_OK||removed==FSE_NOT_EXIST);
 File*f=storage_file_alloc(s);assert(storage_file_open(f,path,FSAM_WRITE,FSOM_CREATE_NEW));
 assert(storage_file_write(f,data,size)==size&&storage_file_sync(f)&&storage_file_close(f));storage_file_free(f);
}
static void small_bundle(Storage*s){
 const uint8_t bytes[]={ 'T','F','A','B',15,0,0,0,1,2,32,2, 9,0,6,0,
  0,0xaa,0,0,0,0,0,0,0, 1,0,2,0,0xbb,0xcc };
 blob(s,ANIMATION_DIR "/Fixture/frames.bin",bytes,sizeof(bytes));
}
#define ANIMATION_META_FILE "meta.txt"
#define RECORD_STORAGE "storage"
typedef int FlipperFormat;
static FlipperFormat metadata_file;
static Storage*metadata_storage;
static bool metadata_fail_after_frames;
static void* animation_record_open(const char*key){assert(!strcmp(key,RECORD_STORAGE)||!strcmp(key,"dolphin"));return metadata_storage;}
static FlipperFormat*flipper_format_file_alloc(Storage*s){assert(s==metadata_storage);return &metadata_file;}
static void flipper_format_set_strict_mode(FlipperFormat*f,bool strict){(void)f;(void)strict;}
static bool flipper_format_file_open_existing(FlipperFormat*f,const char*p){(void)f;assert(strstr(p,"/Fixture/meta.txt"));return true;}
static bool flipper_format_read_header(FlipperFormat*f,FuriString*s,uint32_t*v){(void)f;*v=1;furi_string_set(s,"Flipper Animation");return true;}
static int furi_string_cmp_str(FuriString*s,const char*t){return strcmp(s->text,t);}
static bool flipper_format_get_value_count(FlipperFormat*f,const char*k,uint32_t*n){(void)f;assert(!strcmp(k,"Frames order"));*n=2;return true;}
static bool flipper_format_read_uint32(FlipperFormat*f,const char*k,uint32_t*v,size_t n){
 (void)f;if(!strcmp(k,"Frames order")){assert(n==2);v[0]=0;v[1]=1;return true;}assert(n==1);
 if(!strcmp(k,"Active cycles")&&metadata_fail_after_frames)return false;
 *v=!strcmp(k,"Width")?32:!strcmp(k,"Height")?2:1;return true;
}
static void flipper_format_free(FlipperFormat*f){assert(f==&metadata_file);}
static bool animation_storage_load_bubbles(BubbleAnimation*a,FlipperFormat*f){(void)a;(void)f;return true;}
static void animation_storage_free_bubbles(BubbleAnimation*a){(void)a;}
