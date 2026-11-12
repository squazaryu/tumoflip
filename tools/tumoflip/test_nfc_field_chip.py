"""Real ST25 register constants and configuration functions, fake register I/O."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class NfcFieldChipTests(unittest.TestCase):
    def test_prepare_enables_passive_detection_and_never_enables_field_tx(self):
        source = (ROOT / "applications_user/nfc_field_sniffer/nfc_field_capture.c").read_text()
        registers = re.sub(r"^#(?:include|pragma).*\n", "\n", (ROOT / "lib/drivers/st25r3916_reg.h").read_text(), flags=re.M)
        native("#include <assert.h>\n#include <stdint.h>\n#include <stddef.h>\n#include <stdbool.h>\n#include <string.h>\ntypedef struct FuriHalSpiBusHandle FuriHalSpiBusHandle;\n" + registers + r'''
static uint8_t registers[256];
static void nfc_field_reg_write(uint8_t a,uint8_t v){registers[a]=v;assert(!(registers[ST25R3916_REG_OP_CONTROL]&ST25R3916_REG_OP_CONTROL_tx_en));}
static uint8_t nfc_field_reg_read(uint8_t a){return a==ST25R3916_REG_AUX_DISPLAY?ST25R3916_REG_AUX_DISPLAY_osc_ok:registers[a];}
static void nfc_field_reg_set(uint8_t a,uint8_t v){nfc_field_reg_write(a,registers[a]|v);}
static void nfc_field_reg_clear(uint8_t a,uint8_t v){nfc_field_reg_write(a,registers[a]&~v);}
static void nfc_field_command(uint8_t c){assert(c!=ST25R3916_CMD_TRANSMIT_WITH_CRC&&c!=ST25R3916_CMD_TRANSMIT_WITHOUT_CRC&&c!=ST25R3916_CMD_TRANSMIT_REQA&&c!=ST25R3916_CMD_TRANSMIT_WUPA);if(c==ST25R3916_CMD_SET_DEFAULT)memset(registers,0,sizeof(registers));}
static void furi_delay_ms(unsigned t){(void)t;}
''' + function(source, "static void nfc_field_chip_prepare(") + function(source, "static void nfc_field_chip_sniff(") + r'''
int main(void){
 nfc_field_chip_prepare();assert((registers[ST25R3916_REG_OP_CONTROL]&ST25R3916_REG_OP_CONTROL_en_fd_mask)==ST25R3916_REG_OP_CONTROL_en_fd_manual_efd_pdt);
 nfc_field_chip_sniff();assert((registers[ST25R3916_REG_OP_CONTROL]&ST25R3916_REG_OP_CONTROL_en_fd_mask)==ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);
 assert(registers[ST25R3916_REG_OP_CONTROL]&ST25R3916_REG_OP_CONTROL_rx_en);return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
