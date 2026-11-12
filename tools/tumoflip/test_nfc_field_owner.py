"""Compile the real hardware lease callbacks with register/API stand-ins."""
from pathlib import Path
import re
import unittest
from tools.tumoflip.test_hotplug_assets import function
from tools.tumoflip.test_nfc_completion_equality import native

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "applications_user/nfc_field_sniffer"


class NfcFieldOwnerTests(unittest.TestCase):
    def test_busy_dma_timer_and_failed_wake_preserve_external_owners(self):
        code = (APP / "nfc_field_capture.c").read_text() if (APP / "nfc_field_capture.c").exists() else ""
        lease = "\n".join(re.sub(r"^#(?:include|pragma).*\n", "\n", (APP / name).read_text(), flags=re.M)
                          for name in ("nfc_field_lease.h", "nfc_field_lease.c"))
        functions = "\n".join(function(code, signature) if signature in code else signature + ";" for signature in (
            "static bool nfc_field_take(void* context, NfcFieldResource resource)",
            "static void nfc_field_drop(void* context, NfcFieldResource resource)",
        ))
        native(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
''' + lease + r'''
typedef struct{uint32_t dmamux;}NfcFieldHardware;
static struct{uint32_t CCR;}dma_channel;static struct{uint32_t IFCR;}dma;
static struct{uint32_t CCR;}mux;static struct{uint32_t AHB1ENR;}rcc;
#define DMA1_Channel1 (&dma_channel)
#define DMA1 (&dma)
#define DMAMUX1_Channel0 (&mux)
#define RCC (&rcc)
#define DMA_CCR_EN 1u
#define RCC_AHB1ENR_DMA1EN 1u
#define RCC_AHB1ENR_DMAMUX1EN 2u
enum{FuriHalNfcErrorNone,FuriHalNfcErrorBusy,FuriHalBusTIM2,FuriHalInterruptIdDma1Ch1=0,FuriHalInterruptIdTIM2=1};
typedef void(*FuriHalInterruptISR)(void*);
static bool busy_nfc,busy_timer,bad_wake;static unsigned nfc_owned,awakes,sleeps,critical;
static FuriHalInterruptISR owners[2];
static void nfc_field_irq(void*c){(void)c;}
static int furi_hal_nfc_acquire(void){if(busy_nfc)return FuriHalNfcErrorBusy;nfc_owned++;return FuriHalNfcErrorNone;}
static void furi_hal_nfc_release(void){assert(nfc_owned==1);nfc_owned--;}
static bool furi_hal_bus_is_enabled(int b){assert(b==FuriHalBusTIM2);return busy_timer;}
static bool furi_hal_interrupt_try_set_isr(int n,FuriHalInterruptISR f,void*c){(void)c;assert(critical);if(owners[n])return false;owners[n]=f;return true;}
static void furi_hal_interrupt_set_isr(int n,FuriHalInterruptISR f,void*c){(void)c;assert(!f&&owners[n]==nfc_field_irq);owners[n]=NULL;}
static int furi_hal_nfc_low_power_mode_stop(void){awakes++;return bad_wake?FuriHalNfcErrorBusy:FuriHalNfcErrorNone;}
static void furi_hal_nfc_low_power_mode_start(void){sleeps++;}
#define FURI_CRITICAL_ENTER() critical++
#define FURI_CRITICAL_EXIT() critical--
''' + functions + r'''
static void other(void*c){(void)c;}
int main(void){NfcFieldLease l={0};NfcFieldHardware h={0};const NfcFieldLeaseOps ops={nfc_field_take,nfc_field_drop};
 owners[0]=other;DMA1_Channel1->CCR=0x80;assert(!nfc_field_lease_acquire(&l,&ops,&h));assert(owners[0]==other&&DMA1_Channel1->CCR==0x80&&!nfc_owned&&!awakes);owners[0]=NULL;
 DMA1_Channel1->CCR=DMA_CCR_EN;assert(!nfc_field_lease_acquire(&l,&ops,&h));assert(DMA1_Channel1->CCR==DMA_CCR_EN&&!nfc_owned);DMA1_Channel1->CCR=0;
 busy_timer=true;assert(!nfc_field_lease_acquire(&l,&ops,&h));assert(!owners[0]&&!owners[1]&&!nfc_owned&&!awakes);busy_timer=false;
 bad_wake=true;assert(!nfc_field_lease_acquire(&l,&ops,&h));assert(!owners[0]&&!owners[1]&&!nfc_owned&&awakes==1&&sleeps==1);bad_wake=false;
 assert(nfc_field_lease_acquire(&l,&ops,&h));assert(owners[0]&&owners[1]&&nfc_owned);nfc_field_lease_release(&l);assert(!owners[0]&&!owners[1]&&!nfc_owned&&sleeps==2&&!critical);
 return 0;}
''')


if __name__ == "__main__":
    unittest.main()
