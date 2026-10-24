"""Quac's adapted Community hold logic keeps cancellation and safe saving."""
from pathlib import Path
import unittest
from tools.tumoflip.test_subremote_lifecycle import compile_run, function

ROOT = Path(__file__).resolve().parents[2]
QUAC = ROOT / "applications_user/quac"


class QuacHoldTests(unittest.TestCase):
    def test_uses_checked_saving_and_never_deletes_the_original(self):
        source = (QUAC / "actions/action_subghz.c").read_text()
        self.assertIn("signal_file_update(", source)
        self.assertNotIn("storage_simply_remove(", source)
        self.assertNotIn("flipper_format_delete_key", source)

    def test_actual_polling_finishes_after_release_and_allows_cancel(self):
        source = (QUAC / "actions/action_subghz.c").read_text()
        self.assertIn("quac_action_wait_tx(", source)
        production = function(source, "quac_action_wait_tx")
        compile_run(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
typedef int QuacActionWait;
typedef int SubGhzTxRx;
static struct {bool endless_tx;} subghz_block_generic_global;
static uint32_t tick,until,polls;
static bool cancel;
static uint32_t furi_get_tick(void){return tick;}
static uint32_t furi_ms_to_ticks(uint32_t ms){return ms;}
static bool subghz_txrx_tx_is_complete(SubGhzTxRx* t){(void)t;return polls>=until;}
static bool quac_action_wait_run(QuacActionWait* w,uint32_t ms){
 (void)w;polls++;tick+=ms;return !cancel;
}
''' + production + r'''
int main(void){QuacActionWait wait=0;SubGhzTxRx radio=0;
 until=35;subghz_block_generic_global.endless_tx=true;
 assert(quac_action_wait_tx(&wait,&radio,false,100));
 assert(polls==35&&tick==175&&!subghz_block_generic_global.endless_tx);
 // RAW is one complete file regardless of duration; cancellation still works.
 polls=0;tick=0;until=100;subghz_block_generic_global.endless_tx=false;
 assert(quac_action_wait_tx(&wait,&radio,true,100));assert(polls==100);
 polls=0;cancel=true;assert(!quac_action_wait_tx(&wait,&radio,true,100));assert(polls==1);
 polls=0;cancel=false;tick=UINT32_MAX-49;until=30;subghz_block_generic_global.endless_tx=true;
 assert(quac_action_wait_tx(&wait,&radio,false,100));assert(!subghz_block_generic_global.endless_tx);
 return 0;}
''')


if __name__ == "__main__":
    unittest.main()
