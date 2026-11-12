"""Real application manifest routing; never hide field tools in core flash."""
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/device_library"


class FieldToolsDeliveryTests(unittest.TestCase):
    def test_native_catalog_can_deliver_rx_leaves_and_matching_status_without_rebasing(self):
        from tools.tumoflip import validate_release as release
        paths = [f"apps_data/subghz/plugins/protocol_rx_{stem}.fal" for stem in
                 ("x10", "urmet", "gm0", "gm1", "ty0", "mz2", "mb0", "mb1", "op1", "bmw")]
        paths.append("apps_data/arf_subghz_full/modules/arf_status.fap")
        exports = release.package_extapp_exports()
        for path in paths:
            self.assertEqual(exports.get(Path(path).name), path)
            self.assertIn(path, release.PACKAGE_RELEASE_OVERLAY_FILES)
            self.assertEqual(release.PACKAGE_RELEASE_OVERLAY_GROUPS[path],
                             "arf" if path.endswith("arf_status.fap") else "protocol_packs")
            self.assertNotIn(path, release.PACKAGE_ONLY_PACKAGE_FILES)

    def test_round_object_route_requires_device_library_12_without_loading_it(self):
        from tools.tumoflip.test_field_wallet import storage_native
        storage_native(r'''
static uint8_t elf[512];
static void p16(unsigned a,uint16_t v){elf[a]=v;elf[a+1]=v>>8;}
static void p32(unsigned a,uint32_t v){p16(a,v);p16(a+2,v>>16);}
static void app_file(Storage*s,uint32_t version,const char*name){
 memset(elf,0,sizeof(elf));memcpy(elf,"\177ELF\1\1\1",7);p16(16,1);p16(18,40);p32(20,1);p32(32,64);p16(40,52);p16(46,40);p16(48,3);p16(50,1);
 p32(108,3);p32(120,256);p32(124,32);p32(144,1);p32(148,1);p32(160,320);p32(164,85);memcpy(elf+256,"\0.fapmeta\0",10);
 p32(320,0x52474448);p32(324,1);p16(328,22);p16(330,88);p16(332,7);p32(336,version);memcpy(elf+340,name,strlen(name)+1);
 const char*path="/ext/apps/Tools/device_library.fap";storage_common_remove(s,path);File*f=storage_file_alloc(s);assert(storage_file_open(f,path,FSAM_WRITE,FSOM_CREATE_NEW));assert(storage_file_write(f,elf,sizeof(elf))==sizeof(elf));assert(storage_file_close(f));storage_file_free(f);
}
int main(int argc,char**argv){assert(argc==2);Storage s={0};snprintf(s.root,sizeof(s.root),"%s",argv[1]);assert(storage_simply_mkdir(&s,"/ext/apps"));assert(storage_simply_mkdir(&s,"/ext/apps/Tools"));
 assert(!library_object_entry_ready(&s));app_file(&s,0x10001,"Device Library");assert(!library_object_entry_ready(&s));
 app_file(&s,0x10002,"Device Library");assert(library_object_entry_ready(&s));
 app_file(&s,0x10002,"Other FAP");assert(!library_object_entry_ready(&s));app_file(&s,0x10002,"Device Library");
 close_failure=true;assert(!library_object_entry_ready(&s));close_failure=false;read_budget=1;assert(!library_object_entry_ready(&s));return 0;}
''')

    def test_new_faps_reach_the_actual_base_package_manifest(self):
        from tools.tumoflip.test_package_release import prepare_package_tree
        from tools.tumoflip.validate_release import package_entries
        with tempfile.TemporaryDirectory(prefix="field-package-contract-") as directory:
            _, _, resources = prepare_package_tree(Path(directory))
            paths = {entry["source"] for entry in package_entries(resources)["base"]}
            self.assertIn("apps/Tools/field_tools.fap", paths)
            self.assertIn("apps/NFC/tumo_nfc_sniffer.fap", paths)

    def test_new_apps_are_base_packages_not_updater_payloads(self):
        from tools.tumoflip import validate_release as release
        expected = {"field_tools.fap": "apps/Tools/field_tools.fap",
                    "tumo_nfc_sniffer.fap": "apps/NFC/tumo_nfc_sniffer.fap"}
        for filename, path in expected.items():
            self.assertEqual(release.package_extapp_exports().get(filename), path)
            self.assertIn(path, release.PACKAGE_ONLY_PACKAGE_FILES)
            self.assertEqual(release.PACKAGE_ONLY_PACKAGE_GROUPS[path], "base")
        for stem in ("rx_x10", "rx_urmet", "rx_gm0", "rx_gm1", "rx_ty0", "rx_mz2", "rx_mb0", "rx_mb1", "rx_op1", "rx_bmw"):
            self.assertIn("protocol_" + stem + ".fal", release.PROTOCOL_PACKS)

    def test_field_tools_is_a_buildable_package_only_application(self):
        apps = []
        exec(compile((APP / "application.fam").read_text(), "application.fam", "exec"), {
            "App": lambda **values: apps.append(values),
            "FlipperAppType": SimpleNamespace(EXTERNAL=1, PLUGIN=2),
        })
        selected = [a for a in apps if a["appid"] == "field_tools"]
        self.assertEqual(len(selected), 1, "Field tools must be a package-only, buildable FAP")
        app = selected[0]
        self.assertTrue(app["fap_package_only"])
        for source in app["sources"]:
            self.assertTrue((APP / source).is_file(), source)
        self.assertIn("field_inventory.c", app["sources"])
        self.assertIn("field_bundle.c", app["sources"])
        self.assertIn("field_round.c", app["sources"])


if __name__ == "__main__":
    unittest.main()
