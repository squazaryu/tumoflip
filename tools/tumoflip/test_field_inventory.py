"""Explicit inventory is read-only evidence, not deletion or a file backup."""
import unittest
from tools.tumoflip.test_field_wallet import storage_native

SOURCES = ("field_io.h", "field_io.c", "field_inventory.h", "field_inventory.c")


class FieldInventoryTests(unittest.TestCase):
    def test_inventory_finds_changed_missing_new_and_rejects_bad_baselines(self):
        storage_native(r'''
static bool cancel(void*c,const char*p,uint32_t n){(void)c;(void)p;(void)n;return false;}
static unsigned counts[5];
static void visit(void*c,const char*p,FieldInventoryState state){(void)c;assert(library_path_valid(p)&&state<=FieldInventoryAdded);counts[state]++;}
static void put(Storage*s,const char*p,const char*t){File*f=storage_file_alloc(s);assert(storage_file_open(f,p,FSAM_WRITE,FSOM_CREATE_NEW));assert(storage_file_write(f,t,strlen(t))==strlen(t));assert(storage_file_close(f));storage_file_free(f);}
int main(int argc,char**argv){
 assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);
 assert(storage_simply_mkdir(&s,"/ext/apps_data"));assert(storage_simply_mkdir(&s,"/ext/subghz"));assert(storage_simply_mkdir(&s,"/ext/subghz/Nested"));
 put(&s,"/ext/subghz/One.sub","Original 1");put(&s,"/ext/subghz/Nested/Two.SUB","Original 2");put(&s,"/ext/subghz/Ignore.txt","Not a capture");
 const char*base="/ext/baseline.tinv";assert(field_inventory_create(&s,base,NULL,NULL));
 FieldInventoryStats stats;assert(field_inventory_compare(&s,base,&stats,visit,NULL,NULL));
 assert(stats.unchanged==2&&!stats.missing&&!stats.changed&&!stats.unreadable&&!stats.added);
 assert(!field_inventory_create(&s,base,NULL,NULL));
 assert(storage_common_remove(&s,"/ext/subghz/One.sub")==FSE_OK);put(&s,"/ext/subghz/One.sub","Changed");
 assert(storage_common_remove(&s,"/ext/subghz/Nested/Two.SUB")==FSE_OK);put(&s,"/ext/subghz/New.sub","New");
 assert(field_inventory_compare(&s,base,&stats,visit,NULL,NULL));
 assert(stats.changed==1&&stats.missing==1&&stats.added==1&&!stats.unreadable);
 assert(!field_inventory_create(&s,"/ext/cancel.tinv",cancel,NULL));
 assert(storage_common_stat(&s,"/ext/cancel.tinv",NULL)==FSE_NOT_EXIST);
 assert(!field_inventory_compare(&s,base,&stats,visit,cancel,NULL));
 write_budget=4;assert(!field_inventory_create(&s,"/ext/partial.tinv",NULL,NULL));write_budget=-1;
 assert(storage_common_stat(&s,"/ext/partial.tinv",NULL)==FSE_NOT_EXIST);
 sync_failure=true;assert(!field_inventory_create(&s,"/ext/sync.tinv",NULL,NULL));sync_failure=false;
 read_budget=1;assert(!field_inventory_compare(&s,base,&stats,visit,NULL,NULL));read_budget=-1;
 char host[1400];local_path(&s,base,host);FILE*f=fopen(host,"r+b");assert(f);assert(!fseek(f,-1,SEEK_END));int b=fgetc(f);assert(b>=0);assert(!fseek(f,-1,SEEK_END));fputc(b^1,f);assert(!fclose(f));
 assert(!field_inventory_compare(&s,base,&stats,visit,NULL,NULL));
 assert(storage_common_stat(&s,"/ext/subghz/New.sub",NULL)==FSE_OK);assert(storage_common_stat(&s,"/ext/subghz/One.sub",NULL)==FSE_OK);
 return 0;
}
''', SOURCES)


if __name__ == "__main__":
    unittest.main()
