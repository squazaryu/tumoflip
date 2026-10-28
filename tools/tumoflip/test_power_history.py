"""Bounded passive power history; no battery/charger or device is accessed."""
from pathlib import Path
import unittest
from tools.tumoflip.test_nfc_completion_equality import native
from tools.tumoflip.test_field_wallet import storage_native

ROOT=Path(__file__).resolve().parents[2]
APP=ROOT/'applications/system/device_library'


def production():
    source=APP/'power_history.c'
    return (f'#include "{APP / "power_history.h"}"\n#include <assert.h>\n#include <string.h>\n'
            +(source.read_text().replace('#include "power_history.h"','') if source.exists() else ''))


class PowerHistoryTests(unittest.TestCase):
    def test_atomic_history_save_checked_csv_and_no_cross_launch_integration(self):
        storage_native(r'''
int main(int argc,char**argv){assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);
 PowerHistory h,read;power_history_init(&h);
 assert(power_store_load(&s,&read)==PowerStoreEmpty);
 PowerHistorySample sample={.voltage_mv=3800,.temperature_tenths=250,.current_ma=-100,.soc=90,.gauge_health=95,.gauge_ok=1,.timestamp=123};
 for(unsigned i=0;i<10;i++){sample.tick_ms=i*5000;assert(power_history_add(&h,&sample));}
 assert(power_store_save(&s,&h));assert(power_store_load(&s,&read)==PowerStoreOk&&read.count==10&&!read.continuous);
 sample.tick_ms+=5000;sample.soc=89;assert(power_history_add(&h,&sample));
 write_budget=1;assert(!power_store_save(&s,&h));write_budget=-1;
 assert(power_store_load(&s,&read)==PowerStoreOk&&read.count==10);
 char a[256],b[256];assert(power_store_export(&s,&h,a,sizeof(a)));assert(power_store_export(&s,&h,b,sizeof(b))&&strcmp(a,b));
 File*f=storage_file_alloc(&s);assert(storage_file_open(f,a,FSAM_READ,FSOM_OPEN_EXISTING));char text[2048]={0};assert(storage_file_read(f,text,sizeof(text)-1)>0);assert(strstr(text,"voltage_mv")&&strstr(text,"3800"));assert(storage_file_close(f));storage_file_free(f);
 write_budget=4;assert(!power_store_export(&s,&h,b,sizeof(b)));write_budget=-1;
 assert(storage_common_stat(&s,a,NULL)==FSE_OK);
 return 0;}
''',('field_io.h','field_io.c','power_history.h','power_history.c','power_store.h','power_store.c'))

    def test_bounded_history_invalid_gauge_and_saved_state_validation(self):
        native(production()+r'''
int main(void){
 PowerHistory h;power_history_init(&h);assert(power_history_valid(&h));
 PowerHistorySample s={.voltage_mv=3800,.temperature_tenths=250,.current_ma=-100,.soc=90,.gauge_health=95,.gauge_ok=true};
 for(unsigned i=0;i<10000;i++){s.tick_ms=i*5000U;s.timestamp=1000+i*5;assert(power_history_add(&h,&s));}
 assert(h.count==POWER_HISTORY_POINTS);assert(power_history_at(&h,0)->timestamp==1000+(10000-128)*5);
 assert(!power_history_at(&h,128));
 unsigned count=h.count;s.soc=101;assert(!power_history_add(&h,&s)&&h.count==count);
 s.soc=90;s.gauge_ok=false;assert(!power_history_add(&h,&s));
 s.gauge_ok=true;s.voltage_mv=6000;assert(!power_history_add(&h,&s));
 h.head=129;assert(!power_history_valid(&h));
 return 0;}
''')

    def test_capacity_requires_three_qualified_segments_and_rejects_outlier(self):
        native(production()+r'''
int main(void){PowerHistory h;power_history_init(&h);uint16_t cap;
 assert(!power_history_estimate(&h,&cap));
 PowerHistorySample s={.voltage_mv=3800,.temperature_tenths=250,.current_ma=-100,.soc=100,.gauge_health=95,.gauge_ok=true};
 for(unsigned i=0;i<=1620;i++){
  s.tick_ms=i*10000U;s.soc=100-i/36;assert(power_history_add(&h,&s));
  if(i<1620)assert(!power_history_estimate(&h,&cap));
 }
 assert(h.accepted==3&&power_history_estimate(&h,&cap)&&cap==1000);
 for(unsigned i=1621;i<=2160;i++){s.tick_ms=i*10000U;s.soc=100-i/36;s.current_ma=-500;assert(power_history_add(&h,&s));}
 assert(h.accepted==3&&power_history_estimate(&h,&cap)&&cap==1000);
 assert(power_history_valid(&h));return 0;}
''')

    def test_gap_restart_usb_charge_and_tick_wrap_never_create_false_estimates(self):
        native(production()+r'''
int main(void){PowerHistory h;power_history_init(&h);
 PowerHistorySample s={.voltage_mv=3800,.temperature_tenths=250,.current_ma=-100,.soc=90,.gauge_health=95,.gauge_ok=true,.tick_ms=UINT32_MAX-1000};
 assert(power_history_add(&h,&s));s.tick_ms=3999;s.soc=89;assert(power_history_add(&h,&s));assert(h.duration_ms==5000);
 s.tick_ms+=20000;s.soc=60;assert(power_history_add(&h,&s)&&h.duration_ms==0&&!h.accepted);
 power_history_pause(&h);s.tick_ms+=1000;s.soc=40;assert(power_history_add(&h,&s)&&!h.accepted);
 s.tick_ms+=5000;s.charging=true;s.current_ma=100;assert(power_history_add(&h,&s)&&!h.continuous);
 s.charging=false;s.usb=true;s.tick_ms+=5000;assert(power_history_add(&h,&s)&&!h.continuous);
 s.usb=false;s.current_ma=-100;s.temperature_tenths=500;s.tick_ms+=5000;assert(power_history_add(&h,&s)&&!h.continuous);
 return 0;}
''')


if __name__=='__main__':unittest.main()
