#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <lib/toolbox/manchester_decoder.h>
#include <lib/toolbox/level_duration.h>
#define furi_check assert
#define furi_assert assert
#define UNUSED(x) ((void)(x))
#define COUNT_OF(x) (sizeof(x)/sizeof(*(x)))
#define FURI_LOG_E(...) ((void)0)
#define FURI_LOG_D(...) ((void)0)
#define FURI_LOG_I(...) ((void)0)
#define FURI_LOG_W(...) ((void)0)
#define DURATION_DIFF(a,b) ((a)>(b)?(a)-(b):(b)-(a))
#define bit_read(x,n) (((x)>>(n))&1)
#define SUBGHZ_ASSERT_DECODER_COMMON_LAYOUT(x)
#define SUBGHZ_ASSERT_DECODER_GENERIC_LAYOUT(x)
enum{SUBGHZ_CUSTOM_BTN_OK,SUBGHZ_CUSTOM_BTN_UP,SUBGHZ_CUSTOM_BTN_DOWN,SUBGHZ_CUSTOM_BTN_LEFT,SUBGHZ_CUSTOM_BTN_RIGHT};
typedef enum{SubGhzProtocolStatusOk,SubGhzProtocolStatusError=-1,SubGhzProtocolStatusErrorParserKey=-8,SubGhzProtocolStatusErrorParserOthers=-10,SubGhzProtocolStatusErrorValueBitCount=-11}SubGhzProtocolStatus;
typedef enum{SubGhzProtocolTypeStatic,SubGhzProtocolTypeDynamic}SubGhzProtocolType;
enum{SubGhzProtocolFlag_315=1,SubGhzProtocolFlag_433=2,SubGhzProtocolFlag_868=4,SubGhzProtocolFlag_AM=8,SubGhzProtocolFlag_FM=16,SubGhzProtocolFlag_Decodable=32,SubGhzProtocolFlag_Load=64,SubGhzProtocolFlag_Save=128,SubGhzProtocolFlag_Send=256};
typedef struct{char text[4096];}FuriString;
static FuriString*furi_string_alloc(void){return calloc(1,sizeof(FuriString));}
static void furi_string_free(FuriString*s){free(s);}
static const char*furi_string_get_cstr(FuriString*s){return s->text;}
static void furi_string_printf(FuriString*s,const char*f,...){va_list a;va_start(a,f);vsnprintf(s->text,sizeof(s->text),f,a);va_end(a);}
static void furi_string_cat_printf(FuriString*s,const char*f,...){size_t n=strlen(s->text);va_list a;va_start(a,f);vsnprintf(s->text+n,sizeof(s->text)-n,f,a);va_end(a);}
typedef struct SubGhzProtocol SubGhzProtocol;
typedef struct SubGhzProtocolDecoderBase{const SubGhzProtocol*protocol;void(*callback)(struct SubGhzProtocolDecoderBase*,void*);void*context;}SubGhzProtocolDecoderBase;
typedef struct{uint64_t decode_data;uint32_t te_last,decode_count_bit,parser_step;}SubGhzBlockDecoder;
typedef struct{const char*protocol_name;uint64_t data,data_2;uint32_t serial;uint16_t data_count_bit;uint8_t btn;uint32_t cnt;uint8_t cnt_2;uint32_t seed;}SubGhzBlockGeneric;
typedef struct{SubGhzProtocolDecoderBase base;SubGhzBlockDecoder decoder;SubGhzBlockGeneric generic;}SubGhzProtocolDecoderCommon;
typedef struct{uint32_t te_short,te_long,te_delta,min_count_bit_for_found;}SubGhzBlockConst;
typedef struct{bool btn_is_available,cnt_is_available;uint8_t btn_length_bit,cnt_length_bit,current_btn;uint32_t current_cnt;}SubGhzBlockGenericGlobal;
static SubGhzBlockGenericGlobal subghz_block_generic_global;
static unsigned subghz_custom_btn_get_original(void){return 1;}
static uint32_t furi_get_tick(void){static uint32_t tick;return ++tick;}
static void subghz_custom_btn_set_original(unsigned b){(void)b;}
static void subghz_custom_btn_set_max(unsigned b){(void)b;}
typedef struct{int unused;}SubGhzEnvironment;
typedef struct{int unused;}SubGhzRadioPreset;
typedef struct{char name[32];uint8_t bytes[512];uint32_t value;size_t count;}RxField;
typedef struct{uint64_t data;uint16_t bits;RxField fields[32];unsigned count;const char*fail_key;}FlipperFormat;
typedef struct{void*(*alloc)(SubGhzEnvironment*);void(*free)(void*);void(*feed)(void*,bool,uint32_t);void(*reset)(void*);uint8_t(*get_hash_data)(void*);SubGhzProtocolStatus(*serialize)(void*,FlipperFormat*,SubGhzRadioPreset*);SubGhzProtocolStatus(*deserialize)(void*,FlipperFormat*);void(*get_string)(void*,FuriString*);}SubGhzProtocolDecoder;
struct SubGhzProtocol{const char*name;SubGhzProtocolType type;unsigned flag;const SubGhzProtocolDecoder*decoder;const void*encoder;};
static RxField*rx_field(FlipperFormat*f,const char*k,bool create){for(unsigned i=0;i<f->count;i++)if(!strcmp(k,f->fields[i].name))return &f->fields[i];if(!create)return NULL;assert(f->count<32);RxField*r=&f->fields[f->count++];memset(r,0,sizeof(*r));snprintf(r->name,sizeof(r->name),"%s",k);return r;}
static bool flipper_format_rewind(FlipperFormat*f){return f!=NULL;}
static bool flipper_format_get_value_count(FlipperFormat*f,const char*k,uint32_t*n){RxField*r=rx_field(f,k,false);if(!r)return false;*n=r->count;return true;}
static bool flipper_format_insert_or_update_hex(FlipperFormat*f,const char*k,const uint8_t*v,size_t n){if(f->fail_key&&!strcmp(k,f->fail_key))return false;assert(n<=512);RxField*r=rx_field(f,k,true);r->count=n;memcpy(r->bytes,v,n);return true;}
static bool flipper_format_read_hex(FlipperFormat*f,const char*k,uint8_t*v,size_t n){RxField*r=rx_field(f,k,false);if(!r||r->count!=n)return false;memcpy(v,r->bytes,n);return true;}
static bool flipper_format_insert_or_update_uint32(FlipperFormat*f,const char*k,const uint32_t*v,size_t n){assert(n==1);if(f->fail_key&&!strcmp(k,f->fail_key))return false;RxField*r=rx_field(f,k,true);r->value=*v;r->count=1;return true;}
static bool flipper_format_write_uint32(FlipperFormat*f,const char*k,const uint32_t*v,size_t n){return flipper_format_insert_or_update_uint32(f,k,v,n);}
static bool flipper_format_read_uint32(FlipperFormat*f,const char*k,uint32_t*v,size_t n){assert(n==1);RxField*r=rx_field(f,k,false);if(!r)return false;*v=r->value;return true;}
static bool flipper_format_write_string_cstr(FlipperFormat*f,const char*k,const char*v){return flipper_format_insert_or_update_hex(f,k,(const uint8_t*)v,strlen(v));}
static bool flipper_format_insert_or_update_string_cstr(FlipperFormat*f,const char*k,const char*v){return flipper_format_write_string_cstr(f,k,v);}
static SubGhzProtocolStatus subghz_block_generic_serialize(SubGhzBlockGeneric*g,FlipperFormat*f,SubGhzRadioPreset*p){(void)p;f->data=g->data;f->bits=g->data_count_bit;return 0;}
static SubGhzProtocolStatus subghz_block_generic_deserialize(SubGhzBlockGeneric*g,FlipperFormat*f){g->data=f->data;g->data_2=0;g->data_count_bit=f->bits;return 0;}
static SubGhzProtocolStatus subghz_block_generic_deserialize_check_count_bit(SubGhzBlockGeneric*g,FlipperFormat*f,uint32_t n){subghz_block_generic_deserialize(g,f);return g->data_count_bit==n?0:SubGhzProtocolStatusErrorValueBitCount;}
static void subghz_protocol_blocks_add_bit(SubGhzBlockDecoder*d,uint8_t bit){d->decode_data=(d->decode_data<<1)|bit;d->decode_count_bit++;}
static void subghz_protocol_blocks_add_to_128_bit(SubGhzBlockDecoder*d,uint8_t bit,uint64_t*hi){if(++d->decode_count_bit>64)*hi=(*hi<<1)|(d->decode_data>>63);d->decode_data=(d->decode_data<<1)|bit;}
static uint8_t subghz_protocol_blocks_get_hash_data(SubGhzBlockDecoder*d,size_t n){uint8_t h=0;const uint8_t*p=(const uint8_t*)&d->decode_data;assert(n<=sizeof(*d));for(size_t i=0;i<n;i++)h=(uint8_t)((h^p[i])*31);return h;}
static void subghz_protocol_blocks_set_bit_array(bool bit,uint8_t*data,size_t n,size_t size){assert(n/8<size);if(bit)data[n/8]|=(uint8_t)(1u<<(7-n%8));else data[n/8]&=(uint8_t)~(1u<<(7-n%8));}
