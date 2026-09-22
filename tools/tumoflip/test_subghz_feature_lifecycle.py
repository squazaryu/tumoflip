"""Execute production host-side unload paths with instrumented plugin callbacks."""
from pathlib import Path
import unittest
import tempfile
from unittest.mock import patch
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class FeatureLifecycleTest(unittest.TestCase):
    def test_updater_must_contain_both_exact_feature_plugins(self):
        from tools.tumoflip.validate_release import validate_subghz_feature_resources, ValidationError
        import hashlib
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            hashes = {}
            for name in ("subghz_frequency_analyzer.fal", "subghz_add_manually.fal", "subghz_workspaces.fal"):
                relative = "apps_data/subghz/plugins/" + name
                file = root / relative
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_bytes(name.encode())
                hashes[relative] = hashlib.sha256(file.read_bytes()).hexdigest()
            with patch("tools.tumoflip.validate_release.resources_archive_hashes", return_value=hashes):
                validate_subghz_feature_resources(ROOT, root, root / "resources.ths")
                hashes.pop("apps_data/subghz/plugins/subghz_frequency_analyzer.fal")
                with self.assertRaises(ValidationError):
                    validate_subghz_feature_resources(ROOT, root, root / "resources.ths")

    def test_analyzer_joins_worker_before_unmapping_and_handles_repeated_exit(self):
        source = (ROOT / "applications/main/subghz/subghz.c").read_text()
        release = function(source, "void subghz_release_frequency_analyzer_view(")
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
enum{SubGhzViewIdWidget,SubGhzViewIdFrequencyAnalyzer};
typedef struct{bool stopped;}Analyzer;
typedef struct{void(*free)(Analyzer*);}Plugin;
typedef struct{Analyzer*subghz_frequency_analyzer;Plugin*analyzer_plugin;void*analyzer_plugin_manager;void*view_dispatcher;}SubGhz;
static int phase;static Analyzer analyzer;static int manager;
static void view_dispatcher_switch_to_view(void*d,int v){(void)d;assert(v==SubGhzViewIdWidget);assert(phase==0);analyzer.stopped=true;phase=1;}
static void view_dispatcher_remove_view(void*d,int v){(void)d;assert(v==SubGhzViewIdFrequencyAnalyzer&&phase==1);phase=2;}
static void release_view(Analyzer*a){assert(a->stopped&&phase==2);phase=3;}
static void subghz_feature_plugin_unload(SubGhz*s,void**m){assert(!s->subghz_frequency_analyzer&&!s->analyzer_plugin);if(*m){assert(phase==3);phase=4;*m=NULL;}}
''' + release + r'''
int main(void){Plugin p={release_view};
 for(int i=0;i<100;i++){phase=0;analyzer.stopped=false;SubGhz s={&analyzer,&p,&manager,NULL};subghz_release_frequency_analyzer_view(&s);assert(phase==4&&!s.analyzer_plugin_manager);subghz_release_frequency_analyzer_view(&s);assert(phase==4);}
 return 0;}
''')

    def test_manual_plugin_cannot_unmap_below_its_own_stack_frame(self):
        source = (ROOT / "applications/main/subghz/helpers/subghz_add_manually_plugin.c").read_text()
        dispatch = function(source, "bool subghz_add_manually_scene_on_event(")
        unload = function(source, "void subghz_add_manually_plugin_unload(")
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef int SceneManagerEvent;typedef unsigned SubGhzAddManuallyScene;
typedef struct SubGhz SubGhz;
typedef struct{bool(*on_event)(SubGhz*,SceneManagerEvent);}Handlers;
typedef struct{Handlers scene[1];}Plugin;
struct SubGhz{Plugin*add_manually_plugin;void*add_manually_plugin_manager;uint8_t add_manually_dispatch_depth;bool add_manually_unload_pending;void*byte_input;};
static bool active;static int releases,cleared;
void subghz_add_manually_plugin_unload(SubGhz*);
static void byte_input_set_header_text(void*b,const char*t){(void)b;assert(!*t);cleared++;}
static void byte_input_set_result_callback(void*b,void*a,void*c,void*d,void*e,int n){(void)b;(void)a;(void)c;(void)d;(void)e;assert(n==0);cleared++;}
static void subghz_feature_plugin_unload(SubGhz*s,void**m){assert(!active&&!s->add_manually_plugin);if(*m){releases++;*m=NULL;}}
static bool subghz_feature_plugin_handle_missing(SubGhz*s,int e){(void)s;return e==99;}
static bool callback(SubGhz*s,int e){assert(e==1);active=true;subghz_add_manually_plugin_unload(s);assert(releases==0&&s->add_manually_plugin&&s->add_manually_unload_pending);active=false;return true;}
''' + unload + dispatch + r'''
int main(void){int manager;Plugin p={{{callback}}};SubGhz s={&p,&manager,0,false,NULL};
 assert(subghz_add_manually_scene_on_event(&s,0,1));
 assert(releases==1&&cleared==2&&!s.add_manually_unload_pending&&!s.add_manually_plugin);
 subghz_add_manually_plugin_unload(&s);assert(releases==1);
 assert(subghz_add_manually_scene_on_event(&s,0,99));return 0;}
''')


if __name__ == "__main__":
    unittest.main()
