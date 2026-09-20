"""BLE Remote must select a bonded identity, never silently select another host."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c

ROOT = Path(__file__).resolve().parents[2]


class BleRemotePeersTests(unittest.TestCase):
    def test_controller_peer_policy_is_bounded_and_fail_closed(self):
        path = ROOT / "targets/f7/ble_glue/gap_peer_policy.c"
        self.assertTrue(path.exists(), "selected-peer controller policy is missing")
        source = "\n".join(l for l in path.read_text().splitlines() if not l.startswith("#include"))
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define GAP_BONDED_DEVICES_MAX 35
#define BLE_STATUS_SUCCESS 0
typedef struct {uint8_t address_type;uint8_t address[6];} GapBondedDevice;
typedef struct {uint8_t count;GapBondedDevice devices[GAP_BONDED_DEVICES_MAX];} GapBondedDevices;
typedef struct {uint8_t Address_Type;uint8_t Address[6];} Bonded_Device_Entry_t;
typedef Bonded_Device_Entry_t List_Entry_t;
static int failure,stage,removes;
static uint8_t total=2;
static int aci_gap_get_bonded_devices(uint8_t*n,Bonded_Device_Entry_t*out){
 *n=total;if(total<=35)for(unsigned i=0;i<total;i++){out[i].Address_Type=failure==2?2:i%2;memset(out[i].Address,i+1,6);}return failure==1;
}
static int aci_gap_is_device_bonded(uint8_t type,const uint8_t*addr){assert(type<2);return addr[0]!=1;}
static int aci_gap_add_devices_to_list(uint8_t n,const List_Entry_t*p,uint8_t mode){
 assert(n==1&&mode==5&&p->Address[0]==1);stage=1;return failure==3;
}
static int hci_le_set_address_resolution_enable(uint8_t enabled){
 if(enabled)assert(stage==1);stage=2;return failure==4;
}
static int aci_gap_configure_filter_accept_list(void){assert(stage==2);stage=3;return failure==5;}
static int aci_gap_remove_bonded_device(uint8_t type,const uint8_t*addr){assert(type<2&&addr[0]==1);removes++;return failure==6;}
''' + source + r'''
int main(void){
 GapBondedDevices out;memset(&out,0xA5,sizeof(out));
 assert(gap_peer_read(&out)&&out.count==2&&out.devices[1].address[0]==2);
 for(failure=1;failure<=2;failure++){assert(!gap_peer_read(&out)&&out.count==0);}
 failure=0;total=36;assert(!gap_peer_read(&out)&&out.count==0);total=0;assert(gap_peer_read(&out)&&!out.count);
 GapBondedDevice p={0,{1,1,1,1,1,1}};
 for(failure=0;failure<=4;failure++){stage=0;assert(gap_peer_select(&p)==(failure!=3&&failure!=4));}
 p.address_type=2;assert(!gap_peer_select(&p));p.address_type=0;p.address[0]=2;assert(!gap_peer_select(&p));
 p.address[0]=1;failure=0;assert(gap_peer_forget(&p)&&removes==1);failure=6;assert(!gap_peer_forget(&p));
 p.address_type=2;assert(!gap_peer_forget(&p)&&removes==2);
 failure=0;stage=0;assert(gap_peer_select(NULL)&&stage==3);
 failure=5;stage=0;assert(!gap_peer_select(NULL));
 assert(!gap_peer_read(NULL));return 0;
}
''')

    def test_hid_can_start_without_any_advertising_window(self):
        api = (ROOT / "applications/services/bt/bt_service/bt.h").read_text()
        self.assertIn("bt_profile_start_idle(", api)
        self.assertIn("bt_get_bonded_devices(", api)
        self.assertIn("bt_set_connection_peer(", api)
        self.assertIn("bt_forget_bonded_device(", api)

    def test_only_ble_remote_is_package_only(self):
        from tools.tumoflip import validate_release as release
        target = "apps/Bluetooth/hid_ble.fap"
        self.assertIn(target, release.PACKAGE_ONLY_PACKAGE_FILES)
        self.assertNotIn("apps/USB/hid_usb.fap", release.PACKAGE_ONLY_PACKAGE_FILES)
        self.assertEqual(release.package_extapp_exports()["hid_ble.fap"], target)


if __name__ == "__main__":
    unittest.main()
