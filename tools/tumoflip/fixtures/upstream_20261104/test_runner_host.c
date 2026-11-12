#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define furi_assert assert
#define FURI_LOG_D(...) ((void)0)
#define FURI_LOG_E(...) ((void)0)
#define EXT_PATH(x) "/ext/" x
#define APPID "UnitTest"
#define API_VERSION 0
#define PLUGINS_PATH "/ext/apps_data/unit_tests/plugins"
#define TEST_RUNNER_TMP_DIR "/ext/.tmp"
#define TEST_RUNNER_TMP_UNIT_TESTS_DIR "/ext/.tmp/unit_tests"
typedef int Storage;
typedef int Loader;
typedef int NotificationApp;
typedef int PipeSide;
typedef int CompositeApiResolver;
typedef struct {char text[512];} FuriString;
typedef struct {unsigned index;} File;
typedef struct TestRunner TestRunner;
typedef struct {int(*run)(void);int(*get_minunit_run)(void);int(*get_minunit_assert)(void);int(*get_minunit_status)(void);} TestApi;
typedef struct {const char* appid;unsigned ep_api_version;const void* entry_point;} FlipperAppPluginDescriptor;
typedef struct {bool fail,foreign;} PluginManager;
typedef PluginManager FlipperApplication;
typedef int PluginManagerError;
enum {PluginManagerErrorNone,PluginManagerErrorInvalidFile};
enum {FlipperApplicationPreloadStatusSuccess,FlipperApplicationLoadStatusSuccess};
typedef int FlipperApplicationPreloadStatus;
typedef int FlipperApplicationLoadStatus;
typedef int NotificationSequence;
static const int sequence_error=1,sequence_success=2,sequence_blink_magenta_100=3,sequence_set_only_blue_255=4;
static const char* suites[4];
static unsigned suite_count,started,live,last_notification;
static bool directory_ok=true,locked,interrupt_run;
static unsigned mkdir_fail,mkdir_calls,heap_calls;
static int heap_change;
static bool current_failed;
static char output[8192];
int captured_printf(const char* format,...){
    va_list args;va_start(args,format);int n=vsnprintf(output+strlen(output),sizeof(output)-strlen(output),format,args);va_end(args);return n;
}
#define printf captured_printf
bool loader_is_locked(Loader* p){(void)p;return locked;}
void notification_message(NotificationApp* p,const int* sequence){(void)p;last_notification=*sequence;}
void notification_message_block(NotificationApp* p,const int* sequence){notification_message(p,sequence);}
unsigned long furi_get_tick(void){return 100;}
uint32_t memmgr_get_free_heap(void){return heap_calls++?12000+heap_change:12000;}
void furi_delay_ms(unsigned n){(void)n;}
FuriString* furi_string_alloc(void){live++;return calloc(1,sizeof(FuriString));}
void furi_string_free(FuriString* s){assert(live);live--;free(s);}
void furi_string_set(FuriString* s,const char* p){assert(strlen(p)<sizeof(s->text));strcpy(s->text,p);}
bool furi_string_end_with_str(FuriString* s,const char* suffix){size_t n=strlen(s->text),m=strlen(suffix);return n>=m&&!strcmp(s->text+n-m,suffix);}
const char* furi_string_get_cstr(FuriString* s){return s->text;}
size_t furi_string_size(FuriString* s){return strlen(s->text);}
int furi_string_cmp_str(FuriString* s,const char* p){return strcmp(s->text,p);}
void path_concat(const char* a,const char* b,FuriString* out){snprintf(out->text,sizeof(out->text),"%s/%s",a,b);}
void path_extract_filename(FuriString* in,FuriString* out,bool extension){
    const char* base=strrchr(in->text,'/');strcpy(out->text,base?base+1:in->text);
    if(extension){char* dot=strrchr(out->text,'.');if(dot)*dot=0;}
}
File* storage_file_alloc(Storage* p){(void)p;live++;return calloc(1,sizeof(File));}
void storage_file_free(File* p){assert(live);live--;free(p);}
bool storage_simply_mkdir(Storage* p,const char* name){(void)p;(void)name;return ++mkdir_calls!=mkdir_fail;}
bool storage_dir_open(File* p,const char* name){(void)name;p->index=0;return directory_ok;}
bool storage_dir_read(File* p,void* info,char* name,size_t size){
    (void)info;if(p->index>=suite_count)return false;assert(strlen(suites[p->index])<size);strcpy(name,suites[p->index++]);return true;
}
void storage_dir_close(File* p){(void)p;}
bool cli_is_pipe_broken_or_is_etx_next_char(PipeSide* p){(void)p;return interrupt_run;}
void* composite_api_resolver_get(CompositeApiResolver* p){return p;}
static int suite_run(void){started++;return current_failed?1:0;}
static int suite_tests(void){return 2;}
static int suite_asserts(void){return 3;}
static int suite_status(void){return current_failed;}
static const TestApi test_api={suite_run,suite_tests,suite_asserts,suite_status};
PluginManager* plugin_manager_alloc(const char* id,unsigned version,void* resolver){
    (void)resolver;assert(!strcmp(id,APPID)&&version==API_VERSION);live++;return calloc(1,sizeof(PluginManager));
}
PluginManagerError plugin_manager_load_single(PluginManager* p,const char* path){
    p->fail=strstr(path,"failed")!=NULL;p->foreign=strstr(path,"foreign")!=NULL;
    return p->foreign?PluginManagerErrorInvalidFile:PluginManagerErrorNone;
}
const void* plugin_manager_get_ep(PluginManager* p,unsigned index){assert(index==0);current_failed=p->fail;return p->foreign?NULL:&test_api;}
void plugin_manager_free(PluginManager* p){assert(live);live--;free(p);}
FlipperApplication* flipper_application_alloc(Storage* s,void* r){(void)s;(void)r;live++;return calloc(1,sizeof(FlipperApplication));}
int flipper_application_preload(FlipperApplication* p,const char* path){p->fail=strstr(path,"failed")!=NULL;p->foreign=strstr(path,"foreign")!=NULL;return 0;}
bool flipper_application_is_plugin(FlipperApplication* p){(void)p;return true;}
int flipper_application_map_to_memory(FlipperApplication* p){current_failed=p->fail;return 0;}
const FlipperAppPluginDescriptor* flipper_application_plugin_get_descriptor(FlipperApplication* p){
    static FlipperAppPluginDescriptor descriptor;descriptor=(FlipperAppPluginDescriptor){p->foreign?"NotUnitTest":APPID,0,&test_api};return &descriptor;
}
void flipper_application_free(FlipperApplication* p){plugin_manager_free(p);}
/* STRUCT */
/* PRODUCTION */
int main(int argc,char** argv){
    assert(argc==2);FuriString args={0};Storage s=0;Loader l=0;NotificationApp n=0;
    TestRunner runner={.storage=&s,.loader=&l,.notification=&n,.args=&args};
    if(!strcmp(argv[1],"empty")){
        test_runner_run(&runner);assert(last_notification==sequence_error);assert(strstr(output,"Status: FAILED"));
    }else if(!strcmp(argv[1],"missing")){
        suite_count=1;suites[0]="test_good.fal";strcpy(args.text,"missing");
        test_runner_run(&runner);assert(started==0&&last_notification==sequence_error);assert(strstr(output,"No suite named"));
    }else if(!strcmp(argv[1],"aggregate")){
        suite_count=2;suites[0]="test_failed.fal";suites[1]="test_good.fal";
        test_runner_run(&runner);assert(started==2&&runner.minunit_run==4&&runner.minunit_fail==1);
        assert(last_notification==sequence_error&&strstr(output,"Status: FAILED"));
    }else if(!strcmp(argv[1],"foreign")){
        suite_count=1;suites[0]="test_foreign.fal";
        test_runner_run(&runner);assert(started==0&&last_notification==sequence_error);
    }else if(!strcmp(argv[1],"directory")){
        directory_ok=false;test_runner_run(&runner);assert(last_notification==sequence_error&&strstr(output,"Status: FAILED"));
    }else if(!strcmp(argv[1],"mkdir1")||!strcmp(argv[1],"mkdir2")){
        mkdir_fail=argv[1][5]-'0';test_runner_run(&runner);assert(last_notification==sequence_error&&strstr(output,"Status: FAILED"));
    }else if(!strcmp(argv[1],"interrupt")){
        interrupt_run=true;test_runner_run(&runner);assert(last_notification==sequence_error&&strstr(output,"Interrupted"));
    }else if(!strcmp(argv[1],"locked")){
        locked=true;test_runner_run(&runner);assert(last_notification==sequence_blink_magenta_100&&strstr(output,"Status: FAILED"));
    }else{
        suite_count=1;suites[0]="test_good.fal";runner.minunit_run=99;runner.minunit_fail=5;
        if(!strcmp(argv[1],"gain"))heap_change=1000;
        if(!strcmp(argv[1],"leak"))heap_change=-1000;
        test_runner_run(&runner);assert(runner.minunit_run==2&&runner.minunit_fail==0&&started==1);
        assert(last_notification==sequence_success&&strstr(output,"Status: PASSED"));
        assert(strstr(output,heap_change<0?"Leaked: 1000":"Leaked: 0"));
    }
    assert(live==0);return 0;
}
