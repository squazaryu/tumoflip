"""Native servo state/PWM safety; never touches a pin or physical actuator."""
from pathlib import Path
import unittest

from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/servo_tester_plus"


def model_source():
    code = APP / "servo_model.c"
    return (f'#include "{APP / "servo_model.h"}"\n#include <assert.h>\n'
            + (code.read_text().replace('#include "servo_model.h"', '') if code.exists() else ''))


class ServoPlusTests(unittest.TestCase):
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
