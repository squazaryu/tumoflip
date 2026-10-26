"""Synthetic PB4 samples only: no NFC field, SPI, DMA or device connection."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/nfc_field_sniffer"


class NfcFieldDecoderTests(unittest.TestCase):
    def test_short_reader_frame_bounded_noise_crc_and_output(self):
        production = ""
        for name in ("nfc_field_decoder.h", "nfc_field_decoder.c", "nfc_field_format.c"):
            path = APP / name
            if path.exists():
                production += re.sub(r"^#(?:include|pragma).*\n", "\n", path.read_text(), flags=re.M) + "\n"
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
''' + production + r'''
int main(void){
 nfc_field_decoder_reset(true);assert(nfc_field_decoder_workspace_size()<=16384);
 assert(!nfc_field_decoder_feed(NULL,1));uint8_t too_large[2049]={0};assert(!nfc_field_decoder_feed(too_large,sizeof(too_large)));
 uint8_t samples[320];memset(samples,16,sizeof(samples));
 // Modified-Miller SOF plus REQA0x26, LSB-first7 bits. Each pause is
 // six samples wide at the pinned26-cycle sampling period (64MHz CPU).
 for(unsigned k=0;k<8;k++){unsigned bit=k?((0x26>>(k-1))&1):0;unsigned center=(2324*k+(bit?1743:581)+50)/100;
  for(unsigned i=center-3;i<center+3;i++)samples[i]=0;
 }
 for(unsigned i=0;i<sizeof(samples);i+=32)assert(nfc_field_decoder_feed(samples+i,32));
 nfc_field_decoder_finish();const NfcFieldTrace*t=nfc_field_decoder_trace();
 assert(t->count>=1&&t->count<=NFC_FIELD_FRAME_CAP);
 bool reqa=false;for(unsigned i=0;i<t->count;i++)if(t->frames[i].direction=='R'&&t->frames[i].bits==7&&t->frames[i].data[0]==0x26)reqa=true;
 assert(reqa);
 // A four-bit card NAK0 is real data, not "all-zero noise".
 s_nfr=0;s_last_bits=4;const uint8_t nak=0;add_frame(1,5,'C',&nak,1,0);assert(s_nfr==1&&s_fr[0].b[0]==0&&s_fr[0].bits==4);
 const uint8_t halt[]={0x50,0};assert(nfc_field_crc_a(halt,sizeof(halt))==0xcd57);
 struct{char out[4096];uint64_t guard;}text={.guard=0x123456789abcdef};assert(nfc_field_decoder_format(text.out,sizeof(text.out)));assert(strstr(text.out,"26")&&text.guard==0x123456789abcdef);
 char small[2];assert(!nfc_field_decoder_format(small,sizeof(small)));
 nfc_field_decoder_reset(false);uint32_t random=7;
 for(unsigned n=0;n<10000;n++){for(unsigned i=0;i<64;i++){random=random*1664525u+1013904223u;samples[i]=random>>24;}assert(nfc_field_decoder_feed(samples,64));}
 nfc_field_decoder_discontinuity(false);nfc_field_decoder_finish();t=nfc_field_decoder_trace();
 assert(t->count<=NFC_FIELD_FRAME_CAP&&t->overruns==1);
 for(unsigned i=0;i<t->count;i++)assert(t->frames[i].size<=NFC_FIELD_FRAME_BYTES&&t->frames[i].bits<=NFC_FIELD_FRAME_BYTES*8);
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
