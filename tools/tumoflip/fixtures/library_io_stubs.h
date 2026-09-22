#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <mbedtls/sha256.h>
#define EXT_PATH(p) "/ext/" p
typedef struct {char root[1024];} Storage;
typedef enum {FSE_OK,FSE_NOT_EXIST,FSE_EXIST,FSE_INTERNAL} FS_Error;
enum {FSAM_READ,FSAM_WRITE,FSOM_OPEN_EXISTING,FSOM_CREATE_NEW};
typedef struct {uint64_t size;bool directory;} FileInfo;
typedef struct {Storage*s;FILE*fp;DIR*dir;FS_Error error;char path[1400];} File;
typedef struct {char text[8192];} FuriString;
static int64_t write_budget=-1,read_budget=-1;
static bool sync_failure,sd_failure,mkdir_failure,remove_failure;
static FS_Error ferr(void){return errno==ENOENT?FSE_NOT_EXIST:errno==EEXIST?FSE_EXIST:FSE_INTERNAL;}
static void local_path(Storage*s,const char*p,char*out){assert(!strncmp(p,"/ext/",5));snprintf(out,1400,"%s/%s",s->root,p+5);}
static FuriString*furi_string_alloc(void){return calloc(1,sizeof(FuriString));}
static const char*furi_string_get_cstr(FuriString*s){return s->text;}
static void furi_string_free(FuriString*s){free(s);}
static void furi_string_set(FuriString*s,const char*v){snprintf(s->text,sizeof(s->text),"%s",v);}
static size_t furi_string_size(FuriString*s){return strlen(s->text);}
static void furi_string_printf(FuriString*s,const char*f,...){va_list a;va_start(a,f);vsnprintf(s->text,sizeof(s->text),f,a);va_end(a);}
static FuriString*furi_string_alloc_printf(const char*f,...){FuriString*s=furi_string_alloc();va_list a;va_start(a,f);vsnprintf(s->text,sizeof(s->text),f,a);va_end(a);return s;}
static void furi_string_cat_printf(FuriString*s,const char*f,...){size_t n=strlen(s->text);va_list a;va_start(a,f);vsnprintf(s->text+n,sizeof(s->text)-n,f,a);va_end(a);}
static File*storage_file_alloc(Storage*s){File*f=calloc(1,sizeof(*f));f->s=s;return f;}
static bool storage_file_open(File*f,const char*p,int access,int mode){(void)access;local_path(f->s,p,f->path);int fd=open(f->path,mode==FSOM_CREATE_NEW?O_CREAT|O_EXCL|O_WRONLY:O_RDONLY,0600);if(fd<0){f->error=ferr();return false;}f->fp=fdopen(fd,mode==FSOM_CREATE_NEW?"wb":"rb");f->error=FSE_OK;return f->fp!=NULL;}
static uint64_t storage_file_size(File*f){struct stat s;return f->fp&&!fstat(fileno(f->fp),&s)?(uint64_t)s.st_size:0;}
static size_t storage_file_read(File*f,void*b,size_t n){if(!f->fp||read_budget==0){f->error=FSE_INTERNAL;return 0;}if(read_budget>0&&(uint64_t)read_budget<n)n=read_budget;size_t k=fread(b,1,n,f->fp);if(read_budget>0)read_budget-=k;if(ferror(f->fp))f->error=FSE_INTERNAL;return k;}
static size_t storage_file_write(File*f,const void*b,size_t n){if(!f->fp||write_budget==0){f->error=FSE_INTERNAL;return 0;}size_t asked=n;if(write_budget>0&&(uint64_t)write_budget<n)n=write_budget;size_t k=fwrite(b,1,n,f->fp);if(write_budget>0)write_budget-=k;if(k!=asked)f->error=FSE_INTERNAL;return k;}
static bool storage_file_sync(File*f){return f->fp&&!sync_failure&&!fflush(f->fp)&&!fsync(fileno(f->fp));}
static bool storage_file_close(File*f){if(!f->fp)return false;int r=fclose(f->fp);f->fp=NULL;return !r;}
static void storage_file_free(File*f){if(f->fp)fclose(f->fp);if(f->dir)closedir(f->dir);free(f);}
static FS_Error storage_file_get_error(File*f){return f->error;}
static FS_Error storage_common_stat(Storage*s,const char*p,FileInfo*i){char b[1400];local_path(s,p,b);struct stat st;if(stat(b,&st))return ferr();if(i){i->size=st.st_size;i->directory=S_ISDIR(st.st_mode);}return FSE_OK;}
static FS_Error storage_common_remove(Storage*s,const char*p){if(remove_failure)return FSE_INTERNAL;char b[1400];local_path(s,p,b);return unlink(b)?ferr():FSE_OK;}
static bool storage_simply_mkdir(Storage*s,const char*p){if(mkdir_failure)return false;char b[1400];local_path(s,p,b);return !mkdir(b,0700)||errno==EEXIST;}
static FS_Error storage_sd_status(Storage*s){(void)s;return sd_failure?FSE_INTERNAL:FSE_OK;}
static bool storage_dir_open(File*f,const char*p){local_path(f->s,p,f->path);f->dir=opendir(f->path);return f->dir!=NULL;}
static bool storage_dir_read(File*f,FileInfo*i,char*n,size_t cap){struct dirent*d;while((d=readdir(f->dir))){if(!strcmp(d->d_name,".")||!strcmp(d->d_name,".."))continue;char p[1800];snprintf(p,sizeof(p),"%s/%s",f->path,d->d_name);struct stat st;if(stat(p,&st))return false;i->directory=S_ISDIR(st.st_mode);i->size=st.st_size;snprintf(n,cap,"%s",d->d_name);return true;}return false;}
static bool file_info_is_dir(FileInfo*i){return i->directory;}
static uint32_t furi_hal_rtc_get_timestamp(void){return 123456;}
typedef void PluginManager;
static const void*firmware_api_interface;
enum{PluginManagerErrorNone};
static PluginManager*plugin_manager_alloc(const char*a,unsigned b,const void*c){(void)a;(void)b;(void)c;return NULL;}
static int plugin_manager_load_single(PluginManager*a,const char*b){(void)a;(void)b;return 1;}
static const void*plugin_manager_get_ep(PluginManager*a,unsigned b){(void)a;(void)b;return NULL;}
static void plugin_manager_free(PluginManager*a){(void)a;}
typedef struct{const char*appid;unsigned ep_api_version;const void*entry_point;}FlipperAppPluginDescriptor;
