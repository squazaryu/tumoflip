#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define furi_assert assert
#define FURI_LOG_E(...) ((void)0)
#define FURI_LOG_W(...) ((void)0)
#define RECORD_STORAGE "storage"
#define MIN(a,b) ((a)<(b)?(a):(b))
enum {FSAM_READ=1,FSAM_WRITE=2,FSAM_READ_WRITE=3};
enum {FSOM_OPEN_EXISTING,FSOM_CREATE_NEW,FSOM_CREATE_ALWAYS};
typedef int FS_OpenMode;
enum {StreamOffsetFromStart,StreamOffsetFromCurrent,StreamOffsetFromEnd};
typedef struct {unsigned unused;} Storage;
typedef struct {char value[256];} FuriString;
typedef struct {uint8_t data[2048];size_t size,pos;} Stream;
typedef struct {Stream* stream;} FlipperFormat;
typedef struct {uint8_t data[2048];size_t size;bool exists;} Entry;
typedef struct {Entry* entry;size_t pos;bool opened;int error;} File;
typedef enum {FSE_OK,FSE_ERROR,FSE_NOT_EXIST} FS_Error;
static Entry original,backup;
static Storage storage;
static unsigned operation,fail_at,fail_again;
static bool fail(void){operation++;return operation==fail_at||operation==fail_again;}
static FuriString* furi_string_alloc(void){return calloc(1,sizeof(FuriString));}
static FuriString* furi_string_alloc_printf(const char* fmt,const char* path){
 FuriString* s=furi_string_alloc();snprintf(s->value,sizeof(s->value),fmt,path);return s;
}
static const char* furi_string_get_cstr(const FuriString* s){return s->value;}
static void furi_string_free(FuriString* s){free(s);}
static void path_extract_dirname(const char* p,FuriString* s){(void)p;(void)s;}
static void* furi_record_open(const char* n){(void)n;return &storage;}
static void furi_record_close(const char* n){(void)n;}
static Stream* flipper_format_get_raw_stream(FlipperFormat* f){return f->stream;}
static bool flipper_format_delete_key(FlipperFormat* f,const char* k){(void)f;(void)k;return true;}
static bool flipper_format_insert_or_update_uint32(FlipperFormat* f,const char* k,uint32_t* v,int c){(void)f;(void)k;(void)v;(void)c;return true;}
static File* storage_file_alloc(Storage* s){(void)s;return calloc(1,sizeof(File));}
static void storage_file_free(File* f){assert(!f->opened);free(f);}
static Entry* entry(const char* p){return strstr(p,".tumo-bak")?&backup:&original;}
static FS_Error storage_common_stat(Storage*s,const char*p,void*i){(void)s;(void)i;return fail()?FSE_ERROR:entry(p)->exists?FSE_OK:FSE_NOT_EXIST;}
static bool storage_file_open(File* f,const char* p,int a,int m){
 (void)a;if(fail())return false;Entry* e=entry(p);
 if(m==FSOM_CREATE_NEW&&e->exists)return false;
 if(m==FSOM_OPEN_EXISTING&&!e->exists)return false;
 if(m!=FSOM_OPEN_EXISTING){e->exists=true;e->size=0;}
 f->entry=e;f->pos=0;f->opened=true;return true;
}
static bool storage_file_close(File* f){bool ok=!fail();f->opened=false;return ok;}
static size_t storage_file_read(File* f,void* out,size_t n){
 assert(f->opened);if(fail())return 0;n=MIN(n,f->entry->size-f->pos);
 memcpy(out,f->entry->data+f->pos,n);f->pos+=n;return n;
}
static size_t storage_file_write(File* f,const void* src,size_t n){
 assert(f->opened);if(fail())n/=2;assert(f->pos+n<=sizeof(original.data));
 memcpy(f->entry->data+f->pos,src,n);f->pos+=n;
 if(f->pos>f->entry->size)f->entry->size=f->pos;return n;
}
static uint64_t storage_file_size(File* f){
 if(fail()){f->error=FSE_ERROR;return 0;}return f->entry->size;
}
static int storage_file_get_error(File* f){return f->error;}
static bool storage_file_seek(File* f,uint32_t p,bool start){
 assert(start);if(fail())return false;if(p>f->entry->size)return false;f->pos=p;return true;
}
static bool storage_file_truncate(File* f){if(fail())return false;f->entry->size=f->pos;return true;}
static bool storage_file_sync(File* f){(void)f;return !fail();}
static bool storage_simply_remove(Storage* s,const char* p){
 (void)s;if(fail())return false;entry(p)->exists=false;return true;
}
static bool storage_file_copy_to_file(File* in,File* out,size_t n){
 uint8_t buffer[128];while(n){size_t count=MIN(n,sizeof(buffer));
 if(storage_file_read(in,buffer,count)!=count||storage_file_write(out,buffer,count)!=count)return false;n-=count;}return true;
}
static size_t stream_size(Stream* s){return s->size;}
static size_t stream_tell(Stream* s){return s->pos;}
static bool stream_seek(Stream* s,int32_t p,int origin){
 int64_t pos=p+(origin==StreamOffsetFromStart?0:origin==StreamOffsetFromCurrent?s->pos:s->size);
 if(fail()||pos<0||(size_t)pos>s->size)return false;s->pos=(size_t)pos;return true;
}
static size_t stream_read(Stream* s,uint8_t* data,size_t n){
 if(fail())return 0;n=MIN(n,s->size-s->pos);memcpy(data,s->data+s->pos,n);s->pos+=n;return n;
}
static size_t stream_save_to_file(Stream* s,Storage* st,const char* p,int m){
 File* f=storage_file_alloc(st);size_t count=0;
 if(storage_file_open(f,p,FSAM_WRITE,m))count=storage_file_write(f,s->data,s->size);
 storage_file_close(f);storage_file_free(f);return count;
}
/* SAVE */
static uint8_t old_data[641],new_data[299];
static void reset(Stream* s){
 memset(&original,0,sizeof(original));memset(&backup,0,sizeof(backup));
 original.exists=true;original.size=sizeof(old_data);memcpy(original.data,old_data,sizeof(old_data));
 memset(s,0,sizeof(*s));s->size=sizeof(new_data);s->pos=17;memcpy(s->data,new_data,sizeof(new_data));operation=0;
}
static bool is_old(Entry* e){return e->exists&&e->size==sizeof(old_data)&&!memcmp(e->data,old_data,sizeof(old_data));}
static bool is_new(Entry* e){return e->exists&&e->size==sizeof(new_data)&&!memcmp(e->data,new_data,sizeof(new_data));}
int main(void){
 for(unsigned i=0;i<sizeof(old_data);i++)old_data[i]=(uint8_t)(i*31+7);
 for(unsigned i=0;i<sizeof(new_data);i++)new_data[i]=(uint8_t)(i*17+3);
 Stream s;reset(&s);fail_at=0;fail_again=0;
 assert(signal_file_update(&storage,&s,"test.sub"));assert(is_new(&original));
 assert(s.pos==17);
 unsigned total=operation;
 for(unsigned first=1;first<=total;first++){
  reset(&s);fail_at=first;fail_again=0;
  bool ok=signal_file_update(&storage,&s,"test.sub");
  assert(ok?is_new(&original):(is_old(&original)||is_old(&backup)));
  // A second independent error can occur during rollback/cleanup.
  unsigned with_failure=operation;
  for(unsigned second=first+1;second<=with_failure;second++){
   reset(&s);fail_at=first;fail_again=second;
   ok=signal_file_update(&storage,&s,"test.sub");
   assert(ok?is_new(&original):(is_old(&original)||is_old(&backup)));
  }
 }
 reset(&s);fail_at=fail_again=0;backup.exists=true;backup.size=3;memcpy(backup.data,"own",3);
 assert(!signal_file_update(&storage,&s,"test.sub"));
 assert(is_old(&original)&&backup.size==3&&!memcmp(backup.data,"own",3));
 reset(&s);original.exists=false;assert(!signal_file_update(&storage,&s,"missing.sub"));
 assert(!original.exists);
 reset(&s);s.size=0;assert(!signal_file_update(&storage,&s,"empty.sub"));assert(is_old(&original));
 reset(&s);s.size=(size_t)INT32_MAX+1;assert(!signal_file_update(&storage,&s,"huge.sub"));assert(is_old(&original));
 reset(&s);s.pos=(size_t)INT32_MAX+1;assert(!signal_file_update(&storage,&s,"hugepos.sub"));assert(is_old(&original));
 return 0;
}
