"""Native menu construction and stale-action gates; no simulated pass result."""
import unittest
from tools.tumoflip.test_field_wallet import storage_native

SOURCES = ("field_io.h", "field_io.c", "field_round.h", "field_round.c", "field_menu.h", "field_menu.c")
PRELUDE = r'''
typedef struct {unsigned count;char header[32],rows[40][64];uint32_t actions[40];} Submenu;
static void submenu_reset(Submenu*m){memset(m,0,sizeof(*m));}
static void submenu_set_header(Submenu*m,const char*h){assert(strlen(h)<sizeof(m->header));strcpy(m->header,h);}
static void submenu_add_item(Submenu*m,const char*l,uint32_t id,void(*cb)(void*,uint32_t),void*c){(void)cb;(void)c;assert(m->count<40);snprintf(m->rows[m->count],64,"%s",l);m->actions[m->count++]=id;}
'''


class FieldMenuTests(unittest.TestCase):
    def test_all_pages_keep_their_existing_actions_and_empty_states(self):
        storage_native(r'''
int main(void){
 Submenu menu;FieldRound round={.started=1,.count=1,.items={{.object="Office"}}};
 DeviceCard card={.name="Office"};char names[32][32];
 for(unsigned i=0;i<32;i++)snprintf(names[i],32,"Object %02u",i+1);
 FieldMenuState state={.page=FieldPageNewConfirm,.round=&round,.card=&card,
                      .names=(const char(*)[32])names};
 field_menu_build(&menu,&state,NULL,NULL);assert(menu.count==3);
 assert(menu.actions[1]==FieldConfirmNew);
 state.page=FieldPageBundle;field_menu_build(&menu,&state,NULL,NULL);
 assert(menu.count==3&&!strcmp(menu.rows[0],"Office")&&menu.actions[1]==FieldImport);
 state.card=NULL;field_menu_build(&menu,&state,NULL,NULL);assert(menu.count==2);
 state.page=FieldPageCards;field_menu_build(&menu,&state,NULL,NULL);
 assert(menu.count==1&&!strcmp(menu.rows[0],"No objects"));
 state.count=32;state.more=true;state.offset=32;
 field_menu_build(&menu,&state,NULL,NULL);assert(menu.count==34);
 assert(menu.actions[0]==FieldCardBase&&menu.actions[31]==FieldCardBase+31);
 assert(menu.actions[32]==FieldPreviousCards&&menu.actions[33]==FieldNextCards);
 state.count=33;state.more=false;state.offset=0;
 field_menu_build(&menu,&state,NULL,NULL);assert(menu.count==0);
 state.page=FieldPageItem;state.selected=1;
 field_menu_build(&menu,&state,NULL,NULL);assert(menu.count==1);
 assert(!strcmp(menu.rows[0],"No selected object")&&!field_menu_allows(&state,FieldNote));
 state.selected=0;field_menu_build(&menu,&state,NULL,NULL);assert(menu.count==7);
 state.page=FieldPageBusy;field_menu_build(&menu,&state,NULL,NULL);assert(menu.count==2);
 state.page=FieldPageMessage;field_menu_build(&menu,&state,NULL,NULL);assert(menu.count==0);
 assert(!field_menu_allows(NULL,FieldStartRound)&&!field_menu_allows(&state,0));
 return 0;}
''', SOURCES, PRELUDE)

    def test_menu_is_native_and_actions_are_bound_to_current_page(self):
        storage_native(r'''
int main(void){
 Submenu menu;FieldRound round={.started=1,.count=1,.items={{.object="Office"}}};
 FieldMenuState state={.page=FieldPageHome,.round=&round};
 field_menu_build(&menu,&state,NULL,NULL);assert(menu.count==8&&!strcmp(menu.header,"Field tools"));
 assert(field_menu_allows(&state,FieldStartRound));assert(!field_menu_allows(&state,FieldPass));
 state.page=FieldPageRound;field_menu_build(&menu,&state,NULL,NULL);
 assert(field_menu_allows(&state,FieldRoundBase));assert(!field_menu_allows(&state,FieldRoundBase+1));
 assert(!field_menu_allows(&state,FieldImport));assert(menu.actions[0]==FieldNextPending);
 assert(strstr(menu.rows[1],"PENDING"));
 state.page=FieldPageItem;state.selected=0;field_menu_build(&menu,&state,NULL,NULL);
 assert(field_menu_allows(&state,FieldPass)&&field_menu_allows(&state,FieldProblem)&&field_menu_allows(&state,FieldSkip));
 state.selected=1;assert(!field_menu_allows(&state,FieldPass));
 state.page=FieldPageBusy;assert(!field_menu_allows(&state,FieldStartRound)&&!field_menu_allows(&state,FieldPass));
 state.page=FieldPageNewConfirm;assert(field_menu_allows(&state,FieldConfirmNew));
 state.page=FieldPageBundle;assert(field_menu_allows(&state,FieldImport));
 state.page=FieldPageCards;state.count=32;state.more=true;
 assert(field_menu_allows(&state,FieldCardBase+31)&&!field_menu_allows(&state,FieldCardBase+32));
 assert(field_menu_allows(&state,FieldNextCards)&&!field_menu_allows(&state,FieldPreviousCards));
 return 0;
}
''', SOURCES, PRELUDE)


if __name__ == "__main__":
    unittest.main()
