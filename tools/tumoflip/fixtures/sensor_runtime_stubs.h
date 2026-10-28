#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define UNUSED(x) (void)(x)
#define RECORD_SUBGHZ_RADIO_BROKER "radio"
#define SUBGHZ_DEVICE_CC1101_INT_NAME "internal"
#define FURI_CRITICAL_ENTER() do{}while(0)
#define FURI_CRITICAL_EXIT() do{}while(0)
typedef unsigned SubGhzRadioBroker, SubGhzRadioBrokerLease;
typedef struct {char text[8];} FuriString;
typedef struct {unsigned unused;} SubGhzDevice, SubGhzEnvironment, FlipperFormat;
typedef struct {const char*name;} MockProtocol;
typedef struct {const MockProtocol*protocol;} SubGhzProtocolDecoderBase;
typedef struct {uint8_t data[8192];size_t size;} FuriStreamBuffer;
typedef struct {FuriString*name;uint32_t frequency;} SubGhzRadioPreset;
typedef uint32_t LevelDuration;
typedef struct SubGhzReceiver SubGhzReceiver;
struct SubGhzReceiver {bool reset;void(*callback)(SubGhzReceiver*,SubGhzProtocolDecoderBase*,void*);void*context;};
enum {SubGhzProtocolStatusOk, SubGhzProtocolFlag_Decodable=1, SubGhzProtocolFlag_AM,
      FuriHalSubGhzPresetOok650Async,SubGhzRadioBrokerDeviceInternal,SubGhzRadioBrokerStateAsyncRx};
static const int weather_station_protocol_registry=25;
static unsigned open_records,leases,device_inits,active,reset_count;
static uint32_t heap=50000;
static bool deny,missing,receiver_failure,frequency_failure,format_failure,decode_on_feed;
static SubGhzDevice radio;
static uint32_t ff_id=0x42,ff_channel=2,ff_humidity=56,ff_battery=1;
static float ff_temperature=-19.8f;
static bool ff_bad_id,ff_bad_channel,ff_bad_temperature;
static void* furi_record_open(const char*key){assert(!strcmp(key,"radio"));open_records++;return &radio;}
static void furi_record_close(const char*key){assert(!strcmp(key,"radio")&&open_records);open_records--;}
static bool subghz_radio_broker_acquire(void*b,const char*name,unsigned timeout,unsigned*lease){(void)b;assert(name&&timeout==500);if(deny)return false;assert(!leases);leases++;*lease=1;return true;}
static void subghz_radio_broker_release(void*b,unsigned*lease){(void)b;assert(leases&&*lease&&!active);leases--;}
static void subghz_radio_broker_set_selected_device(void*b,unsigned*l,int d){(void)b;(void)l;(void)d;}
static void subghz_radio_broker_set_state(void*b,unsigned*l,int d){(void)b;(void)l;(void)d;}
static uint32_t memmgr_get_free_heap(void){return heap;}
static FuriStreamBuffer*furi_stream_buffer_alloc(size_t bytes,size_t trigger){assert(bytes==4096&&trigger==4);return calloc(1,sizeof(FuriStreamBuffer));}
static void furi_stream_buffer_free(FuriStreamBuffer*b){assert(!active);free(b);}
static size_t furi_stream_buffer_send(FuriStreamBuffer*b,const void*data,size_t n,unsigned timeout){assert(!timeout);if(n>sizeof(b->data)-b->size)return 0;memcpy(b->data+b->size,data,n);b->size+=n;return n;}
static size_t furi_stream_buffer_receive(FuriStreamBuffer*b,void*data,size_t n,unsigned timeout){assert(!timeout);if(n>b->size)n=b->size;memcpy(data,b->data,n);b->size-=n;memmove(b->data,b->data+n,b->size);return n;}
static void furi_stream_buffer_reset(FuriStreamBuffer*b){b->size=0;}
static FlipperFormat*flipper_format_string_alloc(void){return format_failure?NULL:calloc(1,sizeof(FlipperFormat));}
static void flipper_format_free(FlipperFormat*f){free(f);}
static FuriString*furi_string_alloc_set(const char*text){FuriString*s=calloc(1,sizeof(*s));strcpy(s->text,text);return s;}
static void furi_string_free(FuriString*s){free(s);}
static bool flipper_format_rewind(FlipperFormat*f){assert(f);return true;}
static bool flipper_format_read_uint32(FlipperFormat*f,const char*key,uint32_t*out,unsigned n){assert(f&&n==1);
 if(!strcmp(key,"Id")){*out=ff_id;return !ff_bad_id;}if(!strcmp(key,"Ch")){*out=ff_channel;return !ff_bad_channel;}
 if(!strcmp(key,"Hum")){*out=ff_humidity;return true;}if(!strcmp(key,"Batt")){*out=ff_battery;return true;}return false;}
static bool flipper_format_read_float(FlipperFormat*f,const char*key,float*out,unsigned n){assert(f&&n==1&&!strcmp(key,"Temp"));*out=ff_temperature;return !ff_bad_temperature;}
static int subghz_protocol_decoder_base_serialize(SubGhzProtocolDecoderBase*d,FlipperFormat*f,SubGhzRadioPreset*p){assert(d&&f&&p&&!strcmp(p->name->text,"AM650"));return 0;}
static SubGhzEnvironment*subghz_environment_alloc(void){return calloc(1,sizeof(SubGhzEnvironment));}
static void subghz_environment_free(SubGhzEnvironment*e){free(e);}
static void subghz_environment_set_protocol_registry(SubGhzEnvironment*e,const void*r){assert(e&&r==&weather_station_protocol_registry);}
static SubGhzReceiver*subghz_receiver_alloc_init(SubGhzEnvironment*e){assert(e);return receiver_failure?NULL:calloc(1,sizeof(SubGhzReceiver));}
static void subghz_receiver_free(SubGhzReceiver*r){assert(!active);free(r);}
static void subghz_receiver_set_filter(SubGhzReceiver*r,int flag){assert(r&&flag==SubGhzProtocolFlag_Decodable);}
static void subghz_receiver_set_modulation_filter(SubGhzReceiver*r,int flag){assert(r&&flag==SubGhzProtocolFlag_AM);}
static void subghz_receiver_set_rx_callback(SubGhzReceiver*r,void(*cb)(SubGhzReceiver*,SubGhzProtocolDecoderBase*,void*),void*ctx){assert(r);r->callback=cb;r->context=ctx;}
static void subghz_receiver_reset(SubGhzReceiver*r){assert(r);r->reset=true;reset_count++;}
static void subghz_receiver_decode(SubGhzReceiver*r,bool level,unsigned duration){(void)level;assert(r&&r->reset&&duration);
 if(decode_on_feed){MockProtocol protocol={"Nexus-TH"};SubGhzProtocolDecoderBase decoder={&protocol};r->callback(r,&decoder,r->context);}}
static void subghz_devices_init(void){device_inits++;}
static void subghz_devices_deinit(void){assert(!active&&device_inits);device_inits--;}
static const SubGhzDevice*subghz_devices_get_by_name(const char*name){assert(!strcmp(name,"internal"));return missing?NULL:&radio;}
static bool subghz_devices_is_frequency_valid(const SubGhzDevice*d,unsigned f){assert(d);return f>=300000000;}
static void subghz_devices_reset(const SubGhzDevice*d){assert(d);}
static void subghz_devices_load_preset(const SubGhzDevice*d,int preset,void*data){assert(d&&preset==FuriHalSubGhzPresetOok650Async&&!data);}
static uint32_t subghz_devices_set_frequency(const SubGhzDevice*d,unsigned f){assert(d);return frequency_failure?0:f;}
static void subghz_devices_flush_rx(const SubGhzDevice*d){assert(d);}
static void subghz_devices_set_rx(const SubGhzDevice*d){assert(d);}
static void subghz_devices_start_async_rx(const SubGhzDevice*d,void(*cb)(bool,uint32_t,void*),void*ctx){assert(d&&cb&&ctx&&!active);active++;}
static void subghz_devices_stop_async_rx(const SubGhzDevice*d){assert(d&&active);active--;}
static void subghz_devices_sleep(const SubGhzDevice*d){assert(d&&!active);}
static void subghz_devices_end(const SubGhzDevice*d){assert(d&&!active);}
static uint32_t furi_hal_rtc_get_timestamp(void){return 100;}
static LevelDuration level_duration_make(bool level,uint32_t duration){return duration|((uint32_t)level<<31);}
static uint32_t level_duration_get_duration(LevelDuration d){return d&0x7FFFFFFF;}
static bool level_duration_get_level(LevelDuration d){return d>>31;}
