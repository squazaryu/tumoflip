"""Bounded CC1101 packet configuration and channel arithmetic."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import run_c, function

ROOT = Path(__file__).resolve().parents[2]


class PacketConfigNativeTests(unittest.TestCase):
    def test_rejected_frequency_never_starts_thread(self):
        source = (ROOT / "lib/subghz/subghz_tx_rx_worker.c").read_text()
        run_c(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
typedef int SubGhzDevice;
typedef struct {bool worker_running;void *stream_tx,*stream_rx,*thread;uint32_t frequency;const SubGhzDevice* device;} SubGhzTxRxWorker;
#define furi_check assert
static unsigned threads;
void furi_stream_buffer_reset(void* p) {(void)p;}
bool furi_hal_subghz_is_tx_allowed(uint32_t f) {(void)f;return false;}
void furi_thread_start(void* p) {(void)p;threads++;}
''' + function(source, "bool subghz_tx_rx_worker_start(") + r'''
int main(void) {
    SubGhzTxRxWorker w={0};SubGhzDevice d=0;
    assert(!subghz_tx_rx_worker_start(&w,&d,100));
    assert(threads==0 && !w.worker_running);
    return 0;
}
''')

    def test_bounded_packet_preset_and_channel_frequency(self):
        run_c('#include <assert.h>\n#include <string.h>\n'
              + f'#include "{ROOT}/lib/subghz/packet_config.c"\n' + r'''
int main(void) {
    uint8_t good[]={2,6,8,5,0,0,0x12,0,0,0,0,0,0,0};
    assert(subghz_packet_preset_is_valid(good,sizeof(good)));
    for(size_t n=0;n<sizeof(good);n++)assert(!subghz_packet_preset_is_valid(good,n));
    assert(!subghz_packet_preset_is_valid(NULL,sizeof(good)));
    uint8_t wrong[sizeof(good)];memcpy(wrong,good,sizeof(good));
    wrong[3]=4;assert(!subghz_packet_preset_is_valid(wrong,sizeof(wrong))); // fixed length
    wrong[3]=1;assert(!subghz_packet_preset_is_valid(wrong,sizeof(wrong))); // CRC absent
    wrong[3]=0x35;assert(!subghz_packet_preset_is_valid(wrong,sizeof(wrong))); // async format
    wrong[3]=5;wrong[1]=0;assert(!subghz_packet_preset_is_valid(wrong,sizeof(wrong))); // no sync GDO0
    wrong[1]=6;wrong[0]=0x40;assert(!subghz_packet_preset_is_valid(wrong,sizeof(wrong))); // invalid register
    uint8_t trailing[sizeof(good)+1];memcpy(trailing,good,sizeof(good));trailing[sizeof(good)]=0;
    assert(!subghz_packet_preset_is_valid(trailing,sizeof(trailing)));
    uint8_t oversized[257]={0};assert(!subghz_packet_preset_is_valid(oversized,sizeof(oversized)));
    assert(subghz_packet_channel_frequency(0x10b071,0,0,0)==433919830);
    assert(subghz_packet_channel_frequency(0x10b071,0,0,1)==433945220);
    assert(subghz_packet_channel_frequency(0x10b071,255,3,255)==537311248);
    assert(subghz_packet_channel_frequency(0x1000000,0,0,0)==0); // invalid 24-bit word
    return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
