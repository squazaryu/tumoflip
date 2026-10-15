#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define furi_assert(x) assert(x)
#define FURI_LOG_E(...) ((void)0)
typedef struct {void* protocol;} SubGhzProtocolDecoderBase;
typedef SubGhzProtocolDecoderBase SubGhzProtocolEncoderBase;
typedef struct {int unused;} SubGhzBlockDecoder;
typedef struct {uint32_t repeat;bool is_running;} SubGhzProtocolBlockEncoder;
typedef struct {uint64_t data,data_2;uint16_t data_count_bit;uint32_t serial,cnt;uint8_t btn;} SubGhzBlockGeneric;
typedef int SubGhzRadioPreset;
typedef enum {SubGhzProtocolStatusOk,SubGhzProtocolStatusError,SubGhzProtocolStatusErrorParserOthers,SubGhzProtocolStatusErrorValueBitCount} SubGhzProtocolStatus;
typedef struct {uint64_t data,tail;uint16_t bits,ic;bool has_ic,has_known,known,bad_known,has_tail,fail_write;} FlipperFormat;
typedef struct SubGhzProtocolDecoderNiceFlorS SubGhzProtocolDecoderNiceFlorS;
typedef struct SubGhzProtocolEncoderNiceFlorS SubGhzProtocolEncoderNiceFlorS;
/* @STRUCTS@ */
#define NICE_ONE_COUNT_BIT 72
#define NICE_O_LONG_COUNT_BIT 112
#define NICE_FLOR_S_IC_PLAIN 0xffffu
static uint8_t nice_flors_counter_mode;
static const struct {unsigned min_count_bit_for_found;} subghz_protocol_nice_flor_s_const={52};
static unsigned decodes,uploads;
bool flipper_format_rewind(FlipperFormat* f){(void)f;return true;}
bool flipper_format_key_exist(FlipperFormat* f,const char* key){return !strcmp(key,"ICKnown") && f->has_known;}
bool flipper_format_read_bool(FlipperFormat* f,const char* key,bool* b,size_t n){assert(n==1 && !strcmp(key,"ICKnown"));if(!f->has_known || f->bad_known)return false;*b=f->known;return true;}
bool flipper_format_insert_or_update_bool(FlipperFormat* f,const char* key,const bool* b,size_t n){assert(n==1 && !strcmp(key,"ICKnown"));if(f->fail_write)return false;f->has_known=true;f->known=*b;return true;}
bool flipper_format_read_uint32(FlipperFormat* f,const char* key,uint32_t* value,size_t n){assert(n==1);if(!strcmp(key,"Data")){*value=(uint32_t)f->tail;return true;}return false;}
bool flipper_format_insert_or_update_uint32(FlipperFormat* f,const char* key,const uint32_t* value,size_t n){assert(n==1 && !strcmp(key,"Data"));f->tail=*value;return !f->fail_write;}
bool flipper_format_update_uint32(FlipperFormat* f,const char* key,const uint32_t* value,size_t n){return flipper_format_insert_or_update_uint32(f,key,value,n);}
bool flipper_format_read_hex(FlipperFormat* f,const char* key,uint8_t* value,size_t n){
    if(!strcmp(key,"IC")){assert(n==2);if(!f->has_ic)return false;value[0]=f->ic>>8;value[1]=f->ic;return true;}
    assert(!strcmp(key,"Data2") && n==8);if(!f->has_tail)return false;
    for(size_t i=0;i<8;i++)value[7-i]=(uint8_t)(f->tail>>(i*8));return true;
}
bool flipper_format_insert_or_update_hex(FlipperFormat* f,const char* key,const uint8_t* value,size_t n){
    if(f->fail_write)return false;
    if(!strcmp(key,"IC")){assert(n==2);f->ic=(uint16_t)value[0]<<8|value[1];f->has_ic=true;return true;}
    assert(!strcmp(key,"Data2") && n==8);f->tail=0;for(size_t i=0;i<8;i++)f->tail=f->tail<<8|value[i];f->has_tail=true;return true;
}
bool flipper_format_update_hex(FlipperFormat* f,const char* key,const uint8_t* value,size_t n){(void)f;(void)value;assert(!strcmp(key,"Key") && n==8);return true;}
SubGhzProtocolStatus subghz_block_generic_deserialize(SubGhzBlockGeneric* g,FlipperFormat* f){g->data=f->data;g->data_count_bit=f->bits;return SubGhzProtocolStatusOk;}
SubGhzProtocolStatus subghz_block_generic_serialize(SubGhzBlockGeneric* g,FlipperFormat* f,SubGhzRadioPreset* p){(void)p;f->data=g->data;f->bits=g->data_count_bit;return SubGhzProtocolStatusOk;}
void subghz_protocol_nice_flor_s_remote_controller(SubGhzBlockGeneric* g,const char* n,uint16_t ic,bool o){(void)n;(void)ic;(void)o;decodes++;g->btn=1;}
void subghz_protocol_encoder_nice_flor_s_get_upload(SubGhzProtocolEncoderNiceFlorS* e,uint8_t b,const char* n){(void)e;(void)b;(void)n;uploads++;}
/* @PRODUCTION@ */
int main(void){
    SubGhzProtocolDecoderNiceFlorS d={0};
    FlipperFormat legacy={.bits=52,.data=0x123456789abcdULL,.has_ic=true,.ic=0xffff};
    assert(subghz_protocol_decoder_nice_flor_s_deserialize(&d,&legacy)==SubGhzProtocolStatusOk);
    assert(d.o_code && !d.o_code_no_ic);
    FlipperFormat raw=legacy;raw.bits=112;raw.tail=0x123456789abcdefULL;raw.has_tail=true;raw.has_known=true;raw.known=false;
    assert(subghz_protocol_decoder_nice_flor_s_deserialize(&d,&raw)==SubGhzProtocolStatusOk);
    assert(d.o_code_no_ic && d.generic.data_2==raw.tail);
    FlipperFormat saved={0};
    assert(subghz_protocol_decoder_nice_flor_s_serialize(&d,&saved,NULL)==SubGhzProtocolStatusOk);
    assert(saved.bits==112 && saved.data==raw.data && saved.tail==raw.tail && saved.has_known && !saved.known);
    saved.fail_write=true;
    assert(subghz_protocol_decoder_nice_flor_s_serialize(&d,&saved,NULL)!=SubGhzProtocolStatusOk);
    raw.tail=UINT64_MAX;
    assert(subghz_protocol_decoder_nice_flor_s_deserialize(&d,&raw)!=SubGhzProtocolStatusOk);
    raw=legacy;raw.has_known=true;raw.known=false;
    assert(subghz_protocol_decoder_nice_flor_s_deserialize(&d,&raw)==SubGhzProtocolStatusOk && d.o_code_no_ic);
    SubGhzProtocolEncoderNiceFlorS e={.encoder={.is_running=true}};
    assert(subghz_protocol_encoder_nice_flor_s_deserialize(&e,&raw)!=SubGhzProtocolStatusOk);
    assert(!e.encoder.is_running && !uploads && !decodes);
    raw.known=true;raw.bad_known=true;
    assert(subghz_protocol_encoder_nice_flor_s_deserialize(&e,&raw)!=SubGhzProtocolStatusOk && !uploads);
    raw=legacy;raw.bits=112;
    assert(subghz_protocol_encoder_nice_flor_s_deserialize(&e,&raw)!=SubGhzProtocolStatusOk && !uploads);
    assert(subghz_protocol_encoder_nice_flor_s_deserialize(&e,&legacy)==SubGhzProtocolStatusOk);
    assert(uploads==1 && decodes==1);
    return 0;
}
