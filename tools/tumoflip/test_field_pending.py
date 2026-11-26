"""Native, offline object-round navigation with synthetic cards and no device."""

from pathlib import Path
import unittest

from tools.tumoflip.test_field_wallet import storage_native
from tools.tumoflip.test_field_menu import PRELUDE, SOURCES

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications/system/device_library"


class FieldPendingTests(unittest.TestCase):
    def test_pending_counts_wrap_and_do_not_change_the_round(self):
        storage_native(r"""
int main(void) {
    LibraryPins pins = {.count=3, .names={"Lobby","Workshop","Store"}};
    FieldRound round;
    assert(field_round_begin(&round,&pins,100));
    unsigned pending=99, selected=99;
    assert(field_round_pending_count(&round,&pending) && pending==3);
    assert(field_round_next_pending(&round,0,&selected) && selected==0);
    assert(field_round_mark(&round,0,FieldRoundPassed,"Checked",101));
    assert(field_round_mark(&round,1,FieldRoundSkipped,"No access",102));
    FieldRound before=round;
    assert(field_round_pending_count(&round,&pending) && pending==1);
    assert(field_round_next_pending(&round,0,&selected) && selected==2);
    assert(field_round_next_pending(&round,2,&selected) && selected==2);
    assert(memcmp(&before,&round,sizeof(round))==0);
    assert(field_round_mark(&round,2,FieldRoundProblem,"Needs service",103));
    assert(field_round_pending_count(&round,&pending) && pending==0);
    selected=99; assert(!field_round_next_pending(&round,0,&selected) && selected==99);
    return 0;
}
""", SOURCES, PRELUDE)

    def test_invalid_rounds_and_indices_are_not_reported_as_zero_pending(self):
        storage_native(r"""
int main(void) {
    LibraryPins pins = {.count=1, .names={"Lobby"}};
    FieldRound round;
    assert(field_round_begin(&round,&pins,100));
    unsigned pending=99, selected=99;
    assert(!field_round_pending_count(NULL,&pending) && pending==99);
    assert(!field_round_pending_count(&round,NULL));
    assert(!field_round_next_pending(&round,0,NULL));
    assert(!field_round_next_pending(&round,1,&selected) && selected==99);
    assert(!field_round_next_pending(&round,UINT32_MAX,&selected) && selected==99);
    round.count=0;
    assert(!field_round_pending_count(&round,&pending) && pending==99);
    round.count=LIBRARY_PINS+1;
    assert(!field_round_pending_count(&round,&pending) && pending==99);
    round.count=1; round.items[0].result=FieldRoundSkipped+1;
    assert(!field_round_pending_count(&round,&pending) && pending==99);
    assert(!field_round_next_pending(&round,0,&selected) && selected==99);
    round.items[0].result=FieldRoundPending; round.items[0].note[0]='x';
    assert(!field_round_pending_count(&round,&pending) && pending==99);
    return 0;
}
""", SOURCES, PRELUDE)

    def test_next_pending_is_gated_to_the_round_page_and_updates_only_selection(self):
        storage_native(r"""
int main(void) {
    LibraryPins pins = {.count=3, .names={"Lobby","Workshop","Store"}};
    FieldRound round;
    assert(field_round_begin(&round,&pins,100));
    FieldMenuState state={.page=FieldPageRound,.round=&round,.selected=round.count};
    Submenu menu;
    field_menu_build(&menu,&state,NULL,NULL);
    assert(!strcmp(menu.header,"Round: 3/3 left"));
    assert(menu.actions[0]==FieldNextPending && !strcmp(menu.rows[0],"Next pending object"));
    assert(field_menu_allows(&state,FieldNextPending));
    FieldRound before=round;
    assert(field_menu_select_next_pending(&state) && state.selected==0);
    assert(field_menu_select_next_pending(&state) && state.selected==1);
    assert(field_menu_select_next_pending(&state) && state.selected==2);
    assert(field_menu_select_next_pending(&state) && state.selected==0);
    assert(memcmp(&before,&round,sizeof(round))==0);
    for(unsigned page=FieldPageHome;page<=FieldPageInput;page++) {
        if(page==FieldPageRound) continue;
        state.page=page;
        assert(!field_menu_allows(&state,FieldNextPending));
        assert(!field_menu_select_next_pending(&state));
    }
    state.page=FieldPageRound;
    for(unsigned i=0;i<round.count;i++)
        assert(field_round_mark(&round,i,FieldRoundSkipped,"Not visited",101+i));
    assert(!field_menu_allows(&state,FieldNextPending));
    assert(!field_menu_select_next_pending(&state));
    assert(!field_menu_select_next_pending(NULL));
    field_menu_build(&menu,&state,NULL,NULL);
    assert(!strcmp(menu.header,"Round: 0/3 left"));
    for(unsigned i=0;i<menu.count;i++) assert(menu.actions[i]!=FieldNextPending);
    round.count=LIBRARY_PINS+1;
    assert(!field_menu_allows(&state,FieldNextPending));
    assert(!field_menu_select_next_pending(&state));
    field_menu_build(&menu,&state,NULL,NULL);
    assert(menu.count==1 && menu.actions[0]==0);
    return 0;
}
""", SOURCES, PRELUDE)

    def test_full_round_is_bounded_and_returns_to_its_first_pending_object(self):
        storage_native(r"""
int main(void) {
    LibraryPins pins={.count=LIBRARY_PINS};
    for(unsigned i=0;i<pins.count;i++) snprintf(pins.names[i],32,"Object_%02u",i);
    FieldRound round;
    assert(field_round_begin(&round,&pins,100));
    for(unsigned i=0;i<round.count;i++)
        if(i!=5) assert(field_round_mark(&round,i,FieldRoundPassed,"Checked",101+i));
    unsigned pending=99, selected=99;
    assert(field_round_pending_count(&round,&pending) && pending==1);
    for(unsigned from=0;from<round.count;from++)
        assert(field_round_next_pending(&round,from,&selected) && selected==5);
    FieldMenuState state={.page=FieldPageRound,.round=&round,.selected=UINT32_MAX};
    assert(field_menu_select_next_pending(&state) && state.selected==5);
    return 0;
}
""", SOURCES, PRELUDE)

    def test_application_wires_navigation_without_changing_the_marking_path(self):
        source = (APP / "field_tools.c").read_text()
        self.assertIn("field_menu_select_next_pending(&app->state)", source)
        self.assertIn("app->state.selected = app->round.count;", source)
        self.assertIn('Field tools 1.2', source)
        manifest = (APP / "application.fam").read_text()
        field = manifest.split('appid="field_tools"', 1)[1].split("\n)", 1)[0]
        self.assertIn('fap_version="1.2"', field)
        self.assertIn('fap_category="Tools"', field)


if __name__ == "__main__":
    unittest.main()
