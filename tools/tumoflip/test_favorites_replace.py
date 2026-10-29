"""Checked file replacement uses only an in-memory disposable filesystem."""
import re
from pathlib import Path
import unittest
from tools.tumoflip.test_subremote_lifecycle import compile_run

ROOT = Path(__file__).resolve().parents[2]


class FavoritesReplaceTests(unittest.TestCase):
    def test_original_or_verified_backup_survives_each_io_failure(self):
        fixture = (ROOT / "tools/tumoflip/subremote_save_host_fixture.h").read_text().split("/* SAVE */")[0]
        fixture = fixture.replace("static Entry original,backup;", "static Entry original,backup,replacement;")
        fixture = fixture.replace('return strstr(p,".tumo-bak")?&backup:&original;',
                                  'return !strcmp(p,"favorites.tmp")?&replacement:strstr(p,".tumo-bak")?&backup:&original;')
        fixture += r'''
#define FSE_NOT_EXIST 2
static int storage_common_stat(Storage*s,const char*p,void*i){(void)s;(void)i;return fail()?FSE_ERROR:entry(p)->exists?FSE_OK:FSE_NOT_EXIST;}
bool signal_file_replace(Storage*,const char*,const char*);
/* SIGNAL_IMPLEMENTATION */
static void reset_files(void){memset(&original,0,sizeof(original));memset(&backup,0,sizeof(backup));memset(&replacement,0,sizeof(replacement));
 original.exists=true;original.size=3;memcpy(original.data,"old",3);replacement.exists=true;replacement.size=3;memcpy(replacement.data,"new",3);operation=fail_at=fail_again=0;}
static bool old(Entry*e){return e->exists&&e->size==3&&!memcmp(e->data,"old",3);}
int main(void){reset_files();assert(signal_file_replace(&storage,"favorites.tmp","favorites.txt"));
 assert(original.size==3&&!memcmp(original.data,"new",3));unsigned total=operation;
 for(unsigned n=1;n<=total;n++){reset_files();fail_at=n;bool ok=signal_file_replace(&storage,"favorites.tmp","favorites.txt");
  assert(ok?!memcmp(original.data,"new",3):(old(&original)||old(&backup)));assert(replacement.exists);}
 reset_files();backup.exists=true;backup.size=4;memcpy(backup.data,"keep",4);
 assert(!signal_file_replace(&storage,"favorites.tmp","favorites.txt"));assert(old(&original)&&!memcmp(backup.data,"keep",4));
 reset_files();replacement.size=0;assert(signal_file_replace(&storage,"favorites.tmp","favorites.txt")&&original.exists&&!original.size);
 reset_files();original.exists=false;assert(signal_file_replace(&storage,"favorites.tmp","favorites.txt")&&original.size==3);
 return 0;}
'''
        implementation = re.sub(r'^#include[^\n]*\n', '', (ROOT / 'lib/signal_file/signal_file.c').read_text(), flags=re.M)
        compile_run(fixture, implementation)


if __name__ == '__main__':
    unittest.main()
