"""Bounded field-wallet model, legacy cards, pins and explicit native launch."""
from pathlib import Path
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/device_library"
FIXTURES = ROOT / "tools/tumoflip/fixtures"


def storage_native(program):
    sources = [APP / "library_model.h", APP / "library_model.c", APP / "card_store.h",
               APP / "card_store.c", APP / "pin_store.c",
               ROOT / "lib/fap_metadata/fap_metadata.h", ROOT / "lib/fap_metadata/fap_metadata.c",
               APP / "wallet_launch.c"]
    body = '#include "library_io_stubs.h"\n'
    for source in sources:
        if source.exists():
            body += '#line 1 "' + source.as_posix() + '"\n'
            body += re.sub(r"^#(?:include|pragma).*\n", "\n", source.read_text(), flags=re.M) + "\n"
    body += '#line 1 "wallet-fixture.c"\n' + program
    with tempfile.TemporaryDirectory(prefix="field-wallet-test-") as folder:
        work = Path(folder)
        (work / "sd").mkdir()
        (work / "test.c").write_text(body)
        flags = ["-fprofile-instr-generate", "-fcoverage-mapping"] if os.environ.get("TUMO_WALLET_COVERAGE_DIR") else []
        result = subprocess.run(["cc", "-std=c11", "-D_DEFAULT_SOURCE", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", *flags,
            '-DMBEDTLS_CONFIG_FILE="library_crypto_config.h"', "-I", str(FIXTURES), "-I", str(ROOT / "lib/mbedtls/include"),
            str(work / "test.c"), str(ROOT / "lib/mbedtls/library/sha256.c"),
            str(ROOT / "lib/mbedtls/library/platform_util.c"), "-o", str(work / "test")], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stderr)
        result = subprocess.run([str(work / "test"), str(work / "sd")], capture_output=True, text=True,
                                env=dict(os.environ, LLVM_PROFILE_FILE=str(work / "test.profraw")))
        if result.returncode:
            raise AssertionError(result.stderr)
        if directory := os.environ.get("TUMO_WALLET_COVERAGE_DIR"):
            target = Path(directory); target.mkdir(parents=True, exist_ok=True)
            number = len(list(target.glob("*.profraw")))
            shutil.copy2(work / "test.profraw", target / f"test-{number}.profraw")
            shutil.copy2(work / "test", target / "last-test")


class FieldWalletTests(unittest.TestCase):
    @classmethod
    def tearDownClass(cls):
        if directory := os.environ.get("TUMO_WALLET_COVERAGE_DIR"):
            target = Path(directory)
            subprocess.run(["xcrun", "llvm-profdata", "merge", "-sparse", *map(str, target.glob("*.profraw")),
                            "-o", str(target / "total.profdata")], check=True, capture_output=True)
            report = json.loads(subprocess.check_output(["xcrun", "llvm-cov", "export", str(target / "last-test"),
                               "-instr-profile=" + str(target / "total.profdata")], text=True))
            (target / "coverage.json").write_text(json.dumps(report))
            # Clang maps #line-assembled native fixtures to their physical C file.
            # Select exact production function definitions, not fixture/stub lines.
            production = "\n".join((APP / name).read_text() for name in
                                   ("library_model.c", "card_store.c", "pin_store.c", "wallet_launch.c"))
            functions = [f for f in report["data"][0]["functions"] if re.search(
                r"\b" + re.escape(f["name"].rsplit(":", 1)[-1]) + r"\s*\([^;{}]*\)\s*\{", production)]
            assert len(functions) >= 19, "Missing wallet production coverage mapping"
            regions = [r for f in functions for r in f["regions"] if r[7] == 0]
            measured = set()
            for f in functions:
                start, _, end, *_ = f["regions"][0]
                measured.update(range(start, end + 1))
            lcov = subprocess.check_output(["xcrun", "llvm-cov", "export", str(target / "last-test"),
                                           "-instr-profile=" + str(target / "total.profdata"), "-format=lcov"], text=True)
            lines = []
            physical_fixture = False
            for line in lcov.splitlines():
                if line.startswith("SF:"):
                    physical_fixture = line.endswith("/test.c")
                if physical_fixture and line.startswith("DA:"):
                    number, count = map(int, line[3:].split(",")[:2])
                    if number in measured: lines.append(count)
            assert lines and regions
            summary = {"lines": round(100 * sum(n > 0 for n in lines) / len(lines), 2),
                       "regions": round(100 * sum(r[4] > 0 for r in regions) / len(regions), 2)}
            print("Field wallet production coverage:", summary)
            assert min(summary.values()) >= 80, "Wallet coverage below 80%"

    def test_native_inspect_requires_the_matching_driver_without_executing_it(self):
        storage_native(r'''
bool library_native_inspect_ready(Storage*,LibraryFileKind);
static uint8_t elf[512];
static unsigned api_minor=22;
static void p16(unsigned at,uint16_t n){elf[at]=n;elf[at+1]=n>>8;}
static void p32(unsigned at,uint32_t n){p16(at,n);p16(at+2,n>>16);}
static void app_file(Storage*s,const char*path,const char*name,uint32_t version,unsigned api,unsigned target){
 memset(elf,0,sizeof(elf));memcpy(elf,"\177ELF\1\1\1",7);p16(16,1);p16(18,40);p32(20,1);p32(32,64);p16(40,52);p16(46,40);p16(48,3);p16(50,1);
 p32(108,3);p32(120,256);p32(124,32);p32(144,1);p32(148,1);p32(160,320);p32(164,85);memcpy(elf+256,"\0.fapmeta\0",10);
 p32(320,0x52474448);p32(324,1);p16(328,api_minor);p16(330,api);p16(332,target);p32(336,version);memcpy(elf+340,name,strlen(name)+1);
 storage_common_remove(s,path);File*f=storage_file_alloc(s);assert(storage_file_open(f,path,FSAM_WRITE,FSOM_CREATE_NEW));assert(storage_file_write(f,elf,sizeof(elf))==sizeof(elf));assert(storage_file_close(f));storage_file_free(f);
}
int main(int argc,char**argv){
 assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);
 assert(storage_simply_mkdir(&s,"/ext/apps"));assert(storage_simply_mkdir(&s,"/ext/apps/RFID"));assert(storage_simply_mkdir(&s,"/ext/apps/iButton"));
 assert(!library_native_inspect_ready(&s,LibraryFileReference));assert(library_native_inspect_ready(&s,LibraryFileNfc));
 assert(!library_native_inspect_ready(&s,LibraryFileRfid));
 const char*rf="/ext/apps/RFID/lfrfid.fap";
 app_file(&s,rf,"125 kHz RFID",0x10000,88,7);assert(!library_native_inspect_ready(&s,LibraryFileRfid));
 app_file(&s,rf,"125 kHz RFID",0x10001,88,7);assert(library_native_inspect_ready(&s,LibraryFileRfid));
 api_minor=23;app_file(&s,rf,"125 kHz RFID",0x10001,88,7);assert(!library_native_inspect_ready(&s,LibraryFileRfid));
 api_minor=22;app_file(&s,rf,"125 kHz RFID",0x10001,88,7);
 read_budget=1;assert(!library_native_inspect_ready(&s,LibraryFileRfid));read_budget=-1;
 close_failure=true;assert(!library_native_inspect_ready(&s,LibraryFileRfid));close_failure=false;
 app_file(&s,rf,"Wrong app",0x10001,88,7);assert(!library_native_inspect_ready(&s,LibraryFileRfid));
 app_file(&s,rf,"125 kHz RFID",0x10001,88,8);assert(!library_native_inspect_ready(&s,LibraryFileRfid));
 app_file(&s,rf,"125 kHz RFID",0x10001,87,7);assert(!library_native_inspect_ready(&s,LibraryFileRfid));
 app_file(&s,"/ext/apps/iButton/ibutton.fap","iButton",0x10001,88,7);assert(library_native_inspect_ready(&s,LibraryFileIbutton));
 sd_failure=true;assert(!library_native_inspect_ready(&s,LibraryFileIbutton));sd_failure=false;
 return 0;
}
''')

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
 assert(storage_simply_mkdir(&s,"/ext/apps_data"));assert(storage_simply_mkdir(&s,LIBRARY_ROOT));assert(storage_simply_mkdir(&s,LIBRARY_CARDS));assert(storage_simply_mkdir(&s,LIBRARY_CARDS "/Garage"));
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
