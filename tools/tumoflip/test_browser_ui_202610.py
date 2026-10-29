"""Execute production browser copy, selection and compressed text placement bodies."""
from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
GUI = ROOT / "applications/services/gui"
ARCHIVE = ROOT / "applications/main/archive/helpers"

COPY_STUBS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct {char text[32];} FuriString;
static void furi_string_set(FuriString*d,const FuriString*s){*d=*s;}
static unsigned allocations;
static void* counted_malloc(size_t n){void*p=malloc(n);assert(p);allocations++;return p;}
static void counted_free(void*p){if(p){assert(allocations);allocations--;}free(p);}
#define malloc counted_malloc
#define free counted_free
#define CUSTOM_ICON_MAX_SIZE 32
#define FAP_MANIFEST_MAX_ICON_SIZE 32
typedef struct {int type;uint32_t unsorted_idx;FuriString*name,*display_name;uint8_t*custom_icon_data;} BrowserItem_t;
typedef struct {int type;FuriString*path,*custom_name;uint8_t*custom_icon_data;bool fav,is_app,is_text_file;} ArchiveFile_t;
'''


class BrowserUiRegressionTests(unittest.TestCase):
    def test_browser_icon_copy_allocates_and_owns_its_buffer(self):
        source = (GUI / "modules/file_browser.c").read_text()
        native(COPY_STUBS + function(source, "static void BrowserItem_t_set(") + r'''
int main(void){FuriString name={0},display={0};BrowserItem_t a={.name=&name,.display_name=&display},b=a;
 a.custom_icon_data=malloc(32);memset(a.custom_icon_data,0xab,32);
 for(unsigned i=0;i<100;i++){BrowserItem_t_set(&b,&a);assert(allocations==2);assert(b.custom_icon_data!=a.custom_icon_data);assert(!memcmp(a.custom_icon_data,b.custom_icon_data,32));}
 BrowserItem_t_set(&b,&b);assert(allocations==2&&b.custom_icon_data[0]==0xab);
 free(a.custom_icon_data);a.custom_icon_data=NULL;BrowserItem_t_set(&b,&a);assert(!b.custom_icon_data&&!allocations);return 0;}
''')

    def test_archive_icon_assignment_does_not_leak_or_break_self_assignment(self):
        source = (ARCHIVE / "archive_files.h").read_text()
        native(COPY_STUBS + function(source, "static void ArchiveFile_t_set(") + r'''
int main(void){FuriString path={0},name={0};ArchiveFile_t a={.path=&path,.custom_name=&name},b=a;
 a.custom_icon_data=malloc(32);memset(a.custom_icon_data,0x5a,32);
 for(unsigned i=0;i<100;i++){ArchiveFile_t_set(&b,&a);assert(allocations==2);assert(b.custom_icon_data!=a.custom_icon_data);}
 ArchiveFile_t_set(&b,&b);assert(allocations==2&&b.custom_icon_data[0]==0x5a);
 free(a.custom_icon_data);a.custom_icon_data=NULL;ArchiveFile_t_set(&b,&a);assert(!b.custom_icon_data&&!allocations);return 0;}
''')

    def test_unloaded_archive_selection_is_not_clamped_to_a_different_file(self):
        source = (ARCHIVE / "archive_browser.c").read_text()
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define furi_assert assert
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define CLAMP(x,hi,lo) MIN(MAX(x,lo),hi)
#define with_view_model(view,decl,body,update) do{decl=(view);body;}while(0)
typedef struct {int value;} ArchiveFile_t;
typedef struct {ArchiveFile_t data[3];size_t size;} Array;
typedef struct {Array files;int32_t item_idx,array_offset;} ArchiveBrowserViewModel;
typedef struct {ArchiveBrowserViewModel*view;} ArchiveBrowserView;
static size_t files_array_size(Array a){return a.size;}
#define files_array_get(a,i) ((assert((size_t)(i)<(a).size)),&(a).data[(i)])
''' + function(source, "bool archive_is_item_in_array(") + "\n" +
               function(source, "ArchiveFile_t* archive_get_current_file(") + "\n" +
               function(source, "ArchiveFile_t* archive_get_file_at(") + r'''
int main(void){ArchiveBrowserViewModel m={.files={.size=3},.array_offset=300,.item_idx=299};ArchiveBrowserView b={&m};
 assert(!archive_get_current_file(&b));assert(!archive_get_file_at(&b,299));assert(!archive_get_file_at(&b,303));
 for(int32_t i=300;i<303;i++){m.item_idx=i;assert(archive_get_current_file(&b)==&m.files.data[i-300]);assert(archive_get_file_at(&b,i)==&m.files.data[i-300]);}
 m.files.size=0;assert(!archive_get_file_at(&b,300));assert(!archive_get_current_file(&b));return 0;}
''')

    def test_compressed_text_places_every_row_even_with_no_spare_pixels(self):
        source = (GUI / "elements.c").read_text()
        placement = source.split("// Set vertical alignment for all lines", 1)[1].split("// Draw line by line", 1)[0]
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
enum {AlignTop,AlignCenter,AlignBottom};
typedef struct {int32_t y,leading_min,leading_default;size_t height;} Line;
static void place(Line*line,size_t line_num,size_t height,size_t total_height_default,size_t total_height_min,int y,int vertical){
''' + placement + r'''
}
int main(void){for(size_t n=1;n<=6;n++){for(size_t spare=0;spare<4;spare++){
 Line rows[6]={0};for(size_t i=0;i<n;i++)rows[i]=(Line){.leading_min=8,.leading_default=12,.height=7};
 size_t minimum=7+(n-1)*8;place(rows,n,minimum+spare,minimum+12,minimum,3,AlignCenter);
 assert(rows[0].y==10);for(size_t i=1;i<n;i++)assert(rows[i].y>=rows[i-1].y+8);
 if(n>1)assert(rows[n-1].y==10+(int)((n-1)*8+spare));
 }}return 0;}
''')


if __name__ == "__main__":
    unittest.main()
