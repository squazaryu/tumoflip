"""Run production DESFire identity and poller mode/status code on the host."""

from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function, run_c

ROOT = Path(__file__).resolve().parents[2]


def production(relative, signature):
    text = (ROOT / relative).read_text(encoding="utf-8")
    return function(text, signature) if signature in text else ""


class DesfireLightNativeTests(unittest.TestCase):
    def test_fresh_poller_is_not_allowed_to_inherit_allocator_garbage(self):
        source = production(
            "lib/nfc/protocols/mf_desfire/mf_desfire_poller.c",
            "static MfDesfirePoller* mf_desfire_poller_alloc(",
        )
        run_c(r"""
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef enum {NfcProtocolMfDesfire} NfcProtocol;
typedef struct Iso14443_4aPoller Iso14443_4aPoller;
typedef enum {NxpNativeCommandModePlain,NxpNativeCommandModeIsoWrapped,
              NxpNativeCommandModeMAX} NxpNativeCommandMode;
typedef enum {MfDesfirePollerStateIdle,MfDesfirePollerStateReadVersion}
    MfDesfirePollerState;
typedef enum {MfDesfirePollerSessionStateIdle,MfDesfirePollerSessionStateActive}
    MfDesfirePollerSessionState;
typedef enum {MfDesfireErrorNone,MfDesfireErrorProtocol} MfDesfireError;
typedef struct {int marker;} MfDesfireData;
typedef struct {unsigned bytes;} BitBuffer;
typedef struct {int marker;} MfDesfirePollerEventData;
typedef struct {int type;MfDesfirePollerEventData* data;} MfDesfirePollerEvent;
typedef struct {int protocol;void* event_data;void* instance;} NfcGenericEvent;
typedef int (*NfcGenericCallback)(NfcGenericEvent,void*);
typedef struct MfDesfirePoller {
 void* iso14443_4a_poller; NxpNativeCommandMode command_mode;
 MfDesfirePollerSessionState session_state; MfDesfirePollerState state;
 MfDesfireError error; MfDesfireData* data;
 BitBuffer *tx_buffer,*rx_buffer,*input_buffer,*result_buffer;
 MfDesfirePollerEventData mf_desfire_event_data; MfDesfirePollerEvent mf_desfire_event;
 NfcGenericEvent general_event;
 NfcGenericCallback callback; void* context;
} MfDesfirePoller;
static void* host_malloc(size_t n){void* p=calloc(1,n);
 if(n==sizeof(MfDesfirePoller))memset(p,0xa5,n);return p;}
enum {MF_DESFIRE_BUF_SIZE=64,MF_DESFIRE_RESULT_BUF_SIZE=512};
#define malloc host_malloc
static MfDesfireData payload;
MfDesfireData* mf_desfire_alloc(void){return &payload;}
BitBuffer* bit_buffer_alloc(size_t n){BitBuffer* b=calloc(1,sizeof(*b));b->bytes=n;return b;}
""" + source + r"""
int main(void){
 MfDesfirePoller* p=mf_desfire_poller_alloc((void*)1);
 assert(p->state==MfDesfirePollerStateIdle);
 assert(p->session_state==MfDesfirePollerSessionStateIdle);
 assert(p->command_mode==NxpNativeCommandModePlain);
 assert(p->error==MfDesfireErrorNone&&p->callback==NULL&&p->context==NULL);
 assert(p->iso14443_4a_poller==(void*)1);
 assert(p->data!=NULL&&p->tx_buffer!=NULL&&p->rx_buffer!=NULL);
 assert(p->input_buffer!=NULL&&p->result_buffer!=NULL);
 assert(p->mf_desfire_event.data==&p->mf_desfire_event_data);
 assert(p->general_event.event_data==&p->mf_desfire_event);
 assert(p->general_event.instance==p);
 free(p->tx_buffer);free(p->rx_buffer);free(p->input_buffer);free(p->result_buffer);free(p);
 return 0;
}
""")

    def test_reset_clears_desfire_key_settings_before_another_card(self):
        source = production(
            "lib/nfc/protocols/mf_desfire/mf_desfire.c",
            "void mf_desfire_reset(",
        )
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef struct {uint8_t hw_type,hw_major,hw_minor,hw_storage;} MfDesfireVersion;
typedef struct {uint32_t bytes_free;bool is_present;} MfDesfireFreeMemory;
typedef struct {bool is_master_key_changeable,is_free_directory_list,is_free_create_delete,
 is_config_changeable;uint8_t change_key_id,max_keys,flags;} MfDesfireKeySettings;
typedef struct {unsigned count;} SimpleArray;
typedef struct {void* iso14443_4a_data;MfDesfireVersion version;MfDesfireFreeMemory free_memory;
 MfDesfireKeySettings master_key_settings;SimpleArray* master_key_versions;
 SimpleArray* application_ids;SimpleArray* applications;} MfDesfireData;
#define furi_check(x) assert(x)
static void iso14443_4a_reset(void* p){(void)p;}
void simple_array_reset(SimpleArray* p){p->count=0;}
''' + source + r'''
int main(void){
 SimpleArray keys={9},ids={2},apps={3};
 MfDesfireData data={.master_key_settings={.is_master_key_changeable=true,
  .is_free_directory_list=true,.max_keys=14,.flags=0xff},
  .master_key_versions=&keys,.application_ids=&ids,.applications=&apps};
 mf_desfire_reset(&data);
 assert(!data.master_key_settings.is_master_key_changeable);
 assert(!data.master_key_settings.is_free_directory_list&&data.master_key_settings.max_keys==0);
 assert(keys.count==0&&ids.count==0&&apps.count==0);
 return 0;
}
''')

    def test_light_identity_unknown_size_and_legacy_enum_values(self):
        source = production(
            "lib/nfc/protocols/mf_desfire/mf_desfire.c",
            "static MfDesfireType mf_desfire_get_type_from_version(",
        )
        source += "\n" + production(
            "lib/nfc/protocols/mf_desfire/mf_desfire.c",
            "static MfDesfireSize mf_desfire_get_size_from_version(",
        )
        source += "\n" + production(
            "lib/nfc/protocols/mf_desfire/mf_desfire.c",
            "const char* mf_desfire_get_device_name(",
        )
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string.h>
typedef enum { MfDesfireTypeMF3ICD40, MfDesfireTypeEV1, MfDesfireTypeEV2,
       MfDesfireTypeEV2XL, MfDesfireTypeEV3, MfDesfireTypeUnknown,
       MfDesfireTypeLight, MfDesfireTypeNum } MfDesfireType;
typedef enum { MfDesfireSize2k, MfDesfireSize4k, MfDesfireSize8k,
       MfDesfireSize16k, MfDesfireSize32k, MfDesfireSizeUnknown } MfDesfireSize;
typedef enum { NfcDeviceNameTypeSimple, NfcDeviceNameTypeFull } NfcDeviceNameType;
enum { MF_DESFIRE_HW_MAJOR_TYPE_EV1=1, MF_DESFIRE_HW_MAJOR_TYPE_EV2=0x12,
       MF_DESFIRE_HW_MAJOR_TYPE_EV2_XL=0x22, MF_DESFIRE_HW_MAJOR_TYPE_EV3=0x33,
       MF_DESFIRE_HW_MAJOR_TYPE_MF3ICD40=0, MF_DESFIRE_HW_MINOR_TYPE_MF3ICD40=2,
       MF_DESFIRE_STORAGE_SIZE_MF3ICD40=0xff,
       MF_DESFIRE_STORAGE_SIZE_2K=0x16, MF_DESFIRE_STORAGE_SIZE_4K=0x18,
       MF_DESFIRE_STORAGE_SIZE_8K=0x1a, MF_DESFIRE_STORAGE_SIZE_16K=0x1c,
       MF_DESFIRE_STORAGE_SIZE_32K=0x1e };
#define MF_DESFIRE_PROTOCOL_NAME "Mifare DESFire"
#define MF_DESFIRE_HW_TYPE_MASK 0x0f
#define MF_DESFIRE_HW_TYPE_LIGHT 0x08
#define MF_DESFIRE_TEST_TYPE_MF3ICD40(m,n,s) ((m)==0 && (n)==2 && (s)==0xff)
#define furi_check(x) assert(x)
_Static_assert(MfDesfireTypeUnknown == 5, "preserve saved/extension enum values");
_Static_assert(MfDesfireTypeLight == 6, "append Light after existing IDs");
typedef struct {uint8_t hw_type,hw_major,hw_minor,hw_storage;} MfDesfireVersion;
typedef struct {char text[96];} FuriString;
typedef struct {MfDesfireVersion version;FuriString* device_name;} MfDesfireData;
static const char* mf_desfire_type_strings[]={"(MF3ICD40)","EV1","EV2","EV2 XL","EV3","UNK","Light"};
static const char* mf_desfire_size_strings[]={"2K","4K","8K","16K","32K",""};
static void furi_string_printf(FuriString* s,const char* fmt,...) {
    va_list ap; va_start(ap,fmt); vsnprintf(s->text,sizeof(s->text),fmt,ap); va_end(ap);
}
static void furi_string_set_str(FuriString* s,const char* value) {
    snprintf(s->text,sizeof(s->text),"%s",value);
}
static const char* furi_string_get_cstr(const FuriString* s){return s->text;}
''' + source + r'''
int main(void) {
    FuriString name={0}; MfDesfireData data={.device_name=&name};
    data.version=(MfDesfireVersion){.hw_type=8,.hw_major=0x30,.hw_storage=0x13};
    assert(mf_desfire_get_type_from_version(&data.version)==MfDesfireTypeLight);
    assert(mf_desfire_get_size_from_version(&data.version)==MfDesfireSizeUnknown);
    assert(!strcmp(mf_desfire_get_device_name(&data,NfcDeviceNameTypeFull),
                   "Mifare DESFire Light"));
    assert(!strcmp(mf_desfire_get_device_name(&data,NfcDeviceNameTypeSimple),"Light"));
    data.version=(MfDesfireVersion){.hw_type=0,.hw_major=0x33,.hw_storage=0x1e};
    assert(mf_desfire_get_type_from_version(&data.version)==MfDesfireTypeEV3);
    data.version=(MfDesfireVersion){.hw_type=0,.hw_major=0x7f};
    assert(mf_desfire_get_type_from_version(&data.version)==MfDesfireTypeUnknown);
    data.version=(MfDesfireVersion){.hw_type=8,.hw_major=0x7f};
    assert(mf_desfire_get_type_from_version(&data.version)==MfDesfireTypeLight);
    return 0;
}
''')

    def test_version_parser_accepts_light_but_not_other_product_families(self):
        source = production(
            "lib/nfc/protocols/mf_desfire/mf_desfire_i.c",
            "bool mf_desfire_version_parse(",
        )
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define MF_DESFIRE_HW_TYPE_MASK 0x0f
#define MF_DESFIRE_HW_TYPE_DESFIRE 0x01
#define MF_DESFIRE_HW_TYPE_LIGHT 0x08
typedef struct {
    uint8_t hw_vendor,hw_type,hw_subtype,hw_major,hw_minor,hw_storage,hw_proto;
    uint8_t sw_vendor,sw_type,sw_subtype,sw_major,sw_minor,sw_storage,sw_proto;
    uint8_t uid[7],batch[5],prod_week,prod_year;
} MfDesfireVersion;
typedef struct {uint8_t bytes[32];size_t length;} BitBuffer;
size_t bit_buffer_get_size_bytes(const BitBuffer* buffer){return buffer->length;}
void bit_buffer_write_bytes(const BitBuffer* buffer,void* dst,size_t size){
    memcpy(dst,buffer->bytes,size);
}
''' + source + r'''
int main(void){
    BitBuffer buffer={.length=sizeof(MfDesfireVersion)};
    MfDesfireVersion parsed={0};
    buffer.bytes[1]=MF_DESFIRE_HW_TYPE_DESFIRE;
    assert(mf_desfire_version_parse(&parsed,&buffer));
    buffer.bytes[1]=MF_DESFIRE_HW_TYPE_LIGHT;
    buffer.bytes[3]=0x30;
    assert(mf_desfire_version_parse(&parsed,&buffer));
    assert(parsed.hw_type==MF_DESFIRE_HW_TYPE_LIGHT&&parsed.hw_major==0x30);
    buffer.bytes[1]=0x02;
    assert(!mf_desfire_version_parse(&parsed,&buffer));
    buffer.bytes[1]=0x04;
    assert(!mf_desfire_version_parse(&parsed,&buffer));
    buffer.length--;
    assert(!mf_desfire_version_parse(&parsed,&buffer));
    return 0;
}
''')

    def test_light_refusals_save_an_empty_key_list_as_a_complete_read(self):
        source = production(
            "lib/nfc/protocols/mf_desfire/mf_desfire_poller.c",
            "static NfcCommand mf_desfire_poller_handler_read_master_key_settings(",
        )
        source += "\n" + production(
            "lib/nfc/protocols/mf_desfire/mf_desfire_poller.c",
            "static NfcCommand mf_desfire_poller_handler_read_master_key_version(",
        )
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef enum {MfDesfireErrorNone,MfDesfireErrorNotPresent,MfDesfireErrorProtocol,
 MfDesfireErrorTimeout,MfDesfireErrorAuthentication,MfDesfireErrorCommandNotSupported,
 MfDesfireErrorRejected} MfDesfireError;
typedef enum {NfcCommandContinue} NfcCommand;
typedef enum {MfDesfirePollerStateIdle,MfDesfirePollerStateReadVersion,
 MfDesfirePollerStateReadFreeMemory,MfDesfirePollerStateReadMasterKeySettings,
 MfDesfirePollerStateReadMasterKeyVersion,MfDesfirePollerStateReadApplicationIds,
 MfDesfirePollerStateReadApplications,MfDesfirePollerStateReadFailed,
 MfDesfirePollerStateReadSuccess,MfDesfirePollerStateNum} MfDesfirePollerState;
typedef struct {bool is_master_key_changeable,is_free_directory_list;
 bool is_free_create_delete,is_config_changeable;uint8_t change_key_id,max_keys,flags;
} MfDesfireKeySettings;
typedef struct {size_t count;} SimpleArray;
typedef struct {MfDesfireKeySettings master_key_settings;SimpleArray* master_key_versions;}
 MfDesfireData;
typedef struct {MfDesfirePollerState state;MfDesfireError error;MfDesfireData* data;
 void* iso14443_4a_poller;} MfDesfirePoller;
static unsigned halt_count;
#define FURI_LOG_D(...) ((void)0)
#define FURI_LOG_E(...) ((void)0)
 #define furi_check(x) assert(x)
static MfDesfireError mf_desfire_poller_read_key_settings(MfDesfirePoller* p,
 MfDesfireKeySettings* data){(void)p;(void)data;return MfDesfireErrorRejected;}
static MfDesfireError mf_desfire_poller_read_key_versions(MfDesfirePoller* p,
 SimpleArray* data,uint8_t max){(void)p;(void)data;(void)max;return MfDesfireErrorRejected;}
void simple_array_reset(SimpleArray* a){a->count=0;}
static void iso14443_4a_poller_halt(void* p){(void)p;halt_count++;}
''' + source + r'''
int main(void){
 SimpleArray versions={.count=1};MfDesfireData data={
  .master_key_settings={.max_keys=14,.is_free_directory_list=true},
  .master_key_versions=&versions};
 MfDesfirePoller p={.state=MfDesfirePollerStateReadMasterKeySettings,.data=&data};
 assert(mf_desfire_poller_handler_read_master_key_settings(&p)==NfcCommandContinue);
 assert(p.state==MfDesfirePollerStateReadMasterKeyVersion);
 assert(!data.master_key_settings.is_free_directory_list&&data.master_key_settings.max_keys==1);
 p.state=MfDesfirePollerStateReadMasterKeyVersion;versions.count=1;
 assert(mf_desfire_poller_handler_read_master_key_version(&p)==NfcCommandContinue);
 assert(p.error==MfDesfireErrorRejected&&p.state==MfDesfirePollerStateReadSuccess);
 assert(data.master_key_settings.max_keys==0&&versions.count==0&&halt_count==0);
 return 0;
}
''')

    def test_refused_poller_success_event_does_not_retain_old_error(self):
        source = production(
            "lib/nfc/protocols/mf_desfire/mf_desfire_poller.c",
            "static NfcCommand mf_desfire_poller_handler_read_success(",
        )
        run_c(r'''
#include <assert.h>
#include <stdint.h>
typedef enum {MfDesfireErrorNone,MfDesfireErrorProtocol,MfDesfireErrorRejected} MfDesfireError;
typedef enum {NfcCommandContinue} NfcCommand;
typedef enum {MfDesfirePollerEventTypeReadSuccess,MfDesfirePollerEventTypeReadFailed}
 MfDesfirePollerEventType;
typedef struct {MfDesfireError error;} MfDesfirePollerEventData;
typedef struct {MfDesfirePollerEventType type;MfDesfirePollerEventData* data;}
 MfDesfirePollerEvent;
typedef struct {int protocol;void* event_data;void* instance;} NfcGenericEvent;
typedef int (*NfcGenericCallback)(NfcGenericEvent,void*);
typedef struct {MfDesfirePollerEvent mf_desfire_event;NfcGenericEvent general_event;
 NfcGenericCallback callback;void* context;void* iso14443_4a_poller;} MfDesfirePoller;
static int callback_event_error=-1;
#define FURI_LOG_D(...) ((void)0)
#define furi_check(x) assert(x)
static void iso14443_4a_poller_halt(void* p){(void)p;}
static int callback(NfcGenericEvent event,void* context){(void)context;
 MfDesfirePollerEvent* e=event.event_data;callback_event_error=e->data->error;return NfcCommandContinue;}
''' + source + r'''
int main(void){
 MfDesfirePollerEventData data={.error=MfDesfireErrorRejected};
 MfDesfirePoller p={.mf_desfire_event={.type=MfDesfirePollerEventTypeReadFailed,.data=&data},
 .callback=callback};
 p.general_event.event_data=&p.mf_desfire_event;
 assert(mf_desfire_poller_handler_read_success(&p)==NfcCommandContinue);
 assert(p.mf_desfire_event.type==MfDesfirePollerEventTypeReadSuccess);
 assert(data.error==MfDesfireErrorNone&&callback_event_error==MfDesfireErrorNone);
 return 0;
}
''')

    def test_refusal_is_distinct_from_rf_transport_failure(self):
        source = production(
            "lib/nfc/protocols/mf_desfire/mf_desfire_poller_i.c",
            "MfDesfireError mf_desfire_process_error(",
        )
        source += "\n" + production(
            "lib/nfc/protocols/mf_desfire/mf_desfire_poller_i.c",
            "MfDesfireError mf_desfire_process_status_code(",
        )
        source += "\n" + production(
            "lib/nfc/protocols/mf_desfire/mf_desfire_poller_i.c",
            "bool mf_desfire_error_is_refusal(",
        )
        source += "\n" + production(
            "lib/nfc/protocols/mf_desfire/mf_desfire_poller_i.c",
            "MfDesfireError\n    mf_desfire_poller_read_version_any_mode(",
        )
        run_c(r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum {MfDesfireErrorNone,MfDesfireErrorNotPresent,MfDesfireErrorProtocol,
      MfDesfireErrorTimeout,MfDesfireErrorAuthentication,
      MfDesfireErrorCommandNotSupported,MfDesfireErrorRejected} MfDesfireError;
typedef enum {Iso14443_4aErrorNone,Iso14443_4aErrorNotPresent,Iso14443_4aErrorTimeout,
      Iso14443_4aErrorProtocol} Iso14443_4aError;
enum {NXP_NATIVE_COMMAND_STATUS_OPERATION_OK=0,NXP_NATIVE_COMMAND_STATUS_AUTHENTICATION_ERROR=0xae,
      NXP_NATIVE_COMMAND_STATUS_ILLEGAL_COMMAND_CODE=0x1c};
enum {NxpNativeCommandModePlain,NxpNativeCommandModeIsoWrapped,NxpNativeCommandModeMAX};
typedef int MfDesfireVersion;
typedef struct {unsigned command_mode;} MfDesfirePoller;
bool mf_desfire_error_is_refusal(MfDesfireError error);
MfDesfireError mf_desfire_poller_read_version_any_mode(
    MfDesfirePoller* instance,
    MfDesfireVersion* data);
#define furi_check(x) assert(x)
#define FURI_LOG_I(...) ((void)0)
static unsigned response_index;
static unsigned responses[8];
MfDesfireError mf_desfire_poller_read_version(MfDesfirePoller* p,MfDesfireVersion* v) {
    (void)p;(void)v;return (MfDesfireError)responses[response_index++];
}
''' + source + r'''
int main(void) {
    assert(mf_desfire_process_status_code(0x1c)==MfDesfireErrorRejected);
    assert(mf_desfire_process_status_code(0xae)==MfDesfireErrorAuthentication);
    assert(mf_desfire_process_status_code(0)==MfDesfireErrorNone);
    assert(mf_desfire_error_is_refusal(MfDesfireErrorRejected));
    assert(mf_desfire_error_is_refusal(MfDesfireErrorAuthentication));
    assert(!mf_desfire_error_is_refusal(MfDesfireErrorProtocol));
    assert(mf_desfire_process_error(Iso14443_4aErrorTimeout)==MfDesfireErrorTimeout);
    MfDesfirePoller p={.command_mode=NxpNativeCommandModePlain};MfDesfireVersion v=0;
    responses[0]=MfDesfireErrorRejected; responses[1]=MfDesfireErrorNone;
    response_index=0;
    assert(mf_desfire_poller_read_version_any_mode(&p,&v)==MfDesfireErrorNone);
    assert(response_index==2 && p.command_mode==NxpNativeCommandModeIsoWrapped);
    p.command_mode=NxpNativeCommandModePlain;responses[0]=MfDesfireErrorProtocol;response_index=0;
    assert(mf_desfire_poller_read_version_any_mode(&p,&v)==MfDesfireErrorProtocol);
    assert(response_index==1 && p.command_mode==NxpNativeCommandModePlain);
    p.command_mode=NxpNativeCommandModePlain;responses[0]=MfDesfireErrorRejected;
    responses[1]=MfDesfireErrorTimeout;response_index=0;
    assert(mf_desfire_poller_read_version_any_mode(&p,&v)==MfDesfireErrorTimeout);
    assert(response_index==2 && p.command_mode==NxpNativeCommandModePlain);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
