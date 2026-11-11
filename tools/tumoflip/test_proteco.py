"""Proteco MOVER: upstream public captures plus bounded synthetic framing."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_upstream_20261104 import PRELUDE,declarations
from tools.tumoflip.test_upstream_20261105 import production_function

ROOT=Path(__file__).resolve().parents[2]
PATH=ROOT/'lib/subghz/protocols/proteco.c'


def context(source):
    return (PRELUDE.replace('unsigned repeat;', 'unsigned repeat,front;bool is_running;')
            +'typedef struct SubGhzProtocolDecoderProteco SubGhzProtocolDecoderProteco;\n'
            +'typedef struct SubGhzProtocolEncoderProteco SubGhzProtocolEncoderProteco;\n'
            +declarations(source,'proteco')+'\n'
            +'\n'.join(re.findall(r'(?m)^#define PROTECO_.*$',source))+'\n')


class ProtecoTests(unittest.TestCase):
    def require_source(self):
        if not PATH.exists():
            native('void subghz_protocol_decoder_proteco_reset(void*);\n'
                   'int main(void){subghz_protocol_decoder_proteco_reset(0);return 0;}')
        return PATH.read_text()

    def test_captured_buttons_and_noise_do_not_require_any_button_remapping(self):
        source=self.require_source()
        actual='\n'.join(production_function(source,n) for n in (
            'subghz_protocol_decoder_proteco_reset','subghz_protocol_decoder_proteco_feed'))
        vectors=''
        for index,name in enumerate(('proteco_raw','proteco_b_raw','proteco_c_raw')):
            text=(ROOT/'tools/tumoflip/fixtures/proteco'/f'{name}.sub').read_text()
            values=[int(v) for line in text.splitlines() if line.startswith('RAW_Data:') for v in line.split(':',1)[1].split()]
            vectors+='static const int32_t capture'+str(index)+'[]={'+','.join(map(str,values))+'};\n'
        native(context(source)+actual+vectors+r'''
static void receive(SubGhzProtocolDecoderBase* b,void* c){(void)c;
 assert(((SubGhzProtocolDecoderProteco*)b)->generic.data==expected);received++;}
int main(void){
 (void)subghz_protocol_proteco_const;
 const int32_t* captures[]={capture0,capture1,capture2};
 const size_t sizes[]={COUNT_OF(capture0),COUNT_OF(capture1),COUNT_OF(capture2)};
 const uint64_t keys[]={0xDF2396,0xDF2394,0xDF2395};
 for(unsigned j=0;j<3;j++){
  SubGhzProtocolDecoderProteco d={.base={.callback=receive}};expected=keys[j];received=0;
  subghz_protocol_decoder_proteco_reset(&d);
  for(unsigned i=0;i<sizes[j];i++){int32_t v=captures[j][i];subghz_protocol_decoder_proteco_feed(&d,v>0,v>0?v:-v);}
  assert(received>0);
 }
 return 0;
}
''')

    def test_synthetic_keys_round_trip_and_width_capacity_noise_are_bounded(self):
        source=self.require_source()
        actual='\n'.join(production_function(source,n) for n in (
            'subghz_protocol_encoder_proteco_get_upload',
            'subghz_protocol_decoder_proteco_reset','subghz_protocol_decoder_proteco_feed'))
        native(context(source)+actual+r'''
static void receive(SubGhzProtocolDecoderBase* b,void* c){(void)c;
 SubGhzProtocolDecoderProteco* d=(void*)b;assert(d->generic.data==expected && d->generic.data_count_bit==24);received++;}
int main(void){
 unsigned rates[]={400,440,480};
 for(unsigned k=0;k<4096;k++)for(unsigned r=0;r<3;r++){
  LevelDuration upload[50];SubGhzProtocolEncoderProteco e={.te=rates[r],.encoder={.upload=upload,.size_upload=50}};
  e.generic.data=expected=0xDF2000|k;e.generic.data_count_bit=24;
  assert(subghz_protocol_encoder_proteco_get_upload(&e));assert(e.encoder.size_upload==50 && upload[0].duration==rates[r]*41/2);
  SubGhzProtocolDecoderProteco d={.base={.callback=receive}};received=0;subghz_protocol_decoder_proteco_reset(&d);
  for(unsigned frame=0;frame<3;frame++)for(unsigned i=0;i<50;i++)subghz_protocol_decoder_proteco_feed(&d,upload[i].level,upload[i].duration);
  assert(received==1);
 }
 LevelDuration upload[50];SubGhzProtocolEncoderProteco e={.te=440,.encoder={.upload=upload,.size_upload=49}};
 e.generic.data_count_bit=24;assert(!subghz_protocol_encoder_proteco_get_upload(&e));
 e.encoder.size_upload=50;e.generic.data_count_bit=25;assert(!subghz_protocol_encoder_proteco_get_upload(&e));
 e.generic.data_count_bit=24;e.generic.data=0x1000000;assert(!subghz_protocol_encoder_proteco_get_upload(&e));
 FUZZ_DECODER(SubGhzProtocolDecoderProteco,subghz_protocol_decoder_proteco_feed,4);
 for(unsigned bits=0;bits<350;bits++)if(bits!=24){
  SubGhzProtocolDecoderProteco d={.base={.callback=receive}};subghz_protocol_decoder_proteco_reset(&d);received=0;
  for(unsigned n=0;n<3;n++){
   subghz_protocol_decoder_proteco_feed(&d,false,9020);subghz_protocol_decoder_proteco_feed(&d,true,440);
   for(unsigned i=0;i<bits;i++){subghz_protocol_decoder_proteco_feed(&d,false,880);subghz_protocol_decoder_proteco_feed(&d,true,440);}
  }assert(!received);
 }
 return 0;
}
''')
        registry=(ROOT/'lib/subghz/protocols/plugin_registry.c').read_text()
        self.assertRegex(registry,r'PACK_GROUP\(SubGhzProtocolPackGroupCore\), "proteco"')
        self.assertNotIn('&subghz_protocol_proteco',(ROOT/'lib/subghz/protocols/protocol_items.c').read_text())

    def test_file_width_timing_and_repeat_do_not_leak_from_previous_file(self):
        source=self.require_source()
        actual='\n'.join(production_function(source,n) for n in (
            'subghz_protocol_proteco_read_key','subghz_protocol_encoder_proteco_get_upload',
            'subghz_protocol_encoder_proteco_deserialize'))
        native(context(source)+r'''
typedef enum {SubGhzProtocolStatusError=-1,SubGhzProtocolStatusOk,
 SubGhzProtocolStatusErrorEncoderGetUpload,SubGhzProtocolStatusErrorParserOthers} SubGhzProtocolStatus;
typedef struct {unsigned bits,rewinds,fail_at;uint64_t key;uint32_t te,repeat;bool optional;} FlipperFormat;
static SubGhzProtocolStatus subghz_block_generic_deserialize_check_count_bit(SubGhzBlockGeneric* g,FlipperFormat* f,unsigned bits){
 assert(bits==24);g->data=f->key;g->data_count_bit=f->bits;return SubGhzProtocolStatusOk;}
static bool flipper_format_rewind(FlipperFormat* f){return ++f->rewinds!=f->fail_at;}
static bool flipper_format_read_uint32(FlipperFormat* f,const char* name,uint32_t* out,unsigned n){
 assert(n==1);*out=!strcmp(name,"TE")?f->te:f->repeat;return f->optional;}
''' +actual+r'''
int main(void){(void)received;(void)expected;LevelDuration samples[50];
 SubGhzProtocolEncoderProteco e={.encoder={.upload=samples,.size_upload=50}};
 FlipperFormat f={.bits=24,.key=0xDF2396,.te=480,.repeat=7,.optional=true};
 assert(subghz_protocol_encoder_proteco_deserialize(&e,&f)==SubGhzProtocolStatusOk && e.te==480 && e.encoder.repeat==7);
 f=(FlipperFormat){.bits=24};assert(subghz_protocol_encoder_proteco_deserialize(&e,&f)==SubGhzProtocolStatusOk);
 assert(e.te==440 && e.encoder.repeat==3 && e.encoder.front==0);
 for(unsigned bits=0;bits<80;bits++){
  f=(FlipperFormat){.bits=bits};assert((subghz_protocol_encoder_proteco_deserialize(&e,&f)==SubGhzProtocolStatusOk)==(bits==24));}
 f=(FlipperFormat){.bits=24,.key=0x1000000};assert(subghz_protocol_encoder_proteco_deserialize(&e,&f)!=SubGhzProtocolStatusOk && !e.encoder.is_running);
 for(unsigned fail=1;fail<=2;fail++){
  f=(FlipperFormat){.bits=24,.fail_at=fail};assert(subghz_protocol_encoder_proteco_deserialize(&e,&f)!=SubGhzProtocolStatusOk && !e.encoder.is_running);}
 return 0;}
''')


if __name__=='__main__':unittest.main()
