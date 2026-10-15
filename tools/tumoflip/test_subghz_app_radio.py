"""Broker-owned radio selection for Nice O-Code and Security+ PIN."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class SubGhzAppRadioTests(unittest.TestCase):
    def test_clients_use_the_shared_lease_and_disconnect_checks(self):
        nice = (ROOT / "applications/system/nice_o_code/nice_o_code.c").read_text()
        pin = (ROOT / "applications/system/secplus_pin/secplus_pin_radio.c").read_text()
        for source in (nice, pin):
            self.assertIn('"../subghz_app_radio.h"', source)
            self.assertIn("subghz_app_radio_acquire(", source)
            self.assertIn("subghz_app_radio_release(", source)
            self.assertIn("subghz_app_radio_is_connected(", source)
            self.assertNotIn("furi_hal_power_enable_otg", source)
            self.assertNotIn("furi_hal_power_disable_otg", source)
        for app in ("nice_o_code", "secplus_pin"):
            self.assertIn('"subghz_radio_broker"', (ROOT / f"applications/system/{app}/application.fam").read_text())

    def test_lease_power_and_failure_cleanup(self):
        path = ROOT / "applications/system/subghz_app_radio.h"
        header = path.read_text() if path.exists() else ""
        header = re.sub(r"^#(?:include|pragma).*$", "", header, flags=re.M)
        native(r'''
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#define RECORD_SUBGHZ_RADIO_BROKER "broker"
#define SUBGHZ_DEVICE_CC1101_EXT_NAME "external"
#define SUBGHZ_DEVICE_CC1101_INT_NAME "internal"
typedef struct {int id;} SubGhzDevice;
typedef struct {int id;} SubGhzRadioBroker;
typedef struct {uint32_t token;} SubGhzRadioBrokerLease;
enum {SubGhzRadioBrokerDeviceInternal,SubGhzRadioBrokerDeviceExternalCC1101,SubGhzRadioBrokerStateInitialized};
static SubGhzDevice internal={1},external={2};static SubGhzRadioBroker broker;
static bool busy,ext_present,begin_fail,power_fail,frequency_bad,power,owns_power;
static int opened,closed,init,deinit,begun,ended,idled,selected,released;
void* furi_record_open(const char* n){(void)n;opened++;return &broker;}
void furi_record_close(const char* n){(void)n;closed++;}
bool subghz_radio_broker_acquire(SubGhzRadioBroker* b,const char* o,uint32_t wait,SubGhzRadioBrokerLease* l){(void)b;(void)o;assert(wait==0);if(busy)return false;busy=true;l->token=1;return true;}
bool subghz_radio_broker_external_power_on(SubGhzRadioBroker* b,const SubGhzRadioBrokerLease* l){(void)b;assert(l->token);if(power_fail)return false;owns_power=!power;power=true;return true;}
void subghz_radio_broker_external_power_off(SubGhzRadioBroker* b,const SubGhzRadioBrokerLease* l){(void)b;assert(l->token);if(owns_power)power=false;owns_power=false;}
void subghz_radio_broker_release(SubGhzRadioBroker* b,SubGhzRadioBrokerLease* l){subghz_radio_broker_external_power_off(b,l);l->token=0;busy=false;released++;}
bool subghz_radio_broker_set_selected_device(SubGhzRadioBroker* b,const SubGhzRadioBrokerLease* l,int d){(void)b;assert(l->token);selected=d;return true;}
bool subghz_radio_broker_set_state(SubGhzRadioBroker* b,const SubGhzRadioBrokerLease* l,int d){(void)b;(void)d;assert(l->token);return true;}
void subghz_devices_init(void){init++;}
void subghz_devices_deinit(void){deinit++;}
const SubGhzDevice* subghz_devices_get_by_name(const char* n){return !strcmp(n,"external")?&external:&internal;}
bool subghz_devices_is_connect(const SubGhzDevice* d){return d==&internal || ext_present;}
bool subghz_devices_begin(const SubGhzDevice* d){assert(d);if(begin_fail)return false;begun++;return true;}
void subghz_devices_end(const SubGhzDevice* d){assert(d);ended++;}
void subghz_devices_idle(const SubGhzDevice* d){assert(d==&internal || ext_present);idled++;}
void subghz_devices_sleep(const SubGhzDevice* d){assert(d==&internal || ext_present);}
bool subghz_devices_is_frequency_valid(const SubGhzDevice* d,uint32_t f){(void)d;(void)f;return !frequency_bad;}
''' + header + r'''
#ifndef TUMO_SUBGHZ_APP_RADIO_H
typedef struct {const SubGhzDevice* device;bool external;} SubGhzAppRadio;
static bool subghz_app_radio_acquire(SubGhzAppRadio*,const char*,uint32_t);
static void subghz_app_radio_release(SubGhzAppRadio*);
static bool subghz_app_radio_is_connected(const SubGhzAppRadio*);
static bool subghz_app_radio_acquire_internal(SubGhzAppRadio*,const char*,uint32_t);
#endif
int main(void){
    SubGhzAppRadio r={0};busy=true;
    assert(!subghz_app_radio_acquire(&r,"test",433920000));assert(opened==closed && !init);
    busy=false;ext_present=true;
    assert(subghz_app_radio_acquire(&r,"test",433920000));assert(r.device==&external && r.external && power);
    assert(selected==SubGhzRadioBrokerDeviceExternalCC1101 && subghz_app_radio_is_connected(&r));
    assert(!subghz_app_radio_acquire(&r,"twice",433920000));
    ext_present=false;assert(!subghz_app_radio_is_connected(&r));int old_idle=idled;
    subghz_app_radio_release(&r);assert(idled==old_idle && !busy && !power && begun==ended && init==deinit && opened==closed);
    int old_release=released;subghz_app_radio_release(&r);assert(released==old_release);
    power=true;assert(subghz_app_radio_acquire(&r,"test",433920000));assert(r.device==&internal && !r.external);
    subghz_app_radio_release(&r);assert(power && !busy);
    power=false;begin_fail=true;assert(!subghz_app_radio_acquire(&r,"test",433920000));assert(!busy && opened==closed && init==deinit && begun==ended);
    begin_fail=false;frequency_bad=true;assert(!subghz_app_radio_acquire(&r,"test",1));assert(!busy && begun==ended);
    frequency_bad=false;power_fail=true;assert(subghz_app_radio_acquire(&r,"test",433920000));assert(r.device==&internal);
    subghz_app_radio_release(&r);assert(!busy && opened==closed && init==deinit && begun==ended);
    power_fail=false;ext_present=true;
    assert(subghz_app_radio_acquire_internal(&r,"legacy",433920000));assert(r.device==&internal && !r.external);
    subghz_app_radio_release(&r);assert(!busy && begun==ended);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
