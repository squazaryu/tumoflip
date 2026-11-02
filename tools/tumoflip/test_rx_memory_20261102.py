"""Actual lazy BinRAW and Wardriving admission predicates; no radio/SD writes."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class RxMemoryNovemberTests(unittest.TestCase):
    def test_disabled_binraw_is_not_armed_by_rssi_in_any_receiver(self):
        for relative, diversity in [
            ('applications/main/subghz/scenes/subghz_scene_receiver.c', True),
            ('applications_user/arf_subghz_full/scenes/subghz_scene_receiver.c', False),
            ('applications_user/subghz_wardriving/scenes/subghz_wardriving_scene_receiver.c', False),
        ]:
            text = (ROOT / relative).read_text()
            start = text.index('if(READ_BIT(subghz->filter, SubGhzProtocolFlag_BinRAW))')
            end = text.index('{', start) + 1
            depth = 1
            while depth:
                depth += (text[end] == '{') - (text[end] == '}')
                end += 1
            gate = text[start:end]
            # All RSSI entry points, including diversity, must be inside the filter gate.
            self.assertEqual(text.count('subghz_protocol_decoder_bin_raw_data_input_rssi('),
                             gate.count('subghz_protocol_decoder_bin_raw_data_input_rssi('))
            native(r'''
#include <assert.h>
#include <stdbool.h>
#define READ_BIT(v,b) ((v)&(b))
#define SubGhzProtocolFlag_BinRAW 4U
typedef int SubGhzProtocolDecoderBase;
typedef int SubGhzProtocolDecoderBinRAW;
typedef struct {unsigned filter;void* txrx;} SubGhz;
static bool secondary;
static unsigned calls;
static int primary,other;
static void* subghz_txrx_get_decoder(void* x) {(void)x;return &primary;}
static __attribute__((unused)) void* subghz_wardriving_txrx_get_decoder(void* x) {
    return subghz_txrx_get_decoder(x);
}
static __attribute__((unused)) void* subghz_txrx_get_diversity_decoder(void* x) {
    (void)x;return secondary?&other:0;
}
static void subghz_protocol_decoder_bin_raw_data_input_rssi(void* x,float rssi) {
    assert(x&&rssi==-30);calls++;
}
static void tick(SubGhz* subghz) {struct {float rssi;} ret_rssi={-30};
''' + gate + r'''
}
int main(void) {
    SubGhz x={0};secondary=true;tick(&x);assert(!calls);
    x.filter=1;tick(&x);assert(!calls);
    x.filter=SubGhzProtocolFlag_BinRAW;secondary=false;tick(&x);assert(calls==1);
    secondary=true;tick(&x);
''' + f'assert(calls=={3 if diversity else 2});return 0;}}')

    def test_binraw_decoder_only_pays_for_capture_buffers_when_used(self):
        text = (ROOT / 'lib/subghz/protocols/bin_raw.c').read_text()
        definition = re.search(r'struct SubGhzProtocolDecoderBinRAW \{.*?\};', text, re.S).group()
        actual = '\n'.join(function(text, signature) for signature in [
            'static uint16_t subghz_protocol_bin_raw_get_full_byte(',
            'static bool subghz_protocol_decoder_bin_raw_buffers(',
            'void* subghz_protocol_decoder_bin_raw_alloc(',
            'void subghz_protocol_decoder_bin_raw_free(',
            'void subghz_protocol_decoder_bin_raw_reset(',
            'void subghz_protocol_decoder_bin_raw_feed(',
            'void subghz_protocol_decoder_bin_raw_data_input_rssi(',
            'uint8_t subghz_protocol_decoder_bin_raw_get_hash_data(',
        ])
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define UNUSED(x) ((void)x)
#define furi_assert assert
#define furi_check assert
#define FURI_LOG_W(...) ((void)0)
#define bin_raw_debug(...) ((void)0)
#define bin_raw_debug_tag(...) ((void)0)
#define BIN_RAW_BUF_RAW_SIZE 2048
#define BIN_RAW_MAX_MARKUP_COUNT 20
#define BIN_RAW_THRESHOLD_RSSI -85.0f
#define BIN_RAW_DELTA_RSSI 7.0f
#define BIN_RAW_BUF_MIN_DATA_COUNT 128
enum {BinRAWDecoderStepReset, BinRAWDecoderStepWrite, BinRAWDecoderStepBufFull,
      BinRAWDecoderStepNoParse};
typedef int SubGhzEnvironment;
typedef struct {void (*callback)(void*,void*); void* context;} SubGhzProtocolDecoderBase;
typedef struct {unsigned parser_step;} SubGhzBlockDecoder;
typedef struct {unsigned data_count_bit;} SubGhzBlockGeneric;
typedef struct {uint16_t byte_bias,bit_count;} BinRAW_Markup;
typedef struct SubGhzProtocolDecoderBinRAW SubGhzProtocolDecoderBinRAW;
static const int subghz_protocol_bin_raw=0;
static size_t allocated,live,attempts,analyses,hashes;
static bool deny;
static void* tracked(size_t n) {
    void* p=calloc(1,n); assert(p); allocated+=n; live++; return p;
}
static void* memmgr_heap_alloc_optional(size_t n) {
    attempts++; return deny?NULL:tracked(n);
}
static void* subghz_protocol_decoder_common_alloc(size_t n,const void* p) {
    (void)p; return tracked(n);
}
static void tracked_free(void* p) {if(p){assert(live);live--;} free(p);}
static bool subghz_protocol_bin_raw_check_remote_controller(SubGhzProtocolDecoderBinRAW* x) {
    (void)x; analyses++; return false;
}
static uint8_t subghz_protocol_blocks_add_bytes(const uint8_t* p,size_t n) {
    hashes++; assert(n==1); return p[0];
}
#define malloc tracked
#define free tracked_free
''' + definition + '\n' + actual + r'''
int main(void) {
    for(unsigned bits=0;bits<=UINT16_MAX;bits++) {
        assert(subghz_protocol_bin_raw_get_full_byte(bits)==bits/8+(bits%8!=0));
    }
    SubGhzProtocolDecoderBinRAW* x=subghz_protocol_decoder_bin_raw_alloc(NULL);
    assert(allocated==sizeof(*x)&&live==1&&!x->data&&!x->data_raw);
    assert(!subghz_protocol_decoder_bin_raw_get_hash_data(x)&&!hashes);
    x->decoder.parser_step=BinRAWDecoderStepWrite;
    subghz_protocol_decoder_bin_raw_feed(x,true,100);
    assert(!x->data_raw_ind); // A reset or missed RSSI must not dereference NULL.
    subghz_protocol_decoder_bin_raw_reset(x);
    subghz_protocol_decoder_bin_raw_data_input_rssi(x,-90);
    assert(!attempts&&x->decoder.parser_step==BinRAWDecoderStepReset);
    deny=true;
    subghz_protocol_decoder_bin_raw_data_input_rssi(x,-10);
    assert(attempts==1&&live==1&&!x->data&&!x->data_raw);
    assert(x->decoder.parser_step==BinRAWDecoderStepReset);
    deny=false;
    subghz_protocol_decoder_bin_raw_data_input_rssi(x,-10);
    assert(attempts==2&&live==2);
    assert(allocated==sizeof(*x)+10240);
    assert(x->data==(uint8_t*)(x->data_raw+BIN_RAW_BUF_RAW_SIZE));
    assert(x->decoder.parser_step==BinRAWDecoderStepWrite);
    x->data[0]=42; x->data[BIN_RAW_BUF_RAW_SIZE-1]=23;
    x->data_markup[0].bit_count=8;
    assert(subghz_protocol_decoder_bin_raw_get_hash_data(x)==42&&hashes==1);
    for(size_t i=0;i<BIN_RAW_BUF_RAW_SIZE;i++) subghz_protocol_decoder_bin_raw_feed(x,true,100);
    assert(x->data_raw_ind==BIN_RAW_BUF_RAW_SIZE&&x->data_raw[BIN_RAW_BUF_RAW_SIZE-1]==100);
    assert(x->data[0]==42&&x->data[BIN_RAW_BUF_RAW_SIZE-1]==23);
    subghz_protocol_decoder_bin_raw_feed(x,false,200);
    assert(x->decoder.parser_step==BinRAWDecoderStepBufFull);
    subghz_protocol_decoder_bin_raw_data_input_rssi(x,-100);
    assert(analyses==1&&x->decoder.parser_step==BinRAWDecoderStepReset);
    subghz_protocol_decoder_bin_raw_data_input_rssi(x,-10);
    assert(attempts==2&&!x->data_raw_ind&&!x->data[0]); // Reuse, not another allocation.
    assert(!x->data[BIN_RAW_BUF_RAW_SIZE-1]&&!x->data_raw[BIN_RAW_BUF_RAW_SIZE-1]);
    subghz_protocol_decoder_bin_raw_reset(x);
    assert(live==2); // Payload retained until decoder destruction.
    subghz_protocol_decoder_bin_raw_free(x); assert(!live);
    x=subghz_protocol_decoder_bin_raw_alloc(NULL);
    subghz_protocol_decoder_bin_raw_free(x); assert(!live); // Never-enabled destruction.
    return 0;
}
''')

    def test_wardriving_checks_fragmentation_and_raw_peak_not_only_total_heap(self):
        text=(ROOT/'applications_user/subghz_wardriving/subghz_wardriving_history.c').read_text()
        helper=''
        signature='static bool subghz_wardriving_history_has_room('
        if signature in text:helper=function(text,signature)
        constants='\n'.join(re.findall(r'^#define SUBGHZ_HISTORY_.*$',text,re.M))
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
enum{SubGhzProtocolTypeStatic,SubGhzProtocolTypeBinRAW};
static size_t heap,largest;
static size_t memmgr_get_free_heap(void){return heap;}
static size_t __attribute__((unused)) memmgr_heap_get_max_free_block(void){return largest;}
static bool subghz_wardriving_history_has_room(uint8_t);
''' + constants + '\n' + helper + r'''
int main(void){heap=50000;largest=20000;assert(subghz_wardriving_history_has_room(SubGhzProtocolTypeStatic));assert(subghz_wardriving_history_has_room(SubGhzProtocolTypeBinRAW));
 heap=4000;assert(!subghz_wardriving_history_has_room(SubGhzProtocolTypeStatic));heap=10000;assert(!subghz_wardriving_history_has_room(SubGhzProtocolTypeBinRAW));
 heap=50000;largest=1000;assert(!subghz_wardriving_history_has_room(SubGhzProtocolTypeStatic));largest=5000;assert(!subghz_wardriving_history_has_room(SubGhzProtocolTypeBinRAW));return 0;}
''')


if __name__=='__main__':unittest.main()
