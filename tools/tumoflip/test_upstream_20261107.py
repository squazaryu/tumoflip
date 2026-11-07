"""Source-owned Archive/CLI regressions for the November 7 upstream delta."""
from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
ARCHIVE = ROOT / 'applications/main/archive/helpers'

ARRAY_FIXTURE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define furi_assert assert
#define CLAMP(x,hi,lo) ((x)>(hi)?(hi):((x)<(lo)?(lo):(x)))
#define with_view_model(v,decl,body,update) do { decl=(v);body; } while(0)
typedef struct { unsigned value; } ArchiveFile_t;
typedef struct { size_t count; ArchiveFile_t items[512]; } Files;
typedef struct { Files* files; int32_t item_idx;uint32_t item_cnt,array_offset; } ArchiveBrowserViewModel;
typedef struct { ArchiveBrowserViewModel* view; } ArchiveBrowserView;
typedef int ArchiveTabEnum;
enum {ArchiveTabBrowser,TAB_LEFT};
static int updated;
size_t files_array_size(Files* a){return a->count;}
void files_array_remove_v(Files* a,size_t first,size_t last){
    assert(first<last && last<=a->count);
    memmove(a->items+first,a->items+last,(a->count-last)*sizeof(a->items[0]));
    a->count-=last-first;
}
void ArchiveFile_t_init(ArchiveFile_t* f){f->value=0;}
void ArchiveFile_t_clear(ArchiveFile_t* f){(void)f;}
void files_array_pop_at(ArchiveFile_t* f,Files* a,size_t i){
    assert(i<a->count);*f=a->items[i];files_array_remove_v(a,i,i+1);
}
void files_array_push_at(Files* a,size_t i,ArchiveFile_t f){
    assert(i<=a->count && a->count<512);
    memmove(a->items+i+1,a->items+i,(a->count-i)*sizeof(f));a->items[i]=f;a->count++;
}
void files_array_swap_at(Files* a,size_t first,size_t second){
    assert(first<a->count && second<a->count);ArchiveFile_t t=a->items[first];
    a->items[first]=a->items[second];a->items[second]=t;
}
ArchiveTabEnum archive_get_tab(ArchiveBrowserView* b){(void)b;return ArchiveTabBrowser;}
bool archive_is_home(ArchiveBrowserView* b){(void)b;return false;}
void archive_switch_tab(ArchiveBrowserView* b,int dir){(void)b;(void)dir;}
void archive_update_offset(ArchiveBrowserView* b){(void)b;updated++;}
'''


class NovemberSevenTests(unittest.TestCase):
    def test_archive_remove_rejects_empty_or_unloaded_selection(self):
        source = (ARCHIVE / 'archive_browser.c').read_text()
        actual = function(source, 'bool archive_is_item_in_array(')
        actual += '\n' + function(source, 'void archive_file_array_rm_selected(')
        native(ARRAY_FIXTURE + actual + r'''
int main(void){
    Files a={0};ArchiveBrowserViewModel m={.files=&a};ArchiveBrowserView b={&m};
    archive_file_array_rm_selected(&b);assert(m.item_cnt==0 && a.count==0);
    a.count=3;for(unsigned i=0;i<3;i++)a.items[i].value=i;
    m.item_cnt=10;m.array_offset=4;m.item_idx=3;
    archive_file_array_rm_selected(&b);assert(m.item_cnt==10 && a.count==3);
    m.item_idx=7;archive_file_array_rm_selected(&b);assert(m.item_cnt==10 && a.count==3);
    m.item_idx=5;archive_file_array_rm_selected(&b);
    assert(m.item_cnt==9 && a.count==2 && a.items[0].value==0 && a.items[1].value==2);
    assert(updated==4);
    return 0;
}
''')

    def test_archive_reorder_handles_empty_and_indices_above_255(self):
        source = (ARCHIVE / 'archive_browser.c').read_text()
        actual = function(source, 'void archive_file_array_swap(')
        native(ARRAY_FIXTURE + actual + r'''
int main(void){
    Files a={0};ArchiveBrowserViewModel m={.files=&a};ArchiveBrowserView b={&m};
    archive_file_array_swap(&b,-1);archive_file_array_swap(&b,1);assert(a.count==0);
    a.count=350;m.item_cnt=350;for(unsigned i=0;i<350;i++)a.items[i].value=i;
    m.item_idx=300;archive_file_array_swap(&b,1);
    assert(a.items[300].value==301 && a.items[301].value==300 && a.items[45].value==45);
    archive_file_array_swap(&b,-1);assert(a.items[299].value==301);
    m.item_idx=-1;archive_file_array_swap(&b,1);assert(a.count==350);
    m.item_idx=350;archive_file_array_swap(&b,-1);assert(a.count==350);
    m.array_offset=10;m.item_idx=20;archive_file_array_swap(&b,1);
    assert(a.items[20].value==20 && a.items[21].value==21);
    m.array_offset=0;m.item_idx=0;archive_file_array_swap(&b,-1);
    assert(a.items[0].value==349 && a.count==350);
    m.item_idx=349;archive_file_array_swap(&b,1);assert(a.items[349].value==349);
    return 0;
}
''')

    def test_failed_delete_keeps_favorite_and_browser_row(self):
        source = (ARCHIVE / 'archive_files.c').read_text()
        actual = function(source, 'void archive_delete_file(')
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#define furi_assert assert
#define RECORD_STORAGE "storage"
typedef struct { char value[256]; } FuriString;
typedef int Storage;typedef int FileInfo;typedef int ArchiveBrowserView;
enum {FSE_OK,FSE_DENIED};
static bool remove_ok,original_exists=true;
static unsigned unpins,removed_rows;
FuriString* furi_string_alloc_vprintf(const char* f,va_list a){
    FuriString* s=malloc(sizeof(*s));assert(s);vsnprintf(s->value,sizeof(s->value),f,a);return s;
}
const char* furi_string_get_cstr(FuriString* s){return s->value;}
void furi_string_free(FuriString* s){free(s);}
void* furi_record_open(const char* n){(void)n;return (void*)1;}
void furi_record_close(const char* n){(void)n;}
int storage_common_stat(Storage* s,const char* p,FileInfo* i){(void)s;(void)p;*i=0;return FSE_OK;}
bool file_info_is_dir(FileInfo* i){(void)i;return false;}
int storage_common_remove(Storage* s,const char* p){(void)s;(void)p;if(remove_ok)original_exists=false;return remove_ok?FSE_OK:FSE_DENIED;}
bool storage_simply_remove_recursive(Storage* s,const char* p){return storage_common_remove(s,p)==FSE_OK;}
bool archive_is_favorite(const char* f,...){(void)f;return true;}
bool archive_favorites_delete(const char* f,...){(void)f;unpins++;return true;}
void archive_file_array_rm_selected(ArchiveBrowserView* b){(void)b;removed_rows++;}
''' + actual + r'''
int main(void){
    ArchiveBrowserView b=0;
    archive_delete_file(&b,"%s","/ext/subghz/owned.sub");
    assert(original_exists && unpins==0 && removed_rows==0);
    remove_ok=true;archive_delete_file(&b,"%s","/ext/subghz/owned.sub");
    assert(!original_exists && unpins==1 && removed_rows==1);return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
