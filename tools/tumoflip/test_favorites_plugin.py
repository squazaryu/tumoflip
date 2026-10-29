"""Run the real lazy favorites writer with memory-only files and I/O faults."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_subremote_lifecycle import compile_run

ROOT = Path(__file__).resolve().parents[2]


class FavoritesPluginTests(unittest.TestCase):
    def test_transform_exact_paths_and_preserve_recovery_on_io_failure(self):
        fixture = (ROOT / 'tools/tumoflip/subremote_save_host_fixture.h').read_text().split('/* SAVE */')[0]
        fixture = fixture.replace('static Entry original,backup;', 'static Entry original,backup,replacement;')
        fixture = fixture.replace('return strstr(p,".tumo-bak")?&backup:&original;',
                                  'return strstr(p,"favorites.tmp")?&replacement:strstr(p,".tumo-bak")?&backup:&original;')
        fixture = fixture.replace('if(fail())return 0;n=MIN(n,f->entry->size-f->pos);',
                                  'if(fail()){f->error=FSE_ERROR;return 0;}n=MIN(n,f->entry->size-f->pos);')
        strip = lambda text: re.sub(r'^#(?:include|pragma)[^\n]*\n', '', text, flags=re.M)
        header = strip((ROOT / 'applications/main/archive/helpers/archive_favorites_plugin.h').read_text())
        writer = strip((ROOT / 'applications/main/archive/plugins/favorites_writer.c').read_text())
        library = strip((ROOT / 'lib/signal_file/signal_file.c').read_text())
        program = fixture + r'''
#define EXT_PATH(p) "/ext/" p
#define ARCHIVE_FAV_PATH "/ext/favorites.txt"
#define ARCHIVE_FAV_TEMP_PATH "/ext/favorites.tmp"
typedef struct {const char*appid;unsigned ep_api_version;const void*entry_point;} FlipperAppPluginDescriptor;
static void furi_string_reset(FuriString*s){s->value[0]=0;}
static size_t furi_string_size(FuriString*s){return strlen(s->value);}
static void furi_string_push_back(FuriString*s,char c){size_t n=strlen(s->value);assert(n+1<sizeof(s->value));s->value[n]=c;s->value[n+1]=0;}
static size_t furi_string_search(FuriString*s,const char*p){char*m=strstr(s->value,p);return m?(size_t)(m-s->value):SIZE_MAX;}
static uint32_t storage_file_tell(File*f){return f->pos;}
static FS_Error storage_common_remove(Storage*s,const char*p){return storage_simply_remove(s,p)?FSE_OK:FSE_ERROR;}
bool signal_file_replace(Storage*,const char*,const char*);
''' + header + writer + library + r'''
static const char old[]="/ext/subghz/a.sub\n/ext/subghz/a.sub-long\n";
static void reset_list(void){memset(&original,0,sizeof(original));memset(&backup,0,sizeof(backup));memset(&replacement,0,sizeof(replacement));
 original.exists=true;original.size=strlen(old);memcpy(original.data,old,original.size);operation=fail_at=fail_again=0;}
static bool old_list(Entry*e){return e->exists&&e->size==strlen(old)&&!memcmp(e->data,old,e->size);}
static const char* at(void*c,size_t i){(void)c;return i?"/ext/subghz/second.sub":"/ext/subghz/first.sub";}
int main(void){ArchiveFavoriteRequest r={.edit=FavoriteDelete,.source="/ext/subghz/a.sub"};
 reset_list();assert(archive_favorites_write(&r));assert(original.size==strlen("/ext/subghz/a.sub-long\n")&&!memcmp(original.data,"/ext/subghz/a.sub-long\n",original.size));
 unsigned total=operation;for(unsigned n=1;n<=total;n++){reset_list();fail_at=n;bool ok=archive_favorites_write(&r);if(!ok&&!old_list(&original)&&!old_list(&backup))fprintf(stderr,"failed operation %u/%u original=%zu backup=%d temp=%d\n",n,total,original.size,backup.exists,replacement.exists);assert(ok || old_list(&original)||old_list(&backup));}
 reset_list();replacement.exists=true;replacement.size=4;memcpy(replacement.data,"keep",4);assert(!archive_favorites_write(&r));assert(old_list(&original)&&!memcmp(replacement.data,"keep",4));
 reset_list();r.edit=FavoriteRename;r.destination="/ext/subghz/new.sub";assert(archive_favorites_write(&r));assert(!memcmp(original.data,"/ext/subghz/new.sub\n/ext/subghz/a.sub-long\n",original.size));
 reset_list();r=(ArchiveFavoriteRequest){.edit=FavoriteReorder,.count=2,.path_at=at};assert(archive_favorites_write(&r));assert(!memcmp(original.data,"/ext/subghz/first.sub\n/ext/subghz/second.sub\n",original.size));
 reset_list();r=(ArchiveFavoriteRequest){.edit=FavoriteAppend,.source="/ext/subghz/new.sub"};assert(archive_favorites_write(&r));assert(!memcmp(original.data,old,strlen(old)));
 assert(!archive_favorites_write(NULL));return 0;}
'''
        compile_run(program)


if __name__ == '__main__': unittest.main()
