"""Keep the public plugin paths while removing repeated resident filename text."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class ProtocolPackFilenameTests(unittest.TestCase):
    def test_compact_entries_expand_to_exact_paths_without_truncation(self):
        source = (ROOT / "lib/subghz/protocols/plugin_registry.c").read_text()
        table = source[source.index("static const SubGhzProtocolPackEntry subghz_protocol_pack_entries[]") : source.index("\n};", source.index("static const SubGhzProtocolPackEntry subghz_protocol_pack_entries[]")) + 3]
        helper = "static bool subghz_protocol_pack_filename("
        body = function(source, helper) if helper in source else ""
        body += r'''
int main(void){
 unsigned count=sizeof(subghz_protocol_pack_entries)/sizeof(*subghz_protocol_pack_entries);
 for(unsigned i=0;i<count;i++){
   const char* stem=subghz_protocol_pack_entries[i].filename;
   assert(!strstr(stem,"protocol_")&&!strstr(stem,".fal"));
 }
''' + (r'''
 char path[48];assert(subghz_protocol_pack_filename("renault_v1",path,sizeof(path))&&!strcmp(path,"protocol_renault_v1.fal"));
 assert(!subghz_protocol_pack_filename("renault_v1",path,12));
 assert(!subghz_protocol_pack_filename("../evil",path,sizeof(path)));
 assert(!subghz_protocol_pack_filename("",path,sizeof(path)));
''' if helper in source else "") + "return 0;}"
        names = re.findall(r"SubGhzProtocolPackGroup\w+", table)
        enums = list(dict.fromkeys(names + ["SubGhzProtocolPackGroupCount"]))
        native("#include <assert.h>\n#include <stdio.h>\n#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#include <string.h>\nenum {" + ",".join(enums) + "};\n" +
               "typedef struct {uint32_t groups;const char*filename;}SubGhzProtocolPackEntry;\n#define PACK_GROUP(g) (1UL<<(g))\n#define PACK_GROUP_ALL 0xffffffffUL\n" + table + "\n" + body)


if __name__ == "__main__":
    unittest.main()
