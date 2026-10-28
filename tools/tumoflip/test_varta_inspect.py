"""Own LF observations/dumps; no reader credential capture or tag commands."""
from pathlib import Path
import unittest
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / 'applications/system/device_library'


def production():
    path = APP / 'varta_inspect.c'
    return ('#include <assert.h>\n#include <string.h>\n'
            + f'#include "{APP / "varta_inspect.h"}"\n'
            + (path.read_text().replace('#include "varta_inspect.h"', '') if path.exists() else '')
            + r'''
static uint64_t encode(const uint8_t id[5]){uint64_t f=0x1FF;unsigned columns=0;
 for(unsigned i=0;i<10;i++){unsigned n=(id[i/2]>>(i%2?0:4))&15U,p=0;columns^=n;
  for(unsigned b=0;b<4;b++)p^=(n>>b)&1U;f=(f<<5)|(n<<1)|p;}
 return (f<<5)|(columns<<1);}
''')


class VartaInspectionTests(unittest.TestCase):
    def test_em4100_frame_all_single_bit_corruptions_and_atomic_result(self):
        native(production() + r'''
int main(void){uint8_t id[]={3,0,0x12,0x34,0x56},out[5];uint64_t f=encode(id);
 assert(varta_inspect_frame(f>>32,f,out)&&!memcmp(id,out,5));
 for(unsigned i=0;i<64;i++){uint64_t bad=f^(1ULL<<i);memset(out,0xAA,5);assert(!varta_inspect_frame(bad>>32,bad,out));assert(out[0]==0xAA);}
 uint32_t random=3;for(unsigned k=0;k<10000;k++){for(unsigned i=0;i<5;i++){random=random*1664525+1013904223;id[i]=random>>24;}f=encode(id);assert(varta_inspect_frame(f>>32,f,out)&&!memcmp(out,id,5));}
 return 0;}
''')

    def test_prefix_is_not_authenticity_and_read_mask_is_not_padding(self):
        native(production() + r'''
int main(void){uint8_t mask;assert(varta_inspect_read_list("0 1 2 6 7",&mask)&&mask==0xC7);
 assert(!varta_inspect_read_list("4294967299",&mask));assert(!varta_inspect_read_list("1,2",&mask));assert(!varta_inspect_read_list("1 1",&mask));
 uint8_t id[]={4,0,1,2,3};VartaInspection v;varta_inspect_id(&v,id,64);assert(v.status==VartaIdOnly&&v.known_prefix&&!v.has_marker);
 uint64_t f=encode(id);uint32_t blocks[8]={0x00148050,f>>32,f,0,0,0,0xFFAD2D00,0};
 varta_inspect_dump(&v,blocks,0);assert(v.status==VartaMissingBlocks&&!v.has_marker);
 varta_inspect_dump(&v,blocks,0x47);assert(v.status==VartaDumpDecoded&&v.known_prefix&&v.config_matches&&v.marker_matches);
 blocks[6]^=1;varta_inspect_dump(&v,blocks,0x47);assert(v.has_marker&&!v.marker_matches);
 blocks[1]^=1;varta_inspect_dump(&v,blocks,0x47);assert(v.status==VartaBadFrame);return 0;}
''')


if __name__ == '__main__': unittest.main()
