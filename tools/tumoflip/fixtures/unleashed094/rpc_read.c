#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#define furi_assert(x) assert(x)
#define FURI_LOG_D(...) ((void)0)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX_DATA_SIZE 8
enum {PB_Main_storage_read_request_tag,PB_Main_storage_read_response_tag,PB_CommandStatus_OK=0,PB_CommandStatus_ERROR_STORAGE_INTERNAL=83,FSAM_READ,FSOM_OPEN_EXISTING};
typedef struct {uint16_t size;uint8_t bytes[];} Bytes;
#define PB_BYTES_ARRAY_T_ALLOCSIZE(n) (sizeof(Bytes)+(n))
typedef struct {uint32_t command_id;int which_content,command_status;bool has_next;union {
    struct {const char* path;uint32_t offset,size;} storage_read_request;
    struct {bool has_file,ranged;struct {Bytes* data;} file;} storage_read_response;
} content;} PB_Main;
typedef struct {int unused;} RpcSession;
typedef struct {RpcSession* session;void* api;} RpcStorageSystem;
typedef struct {uint32_t position;} File;
static File file;
static uint8_t contents[64],output[64];
static size_t output_size;
static int allocations,closes,errors,messages,reads,short_read_at;
static bool open_ok,seek_ok;
static uint32_t file_size;
void* checked_malloc(size_t n){void* p=calloc(1,n);assert(p);allocations++;return p;}
void checked_free(void* p){if(p){allocations--;free(p);}}
#define malloc checked_malloc
#define free checked_free
void rpc_system_storage_reset_state(RpcStorageSystem* s,RpcSession* session,bool v){(void)s;(void)session;(void)v;}
File* storage_file_alloc(void* api){(void)api;file.position=0;return &file;}
bool storage_file_open(File* f,const char* path,int a,int b){(void)f;(void)path;(void)a;(void)b;return open_ok;}
uint32_t storage_file_size(File* f){(void)f;return file_size;}
bool storage_file_seek(File* f,uint32_t at,bool absolute){assert(absolute && at<=file_size);if(!seek_ok)return false;f->position=at;return true;}
uint16_t storage_file_read(File* f,uint8_t* bytes,size_t n){
    assert(f->position+n<=file_size && n<=MAX_DATA_SIZE);reads++;
    if(reads==short_read_at)n/=2;
    memcpy(bytes,contents+f->position,n);f->position+=(uint32_t)n;return (uint16_t)n;
}
void storage_file_close(File* f){assert(f==&file);closes++;}
void storage_file_free(File* f){assert(f==&file);}
int rpc_system_storage_get_file_error(File* f){assert(f==&file);return PB_CommandStatus_ERROR_STORAGE_INTERNAL;}
static const int PB_Main_fields=0;
void pb_release(const void* fields,PB_Main* p){(void)fields;free(p->content.storage_read_response.file.data);p->content.storage_read_response.file.data=NULL;}
void rpc_send_and_release(RpcSession* s,PB_Main* p){
    (void)s;assert(p->command_id==42 && p->command_status==PB_CommandStatus_OK);
    assert(p->content.storage_read_response.ranged && p->content.storage_read_response.has_file);
    Bytes* b=p->content.storage_read_response.file.data;assert(output_size+b->size<=sizeof(output));
    memcpy(output+output_size,b->bytes,b->size);output_size+=b->size;messages++;
    pb_release(&PB_Main_fields,p);
}
void rpc_send_and_release_empty(RpcSession* s,uint32_t id,int status){(void)s;assert(id==42 && status!=PB_CommandStatus_OK);errors++;}
/* @PRODUCTION@ */
static void reset(void){assert(allocations==0);output_size=0;closes=errors=messages=reads=short_read_at=0;open_ok=seek_ok=true;file_size=sizeof(contents);for(size_t i=0;i<sizeof(contents);i++)contents[i]=(uint8_t)i;}
int main(void){
    RpcSession session={0};RpcStorageSystem storage={.session=&session};
    PB_Main req={.command_id=42,.which_content=PB_Main_storage_read_request_tag,.content.storage_read_request={.path="/ext/test"}};
    reset();rpc_system_storage_read_process(&req,&storage);assert(output_size==64 && messages==8 && !errors && closes==1 && !allocations);
    reset();req.content.storage_read_request.offset=3;req.content.storage_read_request.size=21;
    rpc_system_storage_read_process(&req,&storage);assert(output_size==21 && !memcmp(output,contents+3,21) && messages==3 && !allocations);
    reset();req.content.storage_read_request.offset=60;req.content.storage_read_request.size=UINT32_MAX;
    rpc_system_storage_read_process(&req,&storage);assert(output_size==4 && !memcmp(output,contents+60,4));
    reset();req.content.storage_read_request.offset=UINT32_MAX;
    rpc_system_storage_read_process(&req,&storage);assert(output_size==0 && messages==1 && !errors);
    reset();req.content.storage_read_request.offset=8;req.content.storage_read_request.size=0;
    rpc_system_storage_read_process(&req,&storage);assert(output_size==56 && !memcmp(output,contents+8,56));
    reset();file_size=0;rpc_system_storage_read_process(&req,&storage);assert(messages==1 && !output_size && !errors);
    reset();seek_ok=false;rpc_system_storage_read_process(&req,&storage);assert(!messages && errors==1 && closes==1 && !allocations);
    reset();open_ok=false;rpc_system_storage_read_process(&req,&storage);assert(!messages && errors==1 && closes==1 && !allocations);
    reset();short_read_at=2;rpc_system_storage_read_process(&req,&storage);assert(messages==1 && output_size==8 && errors==1 && closes==1 && !allocations);
    return 0;
}
