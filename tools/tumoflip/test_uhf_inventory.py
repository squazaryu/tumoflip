"""Execute the bounded UCM601 inventory contract without any UART/RF device."""
from pathlib import Path
import unittest
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / 'applications/system/device_library'


def production():
    path = APP / 'uhf_inventory.c'
    return ('#include <assert.h>\n#include <string.h>\n'
            + f'#include "{APP / "uhf_inventory.h"}"\n'
            + (path.read_text().replace('#include "uhf_inventory.h"', '') if path.exists() else '')
            + r'''
static bool frame(UhfInventory*h,uint8_t cmd,const uint8_t*p,unsigned n,unsigned ts){
 uint8_t b[257]={0xA0,(uint8_t)(n+3),0,cmd};memcpy(b+4,p,n);uint8_t sum=0;
 for(unsigned i=0;i<n+4;i++)sum+=b[i];b[n+4]=(uint8_t)-sum;bool changed=false;
 for(unsigned i=0;i<n+5;i++)changed=uhf_inventory_feed(h,b[i],ts)||changed;return changed;}
''')


class UhfInventoryTests(unittest.TestCase):
    def test_version_checksum_address_and_explicit_command_allowlist(self):
        native(production() + r'''
int main(void){UhfInventory h;uhf_inventory_init(&h);uint8_t b[6];
 assert(uhf_inventory_command(0x72,b)==5&&!memcmp(b,(uint8_t[]){0xA0,3,0,0x72,0xEB},5));
 assert(uhf_inventory_command(0x89,b)==6&&!memcmp(b,(uint8_t[]){0xA0,4,0,0x89,1,0xD2},6));
 assert(uhf_inventory_command(0x8C,b)==5);
 for(unsigned i=0;i<256;i++)if(i!=0x72&&i!=0x89&&i!=0x8C)assert(!uhf_inventory_command(i,b));
 uint8_t ver[]={1,2,3};assert(frame(&h,0x72,ver,3,0)&&h.identified&&h.version[2]==3);
 uhf_inventory_init(&h);uint8_t bad[]={0xA0,6,0,0x72,1,2,3,0};
 for(unsigned i=0;i<sizeof(bad);i++)uhf_inventory_feed(&h,bad[i],0);
 assert(!h.identified&&h.errors);assert(frame(&h,0x72,ver,3,0));
 uhf_inventory_discontinuity(&h);assert(h.used==0);return 0;}
''')

    def test_exact_pc_length_metadata_dedup_bounded_table_and_short_status(self):
        native(production() + r'''
int main(void){UhfInventory h;uhf_inventory_init(&h);
 uint8_t p[]={1,0x30,0,0xE2,0,1,2,3,4,5,6,7,8,9,10,0x01,0x23,0x45,0x67,0x0D,0x3B,0x50};
 assert(frame(&h,0x89,p,sizeof(p),10)&&h.count==1&&h.tags[0].length==12);
 assert(h.tags[0].signal_raw==0x01234567&&h.tags[0].frequency_raw==0x0D3B50);
 assert(frame(&h,0x89,p,sizeof(p),20)&&h.count==1&&h.tags[0].seen==2&&h.tags[0].first==10&&h.tags[0].last==20);
 char hex[97];assert(uhf_inventory_epc(&h.tags[0],hex,sizeof(hex))&&!strcmp(hex,"E2000102030405060708090A"));
 assert(!uhf_inventory_epc(&h.tags[0],hex,24));
 unsigned count=h.count;p[1]=0xF8;assert(!frame(&h,0x89,p,sizeof(p),30)&&h.count==count);p[1]=0x30;
 assert(!frame(&h,0x89,p,sizeof(p)-1,30));
 for(unsigned i=1;i<40;i++){p[4]=i;frame(&h,0x89,p,sizeof(p),40+i);}assert(h.count==32&&h.full==8);
 uint8_t end[]={0x10};assert(!frame(&h,0x89,end,1,100)&&h.completed==1&&h.count==32);
 assert(!frame(&h,0x94,p,sizeof(p),100));return 0;}
''')

    def test_random_stream_resynchronization_and_buffer_variant(self):
        native(production() + r'''
int main(void){UhfInventory h;uhf_inventory_init(&h);uint32_t r=1;
 for(unsigned i=0;i<200000;i++){r=r*1664525+1013904223;uhf_inventory_feed(&h,r>>24,i);assert(h.used<=257&&h.count<=32);}
 uhf_inventory_discontinuity(&h);
 uint8_t p[]={8,0x10,0,1,2,3,4,0xAB,0xCD,0,0,0,9,0,0,3,1,4};
 assert(frame(&h,0x8A,p,sizeof(p),1)&&h.count==1&&h.tags[0].length==4&&h.tags[0].seen==4);
 assert(h.tags[0].signal_raw==9&&h.tags[0].frequency_raw==3&&h.tags[0].antenna==1);
 p[1]=0x30;assert(!frame(&h,0x8A,p,sizeof(p),2)&&h.count==1);return 0;}
''')


if __name__ == '__main__':
    unittest.main()
