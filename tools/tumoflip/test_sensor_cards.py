"""Bounded decoded-sensor identity/history; not guessed protocol or RF acceptance."""
from pathlib import Path
import unittest
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / 'applications/system/device_library'


def production():
    path = APP / 'sensor_cards.c'
    return ('#include <assert.h>\n#include <string.h>\n'
            + f'#include "{APP / "sensor_cards.h"}"\n'
            + (path.read_text().replace('#include "sensor_cards.h"', '') if path.exists() else ''))


class SensorCardTests(unittest.TestCase):
    def test_owned_package_entry_points_and_object_namespace(self):
        app = (APP / 'sensor_app.c').read_text()
        self.assertIn('library_name_valid(argument + 7)', app)
        self.assertIn('"obj_"', app)
        self.assertIn('field_record_save', app)
        self.assertIn('field_record_load', app)
        self.assertIn('field_report_save', app)
        self.assertIn('Sensor Watch', (APP / 'device_library.c').read_text())
        spectrum = (ROOT / 'applications_user/signal_workbench/signal_workbench.c').read_text()
        self.assertIn('"Sensor Watch"', spectrum)
        self.assertIn('/ext/apps/Sub-GHz/sensor_watch.fap', spectrum)

    def test_decoders_are_shared_without_weather_ui_or_tx_and_have_explicit_lease(self):
        path = APP / 'sensor_runtime.c'
        self.assertTrue(path.exists(), 'missing sensor receiver integration')
        runtime = path.read_text()
        for token in ('subghz_radio_broker_acquire', 'subghz_radio_broker_release',
                      'subghz_devices_stop_async_rx', 'subghz_receiver_reset',
                      'subghz_protocol_decoder_base_serialize', 'weather_station_protocol_registry'):
            self.assertIn(token, runtime)
        self.assertNotIn('start_async_tx', runtime)
        self.assertNotIn('WSBlockGeneric*', runtime)
        registry = ROOT / 'applications_user/weather_editor/protocols/protocol_items.h'
        self.assertNotIn('weather_station_app_i.h', registry.read_text())
        self.assertTrue((APP / 'sensor_app.c').exists())

    def test_identity_duplicate_frames_and_ring_history(self):
        native(production() + r'''
int main(void){SensorCards cards;sensor_cards_init(&cards);assert(sensor_cards_valid(&cards));
 SensorPoint p={.timestamp=100,.temperature=-198,.humidity=56,.fields=7};
 assert(sensor_cards_add(&cards,"Nexus-TH",1,2,433920000,&p));
 assert(sensor_cards_add(&cards,"Nexus-TH",1,2,433920000,&p));assert(cards.count==1&&cards.cards[0].count==1&&cards.cards[0].frames==2);
 for(unsigned i=1;i<100;i++){p.timestamp++;p.temperature++;assert(sensor_cards_add(&cards,"Nexus-TH",1,2,433920000,&p));}
 assert(cards.cards[0].count==32&&sensor_card_point(&cards.cards[0],0)->timestamp==168);
 assert(!sensor_card_point(&cards.cards[0],32));
 assert(sensor_cards_add(&cards,"Nexus-TH",1,3,433920000,&p));
 assert(sensor_cards_add(&cards,"Nexus-TH",1,2,868350000,&p));
 assert(sensor_cards_add(&cards,"Other",1,2,433920000,&p));assert(cards.count==4);
 for(unsigned i=2;i<20;i++)sensor_cards_add(&cards,"Nexus-TH",i,2,433920000,&p);
 assert(cards.count==8&&cards.full==14&&sensor_cards_valid(&cards));return 0;}
''')

    def test_invalid_fields_ambiguous_id_and_csv_text_are_rejected(self):
        native(production() + r'''
int main(void){SensorCards c;sensor_cards_init(&c);SensorPoint p={.temperature=250,.fields=1};
 assert(!sensor_cards_add(&c,"Nexus-TH",UINT32_MAX,2,433920000,&p));
 assert(!sensor_cards_add(&c,"=formula",1,2,433920000,&p));
 assert(!sensor_cards_add(&c,"bad,name",1,2,433920000,&p));
 p.humidity=101;p.fields=3;assert(!sensor_cards_add(&c,"Nexus-TH",1,2,433920000,&p));
 p.fields=1;p.temperature=2000;assert(!sensor_cards_add(&c,"Nexus-TH",1,2,433920000,&p));
 p.temperature=-198;assert(sensor_cards_add(&c,"Nexus-TH",1,255,433920000,&p));
 char row[256];assert(sensor_card_row(&c.cards[0],0,row,sizeof(row))&&strstr(row,"Nexus-TH,1,255,433920000,0,-198,,,1"));
 assert(!sensor_card_row(&c.cards[0],0,row,10));assert(!sensor_card_row(&c.cards[0],32,row,sizeof(row)));
 c.cards[0].head=32;assert(!sensor_cards_valid(&c));return 0;}
''')


if __name__ == '__main__': unittest.main()
