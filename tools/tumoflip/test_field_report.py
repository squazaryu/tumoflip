"""Create-new, read-back-verified exports on a disposable SD directory."""
import unittest
from tools.tumoflip.test_field_wallet import storage_native


class FieldReportTests(unittest.TestCase):
    def test_create_new_verified_report_failure_and_collision(self):
        storage_native(r'''
static bool row(void*c,size_t i,char*out,size_t n){(void)c;return snprintf(out,n,"%u,123\n",(unsigned)i)>0;}
static bool fail_row(void*c,size_t i,char*out,size_t n){(void)c;(void)i;(void)out;(void)n;return false;}
int main(int argc,char**argv){assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);char a[256],b[256];
 assert(field_report_save(&s,"uhf",42,"id,value\n",32,row,NULL,a,sizeof(a)));
 assert(field_report_save(&s,"uhf",42,"id,value\n",32,row,NULL,b,sizeof(b))&&strcmp(a,b));
 assert(!field_report_save(&s,"../evil",42,"id\n",1,row,NULL,b,sizeof(b)));
 assert(!field_report_save(&s,"uhf",42,"id\n",1,row,NULL,b,4));
 assert(!field_report_save(&s,"uhf",42,"id\n",1,fail_row,NULL,b,sizeof(b)));
 write_budget=1;assert(!field_report_save(&s,"uhf",42,"id\n",1,row,NULL,b,sizeof(b)));write_budget=-1;
 sync_failure=true;assert(!field_report_save(&s,"uhf",42,"id\n",1,row,NULL,b,sizeof(b)));sync_failure=false;
 close_failure=true;assert(!field_report_save(&s,"uhf",42,"id\n",1,row,NULL,b,sizeof(b)));close_failure=false;
 assert(storage_common_stat(&s,a,NULL)==FSE_OK);
 for(unsigned i=2;i<32;i++)assert(field_report_save(&s,"uhf",42,"id\n",1,row,NULL,b,sizeof(b)));
 assert(!field_report_save(&s,"uhf",42,"id\n",1,row,NULL,b,sizeof(b)));return 0;}
''', ('field_io.h', 'field_io.c', 'field_report.h', 'field_report.c'))


if __name__ == '__main__': unittest.main()
