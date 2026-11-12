"""Run real round/storage bodies against a disposable SD, never the device."""
import unittest
from tools.tumoflip.test_field_wallet import storage_native

SOURCES = ("field_io.h", "field_io.c", "field_round.h", "field_round.c")


class FieldRoundsTests(unittest.TestCase):
    def test_updated_round_exports_a_new_report_without_replacing_the_old_one(self):
        storage_native(r'''
int main(int argc,char**argv){assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);assert(storage_simply_mkdir(&s,"/ext/apps_data"));
 LibraryPins pins={.count=1,.names={"Office"}};FieldRound r={0};char old[256],next[256];
 assert(field_round_begin(&r,&pins,100)&&field_round_save(&s,&r));assert(field_round_export(&s,&r,old,sizeof(old)));
 assert(field_round_mark(&r,0,FieldRoundPassed,"Checked",101)&&field_round_save(&s,&r));
 assert(field_round_export(&s,&r,next,sizeof(next))&&strcmp(old,next));assert(storage_common_stat(&s,old,NULL)==FSE_OK);return 0;}
''', SOURCES)

    def test_generation_corruption_cannot_override_the_last_valid_round(self):
        storage_native(r'''
int main(int argc,char**argv){assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);assert(storage_simply_mkdir(&s,"/ext/apps_data"));
 LibraryPins pins={.count=1,.names={"Office"}};FieldRound r={0},read;
 assert(field_round_begin(&r,&pins,100)&&field_round_save(&s,&r));assert(field_round_mark(&r,0,FieldRoundPassed,"Checked",101)&&field_round_save(&s,&r));
 char path[1400];local_path(&s,"/ext/apps_data/device_library/round/1.record",path);FILE*f=fopen(path,"r+b");assert(f);uint32_t corrupted=0xffffffff;assert(!fseek(f,4,SEEK_SET));assert(fwrite(&corrupted,1,4,f)==4);assert(!fclose(f));
 assert(field_round_load(&s,&read)&&read.items[0].result==FieldRoundPending);return 0;}
''', SOURCES)

    def test_round_requires_explicit_results_and_keeps_previous_record_on_error(self):
        storage_native(r'''
int main(int argc,char**argv){
 assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);
 assert(storage_simply_mkdir(&s,"/ext/apps_data"));
 LibraryPins pins={.count=2,.names={"Office","Garage"}};
 FieldRound r={0},loaded={0};
 assert(field_round_begin(&r,&pins,100));assert(r.count==2&&r.started==100);
 assert(r.items[0].result==FieldRoundPending&&!field_round_complete(&r));
 assert(field_round_save(&s,&r));assert(field_round_load(&s,&loaded));
 assert(loaded.items[0].result==FieldRoundPending);
 assert(field_round_mark(&r,0,FieldRoundPassed,"Own receiver checked",101));
 assert(!field_round_mark(&r,1,FieldRoundPending,"",101));
 assert(!field_round_mark(&r,2,FieldRoundPassed,"",101));
 assert(!field_round_mark(&r,1,FieldRoundProblem,"bad\ntext",101));
 assert(!field_round_mark(&r,1,FieldRoundSkipped,"",99));
 assert(field_round_save(&s,&r));
 assert(field_round_mark(&r,1,FieldRoundProblem,"No response",102));
 write_budget=1;assert(!field_round_save(&s,&r));write_budget=-1;
 assert(field_round_load(&s,&loaded)&&loaded.items[0].result==FieldRoundPassed&&loaded.items[1].result==FieldRoundPending);
 sync_failure=true;assert(!field_round_save(&s,&r));sync_failure=false;
 assert(field_round_load(&s,&loaded)&&loaded.items[1].result==FieldRoundPending);
 close_failure=true;assert(!field_round_save(&s,&r));close_failure=false;
 assert(field_round_load(&s,&loaded)&&loaded.items[1].result==FieldRoundPending);
 assert(field_round_save(&s,&r)&&field_round_complete(&r));
 assert(field_round_load(&s,&loaded)&&loaded.items[1].result==FieldRoundProblem);
 char report[256];assert(field_round_export(&s,&loaded,report,sizeof(report)));
 File*f=storage_file_alloc(&s);assert(storage_file_open(f,report,FSAM_READ,FSOM_OPEN_EXISTING));
 char text[2048]={0};assert(storage_file_read(f,text,sizeof(text)-1)>0);assert(strstr(text,"Office\tPASS")&&strstr(text,"Garage\tPROBLEM"));assert(storage_file_close(f));storage_file_free(f);
 assert(!field_round_export(&s,&loaded,report,sizeof(report))); // CREATE_NEW collision
 return 0;
}
''', SOURCES)


if __name__ == "__main__":
    unittest.main()
