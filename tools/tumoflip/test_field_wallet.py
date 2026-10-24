"""Bounded field-wallet model, legacy cards, pins and explicit native launch."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/device_library"
FIXTURES = ROOT / "tools/tumoflip/fixtures"


def storage_native(program):
    sources = [APP / "library_model.h", APP / "library_model.c", APP / "card_store.h",
               APP / "card_store.c", APP / "pin_store.c"]
    body = '#include "library_io_stubs.h"\n'
    for source in sources:
        if source.exists():
            body += re.sub(r"^#(?:include|pragma).*\n", "", source.read_text(), flags=re.M) + "\n"
    body += program
    with tempfile.TemporaryDirectory(prefix="field-wallet-test-") as folder:
        work = Path(folder)
        (work / "sd").mkdir()
        (work / "test.c").write_text(body)
        flags = ["-fprofile-instr-generate", "-fcoverage-mapping"] if os.environ.get("TUMO_WALLET_COVERAGE") else []
        result = subprocess.run(["cc", "-std=c11", "-D_DEFAULT_SOURCE", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", *flags,
            '-DMBEDTLS_CONFIG_FILE="library_crypto_config.h"', "-I", str(FIXTURES), "-I", str(ROOT / "lib/mbedtls/include"),
            str(work / "test.c"), str(ROOT / "lib/mbedtls/library/sha256.c"),
            str(ROOT / "lib/mbedtls/library/platform_util.c"), "-o", str(work / "test")], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        result = subprocess.run([str(work / "test"), str(work / "sd")], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)


class FieldWalletTests(unittest.TestCase):
    def test_labels_routes_pins_and_remove_never_rewrite_source_paths(self):
        storage_native(r'''
int main(void){
 DeviceCard c={.name="Garage"};strcpy(c.links[0],"/ext/nfc/Pass.nfc");strcpy(c.links[1],"/ext/lfrfid/Entry.rfid");c.link_count=2;
 strcpy(c.labels[0],"Pass");strcpy(c.labels[1],"Entry");assert(library_card_valid(&c));
 char label[48];assert(library_action_label(&c,0,label,sizeof(label))&&!strcmp(label,"Pass"));
 c.labels[0][0]=0;assert(library_action_label(&c,0,label,sizeof(label))&&!strcmp(label,"Pass.nfc"));
 assert(!library_action_label(&c,2,label,sizeof(label)));assert(!library_action_label(&c,0,label,1));
 assert(library_file_kind("/ext/A.RFID")==LibraryFileRfid);
 assert(library_file_kind("/ext/A.ibtn")==LibraryFileIbutton);
 assert(library_file_kind("/ext/A.nfc")==LibraryFileNfc);
 assert(library_file_kind("/ext/A.sub")==LibraryFileSub);
 assert(library_file_kind("/ext/A.ir")==LibraryFileIr);
 assert(library_file_kind("/ext/A.js")==LibraryFileReference);
 assert(library_file_kind("/ext/A.fap")==LibraryFileReference);
 assert(!library_launch_target(LibraryFileReference));
 assert(!strcmp(library_launch_target(LibraryFileRfid),"125 kHz RFID"));
 assert(!strcmp(library_launch_target(LibraryFileIbutton),"iButton"));
 strcpy(c.labels[0],"bad\nlabel");assert(!library_card_valid(&c));c.labels[0][0]=0;
 memset(c.labels[0],'x',sizeof(c.labels[0]));assert(!library_card_valid(&c));c.labels[0][0]=0;
 assert(library_remove_link(&c,0));assert(c.link_count==1&&!strcmp(c.links[0],"/ext/lfrfid/Entry.rfid")&&!strcmp(c.labels[0],"Entry"));
 assert(!c.links[1][0]&&!c.labels[1][0]);assert(!library_remove_link(&c,1));
 LibraryPins pins={0};assert(library_pins_valid(&pins));
 assert(library_pins_toggle(&pins,"Garage")&&library_pins_contains(&pins,"Garage"));
 assert(library_pins_toggle(&pins,"Garage")&&!pins.count);assert(!library_pins_toggle(&pins,"../bad"));
 for(unsigned n=0;n<LIBRARY_PINS;n++){char name[32];snprintf(name,sizeof(name),"Object %u",n);assert(library_pins_toggle(&pins,name));}
 assert(!library_pins_toggle(&pins,"Too many"));assert(library_pins_toggle(&pins,"Object 0"));assert(pins.count==LIBRARY_PINS-1);
 strcpy(pins.names[0],"bad/");assert(!library_pins_valid(&pins));
 return 0;
}
''')

    def test_card_v1_migration_and_checked_pin_rotation(self):
        storage_native(r'''
typedef struct {char name[32],notes[128],tags[64],checked_at[24],links[8][256];uint32_t link_count;} OldCard;
typedef struct {uint32_t magic,generation;OldCard card;uint8_t checksum[32];} OldEnvelope;
static void blob(Storage*s,const char*p,const void*b,size_t n){File*f=storage_file_alloc(s);assert(storage_file_open(f,p,FSAM_WRITE,FSOM_CREATE_NEW));assert(storage_file_write(f,b,n)==n&&storage_file_sync(f)&&storage_file_close(f));storage_file_free(f);}
int main(int argc,char**argv){
 assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);
 assert(storage_simply_mkdir(&s,LIBRARY_ROOT));assert(storage_simply_mkdir(&s,LIBRARY_CARDS));assert(storage_simply_mkdir(&s,LIBRARY_CARDS "/Garage"));
 OldEnvelope old={.magic=0x31434454,.generation=7,.card={.name="Garage",.notes="Original",.link_count=1}};
 strcpy(old.card.links[0],"/ext/nfc/Pass.nfc");assert(!mbedtls_sha256((const uint8_t*)&old,offsetof(OldEnvelope,checksum),old.checksum,0));
 blob(&s,LIBRARY_CARDS "/Garage/0.card",&old,sizeof(old));
 DeviceCard card={0},loaded={0};assert(library_card_load(&s,"Garage",&card));assert(!card.labels[0][0]&&!strcmp(card.notes,"Original"));
 strcpy(card.labels[0],"Pass");write_budget=20;assert(!library_card_save(&s,&card));write_budget=-1;
 assert(library_card_load(&s,"Garage",&loaded)&&!loaded.labels[0][0]);
 assert(library_card_save(&s,&card));assert(library_card_load(&s,"Garage",&loaded)&&!strcmp(loaded.labels[0],"Pass"));
 LibraryPins pins={0},read={0};assert(library_pins_load(&s,&pins)&&!pins.count);
 assert(library_pins_toggle(&pins,"Garage")&&library_pins_save(&s,&pins));
 assert(library_pins_load(&s,&read)&&read.count==1&&!strcmp(read.names[0],"Garage"));
 assert(library_pins_toggle(&pins,"Office"));sync_failure=true;assert(!library_pins_save(&s,&pins));sync_failure=false;
 write_budget=15;assert(!library_pins_save(&s,&pins));write_budget=-1;
 assert(library_pins_load(&s,&read)&&library_pins_contains(&read,"Garage"));
 assert(library_pins_save(&s,&pins));assert(library_pins_load(&s,&read)&&read.count==2);
 sd_failure=true;assert(!library_pins_load(&s,&read));sd_failure=false;
 read_budget=0;assert(!library_pins_save(&s,&pins));read_budget=-1;
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
