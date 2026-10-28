"""Execute the bounded UCM601 inventory contract without any UART/RF device."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / 'applications/system/device_library'


def production():
    path = APP / 'uhf_inventory.c'
    return ('#include <assert.h>\n#include <string.h>\n'
            + f'#include "{APP / "uhf_inventory.h"}"\n'
            + (path.read_text().replace('#include "uhf_inventory.h"', '') if path.exists() else '')
            + r'''
static bool frame(UhfInventory*h,uint8_t cmd,const uint8_t*p,unsigned n,unsigned ts){
 uint8_t b[257]={0xA0,(uint8_t)(n+3),0,cmd};memcpy(b+4,p,n);uint8_t sum=0;
 for(unsigned i=0;i<n+4;i++)sum+=b[i];b[n+4]=(uint8_t)-sum;bool changed=false;
 for(unsigned i=0;i<n+5;i++)changed=uhf_inventory_feed(h,b[i],ts)||changed;return changed;}
''')


class UhfInventoryTests(unittest.TestCase):
    def test_execute_uart_failure_cleanup_and_session_timeout(self):
        path = APP / 'uhf_runtime.c'
        self.assertTrue(path.exists())
        header = re.sub(r'^#.*\n', '\n', (APP / 'uhf_runtime.h').read_text(), flags=re.M)
        runtime = re.sub(r'^#include.*\n', '\n', path.read_text(), flags=re.M)
        native(production() + r'''
typedef struct {bool initialized;} FuriHalSerialHandle;
typedef struct {uint8_t data[8192];unsigned used;} FuriStreamBuffer;
typedef struct {bool connected;} Expansion;
typedef unsigned FuriHalSerialRxEvent;
enum {FuriHalSerialRxEventData=1,FuriHalSerialRxEventFrameError=2,FuriHalSerialRxEventNoiseError=4,
 FuriHalSerialRxEventOverrunError=8,FuriHalSerialRxEventParityError=16,FuriHalSerialIdUsart};
#define RECORD_EXPANSION "expansion"
#define FURI_CRITICAL_ENTER() do{}while(0)
#define FURI_CRITICAL_EXIT() do{}while(0)
static FuriHalSerialHandle port;static Expansion expansion;static FuriStreamBuffer buffer;
static unsigned ticks,disables,enables,releases,stops,frees,closes,last_command,tx_count;
static bool busy,allocation_failure;static uint8_t incoming;
static void* furi_record_open(const char*s){(void)s;return &expansion;}
static void furi_record_close(const char*s){(void)s;closes++;}
static bool expansion_is_connected(Expansion*e){return e->connected;}
static void expansion_disable(Expansion*e){(void)e;disables++;}
static void expansion_enable(Expansion*e){(void)e;enables++;}
static FuriHalSerialHandle*furi_hal_serial_control_acquire(unsigned id){(void)id;return busy?NULL:&port;}
static void furi_hal_serial_control_release(FuriHalSerialHandle*p){assert(!p->initialized);releases++;}
static void furi_hal_serial_init(FuriHalSerialHandle*p,unsigned baud){assert(baud==115200&&!p->initialized);p->initialized=true;}
static void furi_hal_serial_deinit(FuriHalSerialHandle*p){assert(p->initialized);p->initialized=false;}
static void furi_hal_serial_async_rx_stop(FuriHalSerialHandle*p){assert(p->initialized);stops++;}
static void furi_hal_serial_tx(FuriHalSerialHandle*p,const uint8_t*b,size_t n){assert(p->initialized&&n>=5);last_command=b[3];tx_count++;}
static void furi_hal_serial_tx_wait_complete(FuriHalSerialHandle*p){assert(p->initialized);}
static uint8_t furi_hal_serial_async_rx(FuriHalSerialHandle*p){assert(p->initialized);return incoming;}
static void furi_hal_serial_async_rx_start(FuriHalSerialHandle*p,void(*cb)(FuriHalSerialHandle*,FuriHalSerialRxEvent,void*),void*c,bool errors){assert(p->initialized&&cb&&c&&errors);}
static FuriStreamBuffer*furi_stream_buffer_alloc(size_t n,size_t t){assert(n==4096&&t==1);buffer.used=0;return allocation_failure?NULL:&buffer;}
static void furi_stream_buffer_free(FuriStreamBuffer*b){assert(b==&buffer);frees++;}
static size_t furi_stream_buffer_send(FuriStreamBuffer*b,const void*p,size_t n,unsigned timeout){(void)timeout;if(n>sizeof(b->data)-b->used)return 0;memcpy(b->data+b->used,p,n);b->used+=n;return n;}
static size_t furi_stream_buffer_receive(FuriStreamBuffer*b,void*p,size_t n,unsigned timeout){(void)timeout;if(n>b->used)n=b->used;memcpy(p,b->data,n);b->used-=n;memmove(b->data,b->data+n,b->used);return n;}
static void furi_stream_buffer_reset(FuriStreamBuffer*b){b->used=0;}
static uint32_t furi_get_tick(void){return ticks;}
static uint32_t furi_hal_rtc_get_timestamp(void){return ticks/1000;}
''' + header + runtime + r'''
int main(void){(void)frame;UhfRuntime r={0};UhfInventory h;uhf_inventory_init(&h);
 expansion.connected=true;assert(!uhf_runtime_connect(&r,&h)&&!disables&&closes==1);expansion.connected=false;
 busy=true;assert(!uhf_runtime_connect(&r,&h)&&enables==1&&!releases&&!port.initialized);busy=false;
 allocation_failure=true;assert(!uhf_runtime_connect(&r,&h)&&releases==1&&!port.initialized);allocation_failure=false;
 assert(uhf_runtime_connect(&r,&h)&&last_command==0x72&&!r.running);assert(!uhf_runtime_start(&r));
 uint8_t ver[]={0xA0,6,0,0x72,1,2,3,0xE2}; // additive checksum
 uint8_t sum=0;for(unsigned i=0;i<7;i++)sum+=ver[i];ver[7]=(uint8_t)-sum;
 furi_stream_buffer_send(&buffer,ver,sizeof(ver),0);uhf_runtime_tick(&r);
 assert(h.identified&&uhf_runtime_start(&r)&&last_command==0x89);
 ticks=60000;uhf_runtime_tick(&r);assert(!r.running&&r.timed_out&&last_command==0x8C);
 uhf_runtime_disconnect(&r);assert(!r.serial&&!r.stream&&!r.expansion&&!h.identified&&releases==2&&stops==1);
 assert(uhf_runtime_connect(&r,&h));h.identified=true;r.last_rx=ticks;assert(uhf_runtime_start(&r));
 incoming=0xA0;uhf_runtime_rx(&port,FuriHalSerialRxEventData,&r);assert(buffer.used==1);
 uhf_runtime_rx(&port,FuriHalSerialRxEventFrameError,&r);assert(r.overflow);
 uhf_runtime_tick(&r);assert(!r.running&&h.errors&&last_command==0x8C);
 buffer.used=sizeof(buffer.data);uhf_runtime_rx(&port,FuriHalSerialRxEventData,&r);assert(r.overflow);
 uhf_runtime_tick(&r);assert(!buffer.used);
 uhf_runtime_disconnect(&r);return 0;}
''')

    def test_uart_ownership_explicit_start_timeout_and_release(self):
        runtime = APP / 'uhf_runtime.c'
        self.assertTrue(runtime.exists(), 'missing UHF UART lifecycle implementation')
        source = runtime.read_text()
        self.assertIn('expansion_is_connected', source)
        self.assertIn('furi_hal_serial_control_acquire', source)
        self.assertIn('furi_hal_serial_async_rx_stop', source)
        self.assertIn('furi_hal_serial_control_release', source)
        self.assertNotIn('otg_enable', source)
        self.assertNotIn('set_power', source)
        self.assertIn('60000U', source)
        app = APP / 'uhf_app.c'
        self.assertTrue(app.exists(), 'missing explicit UHF inventory UI')
        self.assertIn('field_report_save', app.read_text())
        self.assertIn('RF interrogation', app.read_text())

    def test_version_checksum_address_and_explicit_command_allowlist(self):
        native(production() + r'''
int main(void){UhfInventory h;uhf_inventory_init(&h);uint8_t b[6];
 assert(uhf_inventory_command(0x72,b)==5&&!memcmp(b,(uint8_t[]){0xA0,3,0,0x72,0xEB},5));
 assert(uhf_inventory_command(0x89,b)==6&&!memcmp(b,(uint8_t[]){0xA0,4,0,0x89,1,0xD2},6));
 assert(uhf_inventory_command(0x8C,b)==5);
 for(unsigned i=0;i<256;i++)if(i!=0x72&&i!=0x89&&i!=0x8C)assert(!uhf_inventory_command(i,b));
 uint8_t ver[]={1,2,3};assert(frame(&h,0x72,ver,3,0)&&h.identified&&h.version[2]==3);
 uhf_inventory_init(&h);uint8_t bad[]={0xA0,6,0,0x72,1,2,3,0};
 for(unsigned i=0;i<sizeof(bad);i++)uhf_inventory_feed(&h,bad[i],0);
 assert(!h.identified&&h.errors);assert(frame(&h,0x72,ver,3,0));
 uhf_inventory_discontinuity(&h);assert(h.used==0);return 0;}
''')

    def test_exact_pc_length_metadata_dedup_bounded_table_and_short_status(self):
        native(production() + r'''
int main(void){UhfInventory h;uhf_inventory_init(&h);
 uint8_t p[]={1,0x30,0,0xE2,0,1,2,3,4,5,6,7,8,9,10,0x01,0x23,0x45,0x67,0x0D,0x3B,0x50};
 assert(frame(&h,0x89,p,sizeof(p),10)&&h.count==1&&h.tags[0].length==12);
 assert(h.tags[0].signal_raw==0x01234567&&h.tags[0].frequency_raw==0x0D3B50);
 assert(frame(&h,0x89,p,sizeof(p),20)&&h.count==1&&h.tags[0].seen==2&&h.tags[0].first==10&&h.tags[0].last==20);
 char hex[97];assert(uhf_inventory_epc(&h.tags[0],hex,sizeof(hex))&&!strcmp(hex,"E2000102030405060708090A"));
 assert(!uhf_inventory_epc(&h.tags[0],hex,24));
 unsigned count=h.count;p[1]=0xF8;assert(!frame(&h,0x89,p,sizeof(p),30)&&h.count==count);p[1]=0x30;
 assert(!frame(&h,0x89,p,sizeof(p)-1,30));
 for(unsigned i=1;i<40;i++){p[4]=i;frame(&h,0x89,p,sizeof(p),40+i);}assert(h.count==32&&h.full==8);
 uint8_t end[]={0x10};assert(!frame(&h,0x89,end,1,100)&&h.completed==1&&h.count==32);
 assert(!frame(&h,0x94,p,sizeof(p),100));return 0;}
''')

    def test_random_stream_resynchronization_and_buffer_variant(self):
        native(production() + r'''
int main(void){UhfInventory h;uhf_inventory_init(&h);uint32_t r=1;
 for(unsigned i=0;i<200000;i++){r=r*1664525+1013904223;uhf_inventory_feed(&h,r>>24,i);assert(h.used<=257&&h.count<=32);}
 uhf_inventory_discontinuity(&h);
 uint8_t p[]={8,0x10,0,1,2,3,4,0xAB,0xCD,0,0,0,9,0,0,3,1,4};
 assert(frame(&h,0x8A,p,sizeof(p),1)&&h.count==1&&h.tags[0].length==4&&h.tags[0].seen==4);
 assert(h.tags[0].signal_raw==9&&h.tags[0].frequency_raw==3&&h.tags[0].antenna==1);
 p[1]=0x30;assert(!frame(&h,0x8A,p,sizeof(p),2)&&h.count==1);return 0;}
''')


if __name__ == '__main__':
    unittest.main()
