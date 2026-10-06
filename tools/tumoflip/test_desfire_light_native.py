"""Run production DESFire identity and poller mode/status code on the host."""

from pathlib import Path
import unittest

from tools.tumoflip.test_hotplug_assets import function, run_c

ROOT = Path(__file__).resolve().parents[2]


def production(relative, signature):
    text = (ROOT / relative).read_text(encoding="utf-8")
    return function(text, signature) if signature in text else ""


class DesfireLightNativeTests(unittest.TestCase):
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
            "lib/nfc/protocols/mf_desfire/mf_desfire_poller_i.h",
            "static inline bool mf_desfire_error_is_refusal(",
        )
        source += "\n" + production(
            "lib/nfc/protocols/mf_desfire/mf_desfire_poller_i.c",
            "static MfDesfireError mf_desfire_poller_read_version_any_mode(",
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
#define furi_check(x) assert(x)
#define FURI_LOG_I(...) ((void)0)
static unsigned response_index;
static unsigned responses[8];
static MfDesfireError mf_desfire_poller_read_version(MfDesfirePoller* p,MfDesfireVersion* v) {
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
