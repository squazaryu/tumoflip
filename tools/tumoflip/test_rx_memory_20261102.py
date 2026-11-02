"""Actual lazy BinRAW and Wardriving admission predicates; no radio/SD writes."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT=Path(__file__).resolve().parents[2]


class RxMemoryNovemberTests(unittest.TestCase):
    def test_binraw_decoder_only_pays_for_capture_buffers_when_used(self):
        text=(ROOT/'lib/subghz/protocols/bin_raw.c').read_text()
        definition=re.search(r'struct SubGhzProtocolDecoderBinRAW \{.*?\};',text,re.S).group()
        helper=''
        signature='static bool subghz_protocol_decoder_bin_raw_buffers('
        if signature in text:helper=function(text,signature)
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#define UNUSED(x) ((void)x)
#define furi_assert assert
#define FURI_LOG_W(...) ((void)0)
#define BIN_RAW_BUF_RAW_SIZE 2048
#define BIN_RAW_MAX_MARKUP_COUNT 20
#define BIN_RAW_THRESHOLD_RSSI -85.0f
typedef int SubGhzEnvironment;
typedef int SubGhzProtocolDecoderBase;
typedef int SubGhzBlockDecoder;
typedef int SubGhzBlockGeneric;
typedef struct{uint32_t bit_count,byte_bias;}BinRAW_Markup;
typedef struct SubGhzProtocolDecoderBinRAW SubGhzProtocolDecoderBinRAW;
static const int subghz_protocol_bin_raw=0;
static size_t allocated,live;static bool deny;
static void*tracked(size_t n){void*p=calloc(1,n);assert(p);allocated+=n;live++;return p;}
static void* __attribute__((unused)) memmgr_heap_alloc_optional(size_t n){return deny?NULL:tracked(n);}
static void*subghz_protocol_decoder_common_alloc(size_t n,const void*p){(void)p;return tracked(n);}
static void tracked_free(void*p){if(p){assert(live);live--;}free(p);}
#define malloc tracked
#define free tracked_free
''' + definition + '\n' + helper + '\n' + function(text,'void* subghz_protocol_decoder_bin_raw_alloc(') + '\n' + function(text,'void subghz_protocol_decoder_bin_raw_free(') + r'''
int main(void){SubGhzProtocolDecoderBinRAW*x=subghz_protocol_decoder_bin_raw_alloc(NULL);
 assert(allocated==sizeof(*x)&&live==1&&!x->data&&!x->data_raw);
 subghz_protocol_decoder_bin_raw_free(x);assert(!live);return 0;}
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
