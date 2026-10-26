/* GPL-3.0 ST25 transparent-mode sampling adapted from ARF9b18419de2,
 * originally @thedoctorz. Resource ownership, bounded static buffers and
 * cancellation are Tumoflip adaptations. No field transmission is enabled. */
#include "nfc_field_capture.h"
#include "nfc_field_decoder.h"
#include "nfc_field_lease.h"
#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_nfc.h>
#include <furi_hal_bus.h>
#include <furi_hal_interrupt.h>
#include <furi_hal_resources.h>
#include <lib/drivers/st25r3916.h>
#include <lib/drivers/st25r3916_reg.h>
#include <stm32wbxx.h>
#include <string.h>

#define NFC_FIELD_RING_SIZE 8192U
#define NFC_FIELD_MARGIN 1024U
static uint8_t nfc_field_ring[NFC_FIELD_RING_SIZE] __attribute__((aligned(4)));
static uint8_t nfc_field_batch[NFC_FIELD_SAMPLE_BATCH] __attribute__((aligned(4)));
typedef struct {uint32_t dmamux;bool armed,transparent,insomnia;}NfcFieldHardware;

static void nfc_field_irq(void* context) {
    if((uintptr_t)context == NfcFieldResourceDmaIrq) DMA1->IFCR = 0x0FU;
    else TIM2->SR = 0;
}
static bool nfc_field_take(void* context, NfcFieldResource resource) {
    NfcFieldHardware* hardware = context;
    if(resource == NfcFieldResourceNfc) return furi_hal_nfc_acquire() == FuriHalNfcErrorNone;
    if(resource == NfcFieldResourceDmaIrq) {
        FURI_CRITICAL_ENTER();
        bool reserved = !(DMA1_Channel1->CCR & DMA_CCR_EN) &&
                        furi_hal_interrupt_try_set_isr(FuriHalInterruptIdDma1Ch1, nfc_field_irq, (void*)(uintptr_t)resource);
        if(reserved) {
            RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMAMUX1EN;
            (void)RCC->AHB1ENR;
            hardware->dmamux = DMAMUX1_Channel0->CCR;
            DMA1_Channel1->CCR = 0;
            DMA1->IFCR = 0x0FU;
        }
        FURI_CRITICAL_EXIT();
        return reserved;
    }
    if(resource == NfcFieldResourceTimerIrq) {
        FURI_CRITICAL_ENTER();
        bool reserved = !furi_hal_bus_is_enabled(FuriHalBusTIM2) &&
                        furi_hal_interrupt_try_set_isr(FuriHalInterruptIdTIM2, nfc_field_irq, (void*)(uintptr_t)resource);
        FURI_CRITICAL_EXIT();
        return reserved;
    }
    if(resource == NfcFieldResourceAwake) {
        if(furi_hal_nfc_low_power_mode_stop() == FuriHalNfcErrorNone) return true;
        furi_hal_nfc_low_power_mode_start();
    }
    return false;
}
static void nfc_field_drop(void* context, NfcFieldResource resource) {
    NfcFieldHardware* hardware = context;
    if(resource == NfcFieldResourceAwake) furi_hal_nfc_low_power_mode_start();
    else if(resource == NfcFieldResourceTimerIrq) furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, NULL, NULL);
    else if(resource == NfcFieldResourceDmaIrq) {
        DMA1_Channel1->CCR = 0;
        DMA1->IFCR = 0x0FU;
        DMAMUX1_Channel0->CCR = hardware->dmamux;
        furi_hal_interrupt_set_isr(FuriHalInterruptIdDma1Ch1, NULL, NULL);
    } else if(resource == NfcFieldResourceNfc) furi_hal_nfc_release();
}
static const FuriHalSpiBusHandle* nfc_field_spi(void) {return &furi_hal_spi_bus_handle_nfc;}
static void nfc_field_reg_write(uint8_t address,uint8_t value) {st25r3916_write_reg(nfc_field_spi(),address,value);}
static uint8_t nfc_field_reg_read(uint8_t address) {uint8_t value=0;st25r3916_read_reg(nfc_field_spi(),address,&value);return value;}
static void nfc_field_reg_set(uint8_t address,uint8_t value) {st25r3916_set_reg_bits(nfc_field_spi(),address,value);}
static void nfc_field_reg_clear(uint8_t address,uint8_t value) {st25r3916_clear_reg_bits(nfc_field_spi(),address,value);}
static void nfc_field_command(uint8_t command) {st25r3916_direct_cmd(nfc_field_spi(),command);}

static void nfc_field_chip_prepare(void) {
    nfc_field_command(ST25R3916_CMD_STOP);
    nfc_field_command(ST25R3916_CMD_SET_DEFAULT);
    furi_delay_ms(1);
    nfc_field_reg_set(ST25R3916_REG_IO_CONF2, ST25R3916_REG_IO_CONF2_io_drv_lvl);
    nfc_field_reg_set(ST25R3916_REG_OP_CONTROL, ST25R3916_REG_OP_CONTROL_en);
    for(unsigned i=0;i<50;i++) {
        if(nfc_field_reg_read(ST25R3916_REG_AUX_DISPLAY)&ST25R3916_REG_AUX_DISPLAY_osc_ok) break;
        furi_delay_ms(1);
    }
    nfc_field_command(ST25R3916_CMD_ADJUST_REGULATORS);
    furi_delay_ms(6);
    nfc_field_reg_clear(ST25R3916_REG_MODE,ST25R3916_REG_MODE_om_mask|ST25R3916_REG_MODE_tr_am|ST25R3916_REG_MODE_targ);
    nfc_field_reg_set(ST25R3916_REG_MODE,ST25R3916_REG_MODE_om_iso14443a);
    nfc_field_reg_write(ST25R3916_REG_OVERSHOOT_CONF1,0x40);
    nfc_field_reg_write(ST25R3916_REG_OVERSHOOT_CONF2,0x03);
    nfc_field_reg_write(ST25R3916_REG_UNDERSHOOT_CONF1,0x40);
    nfc_field_reg_write(ST25R3916_REG_UNDERSHOOT_CONF2,0x03);
    nfc_field_reg_write(ST25R3916_REG_CORR_CONF1,0x51);
    nfc_field_reg_write(ST25R3916_REG_CORR_CONF2,0);
    nfc_field_reg_write(ST25R3916_REG_BIT_RATE,0);
    nfc_field_reg_clear(ST25R3916_REG_OP_CONTROL,ST25R3916_REG_OP_CONTROL_tx_en);
}
static void nfc_field_chip_sniff(void) {
    nfc_field_command(ST25R3916_CMD_STOP);
    nfc_field_reg_clear(ST25R3916_REG_OP_CONTROL,ST25R3916_REG_OP_CONTROL_tx_en);
    nfc_field_reg_set(ST25R3916_REG_OP_CONTROL,ST25R3916_REG_OP_CONTROL_en|ST25R3916_REG_OP_CONTROL_rx_en|ST25R3916_REG_OP_CONTROL_en_fd_auto_efd);
    nfc_field_reg_write(ST25R3916_REG_RX_CONF1,0x03);
    nfc_field_reg_write(ST25R3916_REG_RX_CONF2,0x40);
    nfc_field_reg_write(ST25R3916_REG_RX_CONF3,0x78);
    nfc_field_reg_write(ST25R3916_REG_RX_CONF4,0);
    nfc_field_reg_write(ST25R3916_REG_AUX,ST25R3916_REG_AUX_dis_corr);
    nfc_field_reg_clear(ST25R3916_REG_IO_CONF2,ST25R3916_REG_IO_CONF2_miso_pd1|ST25R3916_REG_IO_CONF2_miso_pd2);
    nfc_field_command(ST25R3916_CMD_RESET_RXGAIN);
    nfc_field_command(ST25R3916_CMD_CLEAR_FIFO);
    nfc_field_command(ST25R3916_CMD_UNMASK_RECEIVE_DATA);
}
static bool nfc_field_arm(NfcFieldHardware* hardware) {
    FURI_CRITICAL_ENTER();
    const bool ready = !furi_hal_bus_is_enabled(FuriHalBusTIM2) && !(DMA1_Channel1->CCR&DMA_CCR_EN);
    if(ready) {
        furi_hal_bus_enable(FuriHalBusTIM2);
        DMA1_Channel1->CCR=0;
        DMA1_Channel1->CPAR=(uint32_t)(uintptr_t)&GPIOB->IDR;
        DMA1_Channel1->CMAR=(uint32_t)(uintptr_t)nfc_field_ring;
        DMA1_Channel1->CNDTR=NFC_FIELD_RING_SIZE;
        DMAMUX1_Channel0->CCR=32;
        TIM2->CR1=0;TIM2->SMCR=0;TIM2->CCER=0;TIM2->DIER=0;TIM2->CCMR1=0;TIM2->CCMR2=0;
        TIM2->PSC=0;TIM2->ARR=NFC_FIELD_SAMPLE_CYCLES-1;TIM2->CNT=0;
#ifdef TIM_OR_TI4_RMP
        TIM2->OR=0;
#endif
        TIM2->EGR=TIM_EGR_UG;TIM2->SR=0;TIM2->DIER=TIM_DIER_UDE;
        DMA1_Channel1->CCR=(2u<<12)|(1u<<7)|(1u<<5)|DMA_CCR_EN;
        TIM2->CR1=TIM_CR1_CEN;
        hardware->armed=true;
    }
    FURI_CRITICAL_EXIT();
    return ready;
}
static void nfc_field_hardware_stop(NfcFieldHardware* hardware) {
    if(hardware->armed) {
        TIM2->CR1&=~TIM_CR1_CEN;TIM2->DIER=0;DMA1_Channel1->CCR=0;DMA1->IFCR=0x0FU;
        furi_hal_bus_disable(FuriHalBusTIM2);
        hardware->armed=false;
    }
    if(hardware->transparent) {
        GPIOB->MODER=(GPIOB->MODER&~GPIO_MODER_MODE4)|(2u<<GPIO_MODER_MODE4_Pos);
        nfc_field_command(ST25R3916_CMD_STOP);
        furi_hal_gpio_enable_int_callback(&gpio_nfc_irq_rfid_pull);
        hardware->transparent=false;
    }
    if(hardware->insomnia) {furi_hal_power_insomnia_exit();hardware->insomnia=false;}
}
static bool nfc_field_write_position(uint32_t* position) {
    uint32_t remaining=DMA1_Channel1->CNDTR;
    if(remaining>NFC_FIELD_RING_SIZE) return false;
    *position=(NFC_FIELD_RING_SIZE-remaining)%NFC_FIELD_RING_SIZE;
    __DMB();
    return true;
}
NfcFieldCaptureResult nfc_field_capture(uint32_t timeout_ms,NfcFieldStop stop,void* context) {
    if(timeout_ms<100||timeout_ms>5000) return NfcFieldCaptureError;
    NfcFieldHardware hardware={0};NfcFieldLease lease={0};
    static const NfcFieldLeaseOps ops={nfc_field_take,nfc_field_drop};
    if(!nfc_field_lease_acquire(&lease,&ops,&hardware)) return NfcFieldCaptureBusy;
    NfcFieldCaptureResult result=NfcFieldCaptureNoField;
    uint32_t start=furi_get_tick(),deadline=furi_ms_to_ticks(timeout_ms);
    furi_hal_power_insomnia_enter();hardware.insomnia=true;
    nfc_field_chip_prepare();
    while(!(nfc_field_reg_read(ST25R3916_REG_AUX_DISPLAY)&ST25R3916_REG_AUX_DISPLAY_efd_o)) {
        if(stop&&stop(context)) {result=NfcFieldCaptureCancelled;goto cleanup;}
        if(furi_get_tick()-start>=deadline) goto cleanup;
        furi_delay_ms(1);
    }
    nfc_field_chip_sniff();
    furi_hal_gpio_disable_int_callback(&gpio_nfc_irq_rfid_pull);
    st25r3916_mask_irq(nfc_field_spi(),ST25R3916_IRQ_MASK_ALL);
    furi_hal_gpio_write(&gpio_nfc_cs,true);
    furi_hal_gpio_init(&gpio_nfc_cs,GpioModeOutputPushPull,GpioPullNo,GpioSpeedVeryHigh);
    nfc_field_command(ST25R3916_CMD_TRANSPARENT_MODE);
    GPIOB->MODER&=~GPIO_MODER_MODE4;hardware.transparent=true;
    if(!nfc_field_arm(&hardware)) {result=NfcFieldCaptureBusy;goto cleanup;}
    furi_delay_ms(1);
    nfc_field_decoder_reset((nfc_field_ring[0]>>4)&1);
    uint32_t read=0,last_tick=furi_get_tick();
    result=NfcFieldCaptureComplete;
    while(furi_get_tick()-start<deadline) {
        if(stop&&stop(context)) {result=NfcFieldCaptureCancelled;break;}
        uint32_t now=furi_get_tick(),write;
        if(!nfc_field_write_position(&write)) {result=NfcFieldCaptureError;break;}
        uint32_t pending=(write+NFC_FIELD_RING_SIZE-read)%NFC_FIELD_RING_SIZE;
        if(now-last_tick>=furi_ms_to_ticks(3)||pending>NFC_FIELD_RING_SIZE-NFC_FIELD_MARGIN) {
            nfc_field_decoder_discontinuity((nfc_field_ring[write]>>4)&1);
            read=write;last_tick=now;continue;
        }
        last_tick=now;
        if(pending<1024) {furi_delay_ms(1);continue;}
        uint32_t count=pending-256;
        if(count>NFC_FIELD_SAMPLE_BATCH) count=NFC_FIELD_SAMPLE_BATCH;
        uint32_t first=NFC_FIELD_RING_SIZE-read;
        if(first>count) first=count;
        memcpy(nfc_field_batch,nfc_field_ring+read,first);
        if(count>first) memcpy(nfc_field_batch+first,nfc_field_ring,count-first);
        uint32_t after;
        if(!nfc_field_write_position(&after)) {result=NfcFieldCaptureError;break;}
        uint32_t advanced=(after+NFC_FIELD_RING_SIZE-write)%NFC_FIELD_RING_SIZE;
        uint32_t after_tick=furi_get_tick();
        if(after_tick-now>=furi_ms_to_ticks(3)||pending+advanced>=NFC_FIELD_RING_SIZE) {
            nfc_field_decoder_discontinuity((nfc_field_ring[after]>>4)&1);
            read=after;last_tick=after_tick;continue;
        }
        if(!nfc_field_decoder_feed(nfc_field_batch,count)) {result=NfcFieldCaptureError;break;}
        read=(read+count)%NFC_FIELD_RING_SIZE;
        furi_thread_yield();
    }
    nfc_field_decoder_finish();
cleanup:
    nfc_field_hardware_stop(&hardware);
    nfc_field_lease_release(&lease);
    return result;
}
