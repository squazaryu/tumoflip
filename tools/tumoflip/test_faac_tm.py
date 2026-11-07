"""FAAC TM static 12-DIP framing. Synthetic samples only; no radio/key search."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_upstream_20261104 import PRELUDE, declarations
from tools.tumoflip.test_upstream_20261105 import production_function

ROOT = Path(__file__).resolve().parents[2]
PATH = ROOT / 'lib/subghz/protocols/faac_tm.c'


def context(source):
    macros = '\n'.join(re.findall(r'(?m)^#define FAAC_TM_.*$', source))
    return (PRELUDE.replace('unsigned repeat;', 'unsigned repeat,front;bool is_running;')
            + 'typedef struct SubGhzProtocolDecoderFaacTm SubGhzProtocolDecoderFaacTm;\n'
            + 'typedef struct SubGhzProtocolEncoderFaacTm SubGhzProtocolEncoderFaacTm;\n'
            + declarations(source, 'faac_tm') + '\n' + macros + '\n')


class FaacTmTests(unittest.TestCase):
    def test_all_dips_round_trip_confirmed_and_overlong_frames_rejected(self):
        if not PATH.exists():
            # Intentional link-time RED: the new protocol interface is absent.
            native('void subghz_protocol_decoder_faac_tm_reset(void*);\n'
                   'int main(void){subghz_protocol_decoder_faac_tm_reset(0);return 0;}\n')
            return
        source = PATH.read_text()
        functions = '\n'.join(production_function(source, name) for name in (
            'subghz_protocol_encoder_faac_tm_get_upload',
            'subghz_protocol_decoder_faac_tm_reset', 'subghz_protocol_decoder_faac_tm_feed'))
        native(context(source) + functions + r'''
static void capture(SubGhzProtocolDecoderBase* b,void* c){
    (void)c;SubGhzProtocolDecoderFaacTm* d=(void*)b;
    assert(d->generic.data==expected && d->generic.data_count_bit==12);
    assert(d->te>=350 && d->te<=450);received++;
}
static void frame(SubGhzProtocolDecoderFaacTm* d,LevelDuration* upload,unsigned count){
    for(unsigned i=0;i<count;i++)subghz_protocol_decoder_faac_tm_feed(d,upload[i].level,upload[i].duration);
}
int main(void){
    const uint32_t rates[]={360,400,440};
    for(unsigned key=0;key<4096;key++)for(unsigned r=0;r<COUNT_OF(rates);r++){
        LevelDuration upload[26];SubGhzProtocolEncoderFaacTm e={
            .encoder={.upload=upload,.size_upload=26},.te=rates[r]};
        e.generic.data=expected=key;e.generic.data_count_bit=12;received=0;
        assert(subghz_protocol_encoder_faac_tm_get_upload(&e));
        assert(e.encoder.size_upload==26 && !upload[0].level && upload[0].duration==rates[r]*35);
        SubGhzProtocolDecoderFaacTm d={.base={.callback=capture}};
        subghz_protocol_decoder_faac_tm_reset(&d);
        frame(&d,upload,26);assert(received==0);
        frame(&d,upload,26);assert(received==0);
        frame(&d,upload,26);assert(received==1);
        subghz_protocol_decoder_faac_tm_reset(&d);
        frame(&d,upload,26);frame(&d,upload,26);assert(received==1);
    }
    LevelDuration upload[26];SubGhzProtocolEncoderFaacTm e={.encoder={.upload=upload,.size_upload=25},.te=400};
    e.generic.data_count_bit=12;assert(!subghz_protocol_encoder_faac_tm_get_upload(&e));
    e.encoder.size_upload=26;e.generic.data_count_bit=13;assert(!subghz_protocol_encoder_faac_tm_get_upload(&e));
    e.generic.data_count_bit=12;e.generic.data=0x1000;assert(!subghz_protocol_encoder_faac_tm_get_upload(&e));
    expected=0xfff;received=0;SubGhzProtocolDecoderFaacTm d={.base={.callback=capture}};
    for(unsigned bits=0;bits<350;bits++){
        subghz_protocol_decoder_faac_tm_reset(&d);
        for(unsigned repeat=0;repeat<3;repeat++){
            subghz_protocol_decoder_faac_tm_feed(&d,false,14000);
            subghz_protocol_decoder_faac_tm_feed(&d,true,400);
            for(unsigned i=0;i<bits;i++){
                subghz_protocol_decoder_faac_tm_feed(&d,false,400);
                subghz_protocol_decoder_faac_tm_feed(&d,true,800);
            }
        }
        if(bits!=12)assert(received==0);
        received=0;
    }
    FUZZ_DECODER(SubGhzProtocolDecoderFaacTm,subghz_protocol_decoder_faac_tm_feed,4);
    return 0;
}
''')
        registry = (ROOT / 'lib/subghz/protocols/plugin_registry.c').read_text()
        self.assertRegex(registry, r'PACK_GROUP\(SubGhzProtocolPackGroupCore\), "faac_tm"')
        self.assertNotIn('&subghz_protocol_faac_tm', (ROOT / 'lib/subghz/protocols/protocol_items.c').read_text())

    def test_optional_te_and_exact_key_width_do_not_inherit_previous_file_state(self):
        if not PATH.exists():
            native('int subghz_protocol_encoder_faac_tm_deserialize(void*,void*);\n'
                   'int main(void){return subghz_protocol_encoder_faac_tm_deserialize(0,0);}\n')
            return
        source = PATH.read_text()
        functions = '\n'.join(production_function(source, name) for name in (
            'subghz_protocol_faac_tm_read_key', 'subghz_protocol_encoder_faac_tm_get_upload',
            'subghz_protocol_encoder_faac_tm_deserialize'))
        native(context(source) + r'''
typedef enum {SubGhzProtocolStatusError=-1,SubGhzProtocolStatusOk,
    SubGhzProtocolStatusErrorEncoderGetUpload,SubGhzProtocolStatusErrorParserOthers} SubGhzProtocolStatus;
typedef struct {unsigned bits;uint64_t data;uint32_t te,repeat;unsigned rewinds,fail_at;
    bool has_te,has_repeat,fail_te,fail_repeat,fail_generic;} FlipperFormat;
SubGhzProtocolStatus subghz_block_generic_deserialize_check_count_bit(SubGhzBlockGeneric* g,FlipperFormat* f,unsigned n){
    assert(n==12);g->data=f->data;g->data_count_bit=f->bits;
    return f->fail_generic?SubGhzProtocolStatusError:SubGhzProtocolStatusOk;
}
bool flipper_format_rewind(FlipperFormat* f){f->rewinds++;return f->rewinds!=f->fail_at;}
bool flipper_format_read_uint32(FlipperFormat* f,const char* key,uint32_t* out,unsigned n){
    assert(n==1);if(strcmp(key,"TE")==0){*out=f->te;return f->has_te&&!f->fail_te;}
    assert(strcmp(key,"Repeat")==0);*out=f->repeat;return f->has_repeat&&!f->fail_repeat;
}
''' + functions + r'''
int main(void){
    LevelDuration upload[26];SubGhzProtocolEncoderFaacTm e={.encoder={.upload=upload,.size_upload=26}};
    const unsigned rates[]={0,100,101,360,400,440,999,1000,UINT32_MAX};
    for(unsigned i=0;i<COUNT_OF(rates);i++){
        FlipperFormat f={.bits=12,.data=0xfff,.te=rates[i],.has_te=true,.repeat=7,.has_repeat=true};
        assert(subghz_protocol_encoder_faac_tm_deserialize(&e,&f)==SubGhzProtocolStatusOk);
        assert(e.te==((rates[i]>100&&rates[i]<1000)?rates[i]:400));assert(e.encoder.repeat==7);
        f=(FlipperFormat){.bits=12,.te=500,.repeat=99};
        assert(subghz_protocol_encoder_faac_tm_deserialize(&e,&f)==SubGhzProtocolStatusOk);
        assert(e.te==400 && e.encoder.repeat==3);
    }
    for(unsigned bits=0;bits<80;bits++){
        FlipperFormat f={.bits=bits};int result=subghz_protocol_encoder_faac_tm_deserialize(&e,&f);
        assert((result==SubGhzProtocolStatusOk)==(bits==12));
    }
    FlipperFormat f={.bits=12,.data=0x1000};
    assert(subghz_protocol_encoder_faac_tm_deserialize(&e,&f)!=SubGhzProtocolStatusOk && !e.encoder.is_running);
    for(unsigned fail=1;fail<=2;fail++){
        f=(FlipperFormat){.bits=12,.fail_at=fail};
        assert(subghz_protocol_encoder_faac_tm_deserialize(&e,&f)!=SubGhzProtocolStatusOk && !e.encoder.is_running);
    }
    f=(FlipperFormat){.bits=12,.te=500,.has_te=true,.fail_te=true,.repeat=99,.has_repeat=true,.fail_repeat=true};
    assert(subghz_protocol_encoder_faac_tm_deserialize(&e,&f)==SubGhzProtocolStatusOk);
    assert(e.te==400 && e.encoder.repeat==3);return 0;
}
''')


if __name__ == '__main__':
    unittest.main()
