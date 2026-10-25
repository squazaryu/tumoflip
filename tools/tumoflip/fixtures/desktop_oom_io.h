#include "library_io_stubs.h"
#include <setjmp.h>
#define furi_assert assert
#define furi_check assert
#define FURI_LOG_E(...) ((void)0)
#define FURI_LOG_W(...) ((void)0)
#define FURI_CONST_ASSIGN(dst,value) ((dst)=(value))
#define FURI_CONST_ASSIGN_PTR(dst,value) ((dst)=(value))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define ROUND_UP_TO(value,divisor) (((value)+(divisor)-1)/(divisor))
#define ANIMATION_DIR "/ext/dolphin"
#define ANIMATION_FRAMES_FILE "frames.bin"
#define ANIMATION_FRAME_BUNDLE_VERSION 1U
typedef struct{uint8_t frame_count,frame_rate,height,width;const uint8_t**frames;}Icon;
typedef struct{Icon icon_animation;uint8_t passive_frames,active_frames;const uint8_t*frame_order;}BubbleAnimation;
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
