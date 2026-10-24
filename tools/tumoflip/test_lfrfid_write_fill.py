"""Compare actual before/after callbacks with identical pre-encoded bit buffers.

Only encoding (unchanged by this refactor) is stubbed. Actual writer callback
branches, configuration words, bit access/reversal and request layouts execute.
"""
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from tools.tumoflip.test_hotplug_assets import function

ROOT = Path(__file__).resolve().parents[2]


def source(path):
    return "\n".join(line for line in (ROOT / path).read_text().splitlines()
                     if not line.startswith(("#include", "#pragma once")))


class LfRfidWriteFillTests(unittest.TestCase):
    def test_shared_implementation_is_internal_and_preserves_special_cases(self):
        self.assertTrue((ROOT / "lib/lfrfid/protocols/lfrfid_write_fill.c").exists())
        api = (ROOT / "targets/f7/api_symbols.csv").read_text()
        self.assertNotIn("Function,+,lfrfid_write_fill_", api)
        for name in ("protocol_em4100", "protocol_electra", "protocol_h10301"):
            self.assertNotIn('"lfrfid_write_fill.h"', (ROOT / f"lib/lfrfid/protocols/{name}.c").read_text())

    def test_twenty_production_callbacks_match_frozen_legacy_requests(self):
        fixture = json.loads((ROOT / "tools/tumoflip/fixtures/lfrfid_write_fill_legacy.json").read_text())
        definitions, callbacks = [], []
        for index, (path, legacy) in enumerate(fixture["callbacks"].items()):
            match = re.search(r"bool (\w+)\((\w+)\* (\w+),", legacy)
            name, typename, instance = match.groups()
            current = function((ROOT / path).read_text(), "bool " + name + "(")
            fields = sorted(set(re.findall(r"\b" + instance + r"->(\w+)", legacy + current)))
            definitions.append("typedef struct {" + "".join(f"uint8_t {field}[64];" for field in fields) + "} " + typename + ";")
            calls = set(re.findall(r"\b(protocol_\w+)\(", legacy + current)) - {name}
            definitions.extend(f"#define {call}(...) ((void)0)" for call in sorted(calls))
            definitions.append(legacy.replace(name, "legacy_" + name, 1))
            definitions.append(current)
            callbacks.append(f"""
            for(unsigned sample=0;sample<256;sample++) {{
                {typename} proto; random_bytes(&proto,sizeof(proto));
                {'proto.data[0]=0;proto.data[1]=0;' if 'data' in fields else ''}
                if(sample&1) {{ {'proto.data[0]=1;' if 'data' in fields else ''} }}
                for(unsigned type=0;type<LFRFIDWriteTypeMax;type++) {{
                    struct {{uint64_t before;LFRFIDWriteRequest request;uint64_t after;}} old,new;
                    memset(&old,0xA5,sizeof(old));memcpy(&new,&old,sizeof(old));
                    old.request.write_type=new.request.write_type=type;
                    {typename} old_proto=proto,new_proto=proto;
                    bool a=legacy_{name}(&old_proto,&old.request),b={name}(&new_proto,&new.request);
                    assert(a==b);assert(!memcmp(&old,&new,sizeof(old)));
                    assert(!memcmp(&old_proto,&new_proto,sizeof(old_proto)));
                    assert(old.before==UINT64_C(0xA5A5A5A5A5A5A5A5));
                    assert(new.after==UINT64_C(0xA5A5A5A5A5A5A5A5));
                }}
            }}
            """)
        body = r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define furi_check assert
#define furi_assert assert
#define furi_crash(...) abort()
#define COUNT_OF(x) (sizeof(x)/sizeof((x)[0]))
typedef int ProtocolBase;
typedef struct {uint64_t opaque[8];} LFRFIDHitagMicro,LFRFIDHitagS;
'''
        for path in ("lib/bit_lib/bit_lib.h", "lib/bit_lib/bit_lib.c", "lib/lfrfid/tools/t5577.h",
                     "lib/lfrfid/tools/em4305.h", "lib/lfrfid/protocols/lfrfid_protocols.h"):
            body += source(path) + "\n"
        if (ROOT / "lib/lfrfid/protocols/lfrfid_write_fill.c").exists():
            body += source("lib/lfrfid/protocols/lfrfid_write_fill.h") + "\n"
            body += source("lib/lfrfid/protocols/lfrfid_write_fill.c") + "\n"
        body += "\n".join(definitions) + r'''
static uint32_t random_state=0x13572468;
static void random_bytes(void* target,size_t size){uint8_t* p=target;while(size--){
 random_state=random_state*1664525u+1013904223u;*p++=random_state>>24;}}
int main(void){
'''+"\n".join(callbacks)+"\nreturn 0;}\n"
        with tempfile.TemporaryDirectory(prefix="rfid-writer-equivalence-") as directory:
            work = Path(directory)
            (work / "test.c").write_text(body)
            compiled = subprocess.run(["cc", "-std=c11", "-O1", "-g", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                                       str(work / "test.c"), "-o", str(work / "test")],
                                      capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            ran = subprocess.run([str(work / "test")], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
