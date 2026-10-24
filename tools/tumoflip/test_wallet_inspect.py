"""Actual LF/iButton entry routes: wallet inspection never starts emulation."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class WalletInspectTests(unittest.TestCase):
    def test_real_entries_keep_rpc_and_legacy_launch_but_inspect_safely(self):
        header = ROOT / "lib/toolbox/inspect_file_launch.h"
        helper = header.read_text().replace("#pragma once", "") if header.exists() else ""
        lf = (ROOT / "applications/main/lfrfid/lfrfid.c").read_text()
        ib = (ROOT / "applications/main/ibutton/ibutton.c").read_text()
        native(r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#define FURI_LOG_D(...) ((void)0)
typedef int RpcAppSystem;
typedef struct {char text[300];}FuriString;
typedef struct {FuriString*file_path;void*view_dispatcher,*gui,*scene_manager;RpcAppSystem*rpc_ctx;}LfRfid;
typedef struct {FuriString*file_path;void*view_dispatcher,*gui,*scene_manager;RpcAppSystem*rpc;}iButton;
enum {ViewDispatcherTypeDesktop,ViewDispatcherTypeFullscreen,LfRfidSceneRpc=10,LfRfidSceneEmulate,LfRfidSceneStart,LfRfidSceneSavedKeyMenu,
 iButtonSceneRpc=20,iButtonSceneEmulate,iButtonSceneStart,iButtonSceneSavedKeyMenu,DolphinDeedRfidEmulate=30,DolphinDeedIbuttonEmulate};
static LfRfid l;static iButton b;static FuriString path;static unsigned scene,deeds,loads,stops;
static bool good=true;static const char*expected;
static void reset(void){memset(&path,0,sizeof(path));memset(&l,0,sizeof(l));memset(&b,0,sizeof(b));l.file_path=b.file_path=&path;scene=deeds=loads=stops=0;good=true;}
static LfRfid*lfrfid_alloc(void){return &l;}static iButton*ibutton_alloc(void){return &b;}
static void lfrfid_free(LfRfid*a){(void)a;}static void ibutton_free(iButton*a){(void)a;}
static void lfrfid_make_app_folder(LfRfid*a){(void)a;}static void ibutton_make_app_folder(iButton*a){(void)a;}
static void furi_string_set(FuriString*s,const char*t){assert(strlen(t)<sizeof(s->text));strcpy(s->text,t);}
static bool lfrfid_load_key_data(LfRfid*a,FuriString*p,bool show){(void)a;assert(show);loads++;return good&&!strcmp(p->text,expected);}
static bool ibutton_load_key(iButton*a,bool show){assert(show);loads++;return good&&!strcmp(a->file_path->text,expected);}
static int scan(const char*s,const char*f,void*p){(void)f;if(!strncmp(s,"RPC ",4)){*(uint32_t*)p=0x1234;return 1;}return 0;}
#define sscanf scan
static void rpc_system_app_set_callback(RpcAppSystem*r,void*cb,void*ctx){(void)r;(void)cb;(void)ctx;}
static void rpc_system_app_send_started(RpcAppSystem*r){(void)r;}static void rpc_system_app_send_exited(RpcAppSystem*r){(void)r;}
static void rpc_command_callback(void){}static void ibutton_rpc_command_callback(void){}
static void view_dispatcher_attach_to_gui(void*v,void*g,int type){(void)v;(void)g;(void)type;}
static void scene_manager_next_scene(void*m,unsigned s){(void)m;scene=s;}
static void dolphin_deed(int d){(void)d;deeds++;}
static void view_dispatcher_stop(void*v){(void)v;stops++;}static void view_dispatcher_run(void*v){(void)v;}
''' + helper + "\n" + function(lf, "int32_t lfrfid_app(") + "\n" + function(ib, "int32_t ibutton_app(") + r'''
int main(void){
 reset();expected="/ext/lfrfid/Pass.rfid";lfrfid_app("inspect:/ext/lfrfid/Pass.rfid");assert(scene==LfRfidSceneSavedKeyMenu&&!deeds&&loads==1);
 reset();expected="/ext/ibutton/Key.ibtn";ibutton_app("inspect:/ext/ibutton/Key.ibtn");assert(scene==iButtonSceneSavedKeyMenu&&!deeds&&loads==1);
 reset();expected="/ext/lfrfid/Pass.rfid";lfrfid_app((void*)expected);assert(scene==LfRfidSceneEmulate&&deeds==1);
 reset();expected="/ext/ibutton/Key.ibtn";ibutton_app((void*)expected);assert(scene==iButtonSceneEmulate&&deeds==1);
 const char*bad[]={"inspect:","inspect:/int/key","inspect:/ext/../key","inspect:/ext/a//b","inspect:/ext/a\\b","inspect:/ext/a\nb"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
  reset();expected="impossible";lfrfid_app((void*)bad[i]);assert(!loads&&!deeds&&scene!=LfRfidSceneEmulate);
  reset();expected="impossible";ibutton_app((void*)bad[i]);assert(!loads&&!deeds&&scene!=iButtonSceneEmulate);
 }
 reset();lfrfid_app(NULL);assert(scene==LfRfidSceneStart&&!deeds&&!loads);
 reset();ibutton_app(NULL);assert(scene==iButtonSceneStart&&!deeds&&!loads);
 reset();lfrfid_app("RPC 1234");assert(scene==LfRfidSceneRpc&&!loads);
 reset();ibutton_app("RPC 1234");assert(scene==iButtonSceneRpc&&!loads);
 reset();expected="/ext/a.rfid";good=false;lfrfid_app("inspect:/ext/a.rfid");assert(!deeds&&scene!=LfRfidSceneEmulate);
 reset();expected="/ext/a.ibtn";good=false;ibutton_app("inspect:/ext/a.ibtn");assert(!deeds&&scene!=iButtonSceneEmulate);
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
