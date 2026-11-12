"""Nonfatal IRQ reservation prevents a field FAP from stealing a live owner."""
from pathlib import Path
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]


class InterruptReservationTests(unittest.TestCase):
    def test_invalid_busy_and_competing_reservations_do_not_replace_an_owner(self):
        code = (ROOT / "targets/f7/furi_hal/furi_hal_interrupt.c").read_text()
        signature = "bool furi_hal_interrupt_try_set_isr("
        body = function(code, signature) if signature in code else "bool furi_hal_interrupt_try_set_isr(FuriHalInterruptId,FuriHalInterruptISR,void*);"
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
typedef enum{First,Second,FuriHalInterruptIdMax}FuriHalInterruptId;
typedef void(*FuriHalInterruptISR)(void*);
static struct{struct{FuriHalInterruptISR isr;void*context;}isr[FuriHalInterruptIdMax];}furi_hal_interrupt;
pthread_mutex_t mutex=PTHREAD_MUTEX_INITIALIZER;
#define FURI_CRITICAL_ENTER() assert(!pthread_mutex_lock(&mutex))
#define FURI_CRITICAL_EXIT() assert(!pthread_mutex_unlock(&mutex))
void furi_hal_interrupt_set_isr(FuriHalInterruptId n,FuriHalInterruptISR f,void*c){assert(!furi_hal_interrupt.isr[n].isr);furi_hal_interrupt.isr[n].isr=f;furi_hal_interrupt.isr[n].context=c;}
''' + body + r'''
static void callback(void*c){(void)c;}
static bool won[32];
static void*compete(void*c){size_t i=(size_t)c;won[i]=furi_hal_interrupt_try_set_isr(Second,callback,c);return NULL;}
int main(void){
 assert(!furi_hal_interrupt_try_set_isr(FuriHalInterruptIdMax,callback,NULL));
 assert(!furi_hal_interrupt_try_set_isr((FuriHalInterruptId)-1,callback,NULL));
 assert(!furi_hal_interrupt_try_set_isr(First,NULL,NULL));
 assert(furi_hal_interrupt_try_set_isr(First,callback,(void*)123));
 assert(!furi_hal_interrupt_try_set_isr(First,callback,(void*)456));assert(furi_hal_interrupt.isr[First].context==(void*)123);
 pthread_t threads[32];for(size_t i=0;i<32;i++)assert(!pthread_create(&threads[i],NULL,compete,(void*)i));
 unsigned wins=0;for(unsigned i=0;i<32;i++){assert(!pthread_join(threads[i],NULL));wins+=won[i];}assert(wins==1);
 return 0;
}
''')


if __name__ == "__main__":
    unittest.main()
