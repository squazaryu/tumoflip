"""Native servo state/PWM safety; never touches a pin or physical actuator."""
from pathlib import Path
import re
import unittest

from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/servo_tester_plus"


def model_source():
    code = APP / "servo_model.c"
    return (f'#include "{APP / "servo_model.h"}"\n#include <assert.h>\n'
            + (code.read_text().replace('#include "servo_model.h"', '') if code.exists() else ''))


class ServoPlusTests(unittest.TestCase):
    def test_real_backend_owns_pwm_restores_only_owned_power_and_cleans_every_failure(self):
        header = re.sub(r'^#(?:include|pragma).*\n', '', (APP/'servo_hardware.h').read_text(), flags=re.M)
        implementation=APP/'servo_hardware.c'
        code=re.sub(r'^#include .*\n', '', implementation.read_text(), flags=re.M) if implementation.exists() else ''
        native(model_source()+r'''
#include <stddef.h>
#define RECORD_SUBGHZ_RADIO_BROKER "radio"
#define FURI_CRITICAL_ENTER() do{assert(!critical);critical=true;}while(0)
#define FURI_CRITICAL_EXIT() do{assert(critical);critical=false;}while(0)
typedef int SubGhzRadioBroker;typedef unsigned SubGhzRadioBrokerLease;
enum {FuriHalPwmOutputIdTim1PA7,SubGhzRadioBrokerStateCleaningUp};
enum {LL_TIM_CHANNEL_CH1N,LL_TIM_OCPOLARITY_LOW};
static unsigned opened,leases,starts,stops,enables,disables,compares;
static bool critical,deny,busy,power,power_fail;
static uint32_t compare_value,period=64000;
#define TIM1 (&period)
void* furi_record_open(const char*r){(void)r;opened++;return &opened;}
void furi_record_close(const char*r){(void)r;assert(opened);opened--;}
bool subghz_radio_broker_acquire(SubGhzRadioBroker*b,const char*n,unsigned t,SubGhzRadioBrokerLease*l){(void)b;assert(n&&t<=500);if(deny)return false;assert(!leases);leases++;*l=1;return true;}
void subghz_radio_broker_release(SubGhzRadioBroker*b,SubGhzRadioBrokerLease*l){(void)b;assert(leases&&*l);leases--;}
void subghz_radio_broker_set_state(SubGhzRadioBroker*b,SubGhzRadioBrokerLease*l,int s){(void)b;(void)s;assert(leases&&*l);}
bool furi_hal_pwm_is_running(int c){assert(c==FuriHalPwmOutputIdTim1PA7);return busy;}
void furi_hal_pwm_start(int c,unsigned f,unsigned d){assert(critical&&leases&&c==FuriHalPwmOutputIdTim1PA7&&f==50&&d==0&&!busy);busy=true;starts++;}
void furi_hal_pwm_stop(int c){assert(c==FuriHalPwmOutputIdTim1PA7&&leases&&busy);busy=false;stops++;}
bool furi_hal_power_is_otg_enabled(void){return power;}
bool furi_hal_power_enable_otg(void){enables++;if(power_fail)return false;power=true;return true;}
void furi_hal_power_disable_otg(void){disables++;power=false;}
uint32_t LL_TIM_GetAutoReload(void*t){assert(t==TIM1);return period-1;}
void LL_TIM_OC_SetCompareCH1(void*t,uint32_t v){assert(critical&&leases&&busy&&t==TIM1&&v>0&&v<period);compare_value=v;compares++;}
void LL_TIM_OC_SetPolarity(void*t,int ch,int polarity){assert(critical&&t==TIM1&&ch==LL_TIM_CHANNEL_CH1N&&polarity==LL_TIM_OCPOLARITY_LOW);}
''' + header+code+r'''
int main(void){
 ServoHardware h={0};
 assert(servo_hardware_start(&h,499,false)==ServoHardwareInvalid&&!opened&&!starts);
 deny=true;assert(servo_hardware_start(&h,1500,false)==ServoHardwareBusy&&!opened&&!leases);deny=false;
 busy=true;assert(servo_hardware_start(&h,1500,false)==ServoHardwareBusy&&busy&&!opened&&!leases&&!stops);busy=false;
 power_fail=true;assert(servo_hardware_start(&h,1500,true)==ServoHardwarePowerError&&!opened&&!leases&&!busy);power_fail=false;
 power=true;assert(servo_hardware_start(&h,1500,true)==ServoHardwareOk&&compares&&compare_value==4800);
 assert(servo_hardware_start(&h,1500,true)==ServoHardwareBusy);
 assert(servo_hardware_apply(&h,1510)&&compare_value==4832);
 servo_hardware_stop(&h);assert(power&&!disables&&!opened&&!leases&&!busy);
 power=false;assert(servo_hardware_start(&h,1500,true)==ServoHardwareOk&&power);
 assert(!servo_hardware_apply(&h,3000));assert(!busy&&!power&&!opened&&!leases&&disables==1);
 assert(servo_hardware_start(&h,1500,false)==ServoHardwareOk&&!power);
 servo_hardware_stop(&h);servo_hardware_stop(&h);servo_hardware_stop(NULL);
 assert(!opened&&!leases&&!busy&&!critical);assert(starts==stops);
 return 0;
}
''')

    def test_bounded_modes_calibration_and_explicit_arm(self):
        native(model_source() + r'''
int main(void) {
    ServoModel s; servo_model_init(&s);
    assert(!s.armed && s.minimum_us==1000 && s.maximum_us==2000 && s.pulse_us==1500);
    assert(!servo_model_tick(&s,1000));
    assert(!servo_model_limits(&s,499,2000));
    assert(!servo_model_limits(&s,1500,1500));
    assert(!servo_model_limits(&s,1000,2501));
    assert(servo_model_limits(&s,900,2100));
    assert(servo_model_mode(&s,ServoCenter,0)&&s.pulse_us==1500&&!s.armed);
    assert(!servo_model_mode(&s,(ServoMode)42,0));
    assert(servo_model_mode(&s,ServoManual,0));
    for(unsigned i=0;i<1000;i++)servo_model_nudge(&s,1);
    assert(s.pulse_us==2100);
    for(unsigned i=0;i<1000;i++)servo_model_nudge(&s,-1);
    assert(s.pulse_us==900);
    servo_model_arm(&s,0);assert(s.armed);
    assert(!servo_model_limits(&s,1000,2000)); // calibrate only while stopped
    servo_model_stop(&s);assert(!s.armed);
    return 0;
}
''')

    def test_sweep_tick_wrap_pause_and_stop_are_fail_safe(self):
        native(model_source() + r'''
int main(void) {
    ServoModel s;servo_model_init(&s);
    assert(servo_model_mode(&s,ServoSweep,UINT32_MAX-20));
    servo_model_arm(&s,UINT32_MAX-20);
    assert(!servo_model_tick(&s,UINT32_MAX-1));
    assert(servo_model_tick(&s,29));
    uint32_t t=29;
    for(unsigned i=0;i<100000;i++){
        t+=50;servo_model_tick(&s,t);
        assert(s.armed&&s.pulse_us>=s.minimum_us&&s.pulse_us<=s.maximum_us);
    }
    assert(servo_model_tick(&s,t+3000));assert(!s.armed); // stalled GUI must not restart sweep
    return 0;
}
''')

    def test_compare_uses_microsecond_resolution_not_integer_percent(self):
        native(model_source() + r'''
int main(void) {
    uint32_t compare=0;
    assert(!servo_compare_ticks(0,1500,&compare));
    assert(!servo_compare_ticks(64000,499,&compare));
    assert(!servo_compare_ticks(64000,2501,&compare));
    assert(!servo_compare_ticks(64000,1500,0));
    for(unsigned us=500;us<=2500;us++){
        assert(servo_compare_ticks(64000,us,&compare));
        assert(compare==(uint32_t)((uint64_t)64000*us/20000));
    }
    uint32_t a,b;assert(servo_compare_ticks(64000,1500,&a));
    assert(servo_compare_ticks(64000,1510,&b)&&b>a);
    assert(servo_compare_ticks(UINT32_MAX,2500,&compare));
    assert(compare==UINT32_MAX/8);
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
