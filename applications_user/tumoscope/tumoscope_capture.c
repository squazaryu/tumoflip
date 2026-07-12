#include "tumoscope_capture.h"
#include "tumoscope_ring.h"

#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_bus.h>
#include <furi_hal_gpio.h>

#include <stm32wbxx_ll_dma.h>
#include <stm32wbxx_ll_tim.h>

#define TUMOSCOPE_TIMER              TIM16
#define TUMOSCOPE_TIMER_CHANNEL      LL_TIM_CHANNEL_CH1
#define TUMOSCOPE_TIMER_CLOCK_HZ     64000000UL
#define TUMOSCOPE_DMA                DMA2
#define TUMOSCOPE_DMA_CHANNEL        LL_DMA_CHANNEL_2
#define TUMOSCOPE_DMA_IRQ            FuriHalInterruptIdDma2Ch2
#define TUMOSCOPE_DMA_SAMPLE_COUNT   512U
#define TUMOSCOPE_DMA_HALF_COUNT     (TUMOSCOPE_DMA_SAMPLE_COUNT / 2U)
#define TUMOSCOPE_PRETRIGGER_DEFAULT 25U

struct TumoScopeCapture {
    uint16_t dma_samples[TUMOSCOPE_DMA_SAMPLE_COUNT];
    uint8_t ring_storage[TUMOSCOPE_CAPTURE_MAX_SAMPLES];
    TumoScopeRing ring;
    TumoScopeCaptureConfig config;
    volatile bool running;
    volatile bool complete;
    volatile bool error;
    bool hardware_active;
};

static uint8_t tumoscope_capture_normalize(uint16_t gpio) {
    uint8_t sample = 0U;
    if(gpio & LL_GPIO_PIN_0) sample |= 1U << 0U;
    if(gpio & LL_GPIO_PIN_1) sample |= 1U << 1U;
    if(gpio & LL_GPIO_PIN_3) sample |= 1U << 2U;
    return sample;
}

static void tumoscope_capture_stop_fast(TumoScopeCapture* capture) {
    LL_TIM_DisableCounter(TUMOSCOPE_TIMER);
    LL_DMA_DisableChannel(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL);
    LL_DMA_DisableIT_HT(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL);
    LL_DMA_DisableIT_TC(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL);
    LL_DMA_DisableIT_TE(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL);
    capture->running = false;
}

static void
    tumoscope_capture_process(TumoScopeCapture* capture, const uint16_t* source, size_t count) {
    for(size_t index = 0U; index < count && capture->running; index++) {
        const uint8_t sample = tumoscope_capture_normalize(source[index]);
        if(tumoscope_ring_push(&capture->ring, sample)) {
            capture->complete = true;
            tumoscope_capture_stop_fast(capture);
        }
    }
}

static void tumoscope_capture_dma_isr(void* context) {
    TumoScopeCapture* capture = context;
    if(LL_DMA_IsActiveFlag_TE2(TUMOSCOPE_DMA)) {
        LL_DMA_ClearFlag_TE2(TUMOSCOPE_DMA);
        capture->error = true;
        tumoscope_capture_stop_fast(capture);
        return;
    }
    if(LL_DMA_IsActiveFlag_HT2(TUMOSCOPE_DMA)) {
        LL_DMA_ClearFlag_HT2(TUMOSCOPE_DMA);
        tumoscope_capture_process(capture, capture->dma_samples, TUMOSCOPE_DMA_HALF_COUNT);
    }
    if(LL_DMA_IsActiveFlag_TC2(TUMOSCOPE_DMA)) {
        LL_DMA_ClearFlag_TC2(TUMOSCOPE_DMA);
        tumoscope_capture_process(
            capture, &capture->dma_samples[TUMOSCOPE_DMA_HALF_COUNT], TUMOSCOPE_DMA_HALF_COUNT);
    }
}

TumoScopeCapture* tumoscope_capture_alloc(void) {
    return calloc(1U, sizeof(TumoScopeCapture));
}

static void tumoscope_capture_cleanup_hardware(TumoScopeCapture* capture) {
    if(!capture->hardware_active) return;

    tumoscope_capture_stop_fast(capture);
    furi_hal_interrupt_set_isr(TUMOSCOPE_DMA_IRQ, NULL, NULL);
    LL_TIM_DisableDMAReq_CC1(TUMOSCOPE_TIMER);
    LL_TIM_CC_DisableChannel(TUMOSCOPE_TIMER, TUMOSCOPE_TIMER_CHANNEL);
    LL_DMA_DeInit(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL);
    furi_hal_bus_disable(FuriHalBusTIM16);
    furi_hal_gpio_init_simple(&gpio_ext_pc0, GpioModeAnalog);
    furi_hal_gpio_init_simple(&gpio_ext_pc1, GpioModeAnalog);
    furi_hal_gpio_init_simple(&gpio_ext_pc3, GpioModeAnalog);
    capture->hardware_active = false;
}

void tumoscope_capture_free(TumoScopeCapture* capture) {
    if(!capture) return;
    tumoscope_capture_cleanup_hardware(capture);
    free(capture);
}

bool tumoscope_capture_start(TumoScopeCapture* capture, const TumoScopeCaptureConfig* config) {
    furi_check(capture);
    furi_check(config);
    if(capture->hardware_active || config->sample_count == 0U ||
       config->sample_count > TUMOSCOPE_CAPTURE_MAX_SAMPLES || config->sample_rate == 0U ||
       config->sample_rate > TUMOSCOPE_TIMER_CLOCK_HZ ||
       TUMOSCOPE_TIMER_CLOCK_HZ % config->sample_rate != 0U) {
        return false;
    }

    capture->config = *config;
    if(capture->config.pretrigger_percent > 50U)
        capture->config.pretrigger_percent = TUMOSCOPE_PRETRIGGER_DEFAULT;
    capture->running = true;
    capture->complete = false;
    capture->error = false;
    tumoscope_ring_init(
        &capture->ring,
        capture->ring_storage,
        capture->config.sample_count,
        capture->config.trigger,
        capture->config.pretrigger_percent);

    furi_hal_gpio_init(&gpio_ext_pc0, GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(&gpio_ext_pc1, GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);
    furi_hal_gpio_init(&gpio_ext_pc3, GpioModeInput, GpioPullNo, GpioSpeedVeryHigh);

    furi_hal_bus_enable(FuriHalBusTIM16);
    const uint32_t period = TUMOSCOPE_TIMER_CLOCK_HZ / config->sample_rate;
    LL_TIM_SetPrescaler(TUMOSCOPE_TIMER, 0U);
    LL_TIM_SetCounterMode(TUMOSCOPE_TIMER, LL_TIM_COUNTERMODE_UP);
    LL_TIM_SetAutoReload(TUMOSCOPE_TIMER, period - 1U);
    LL_TIM_SetClockDivision(TUMOSCOPE_TIMER, LL_TIM_CLOCKDIVISION_DIV1);
    LL_TIM_DisableARRPreload(TUMOSCOPE_TIMER);
    LL_TIM_SetClockSource(TUMOSCOPE_TIMER, LL_TIM_CLOCKSOURCE_INTERNAL);

    LL_TIM_OC_InitTypeDef timer_output = {};
    timer_output.OCMode = LL_TIM_OCMODE_FROZEN;
    timer_output.OCState = LL_TIM_OCSTATE_DISABLE;
    timer_output.OCNState = LL_TIM_OCSTATE_DISABLE;
    timer_output.CompareValue = period / 2U;
    timer_output.OCPolarity = LL_TIM_OCPOLARITY_HIGH;
    LL_TIM_OC_Init(TUMOSCOPE_TIMER, TUMOSCOPE_TIMER_CHANNEL, &timer_output);
    LL_TIM_OC_DisableFast(TUMOSCOPE_TIMER, TUMOSCOPE_TIMER_CHANNEL);

    LL_DMA_SetMemoryAddress(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL, (uint32_t)capture->dma_samples);
    LL_DMA_SetPeriphAddress(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL, (uint32_t)&GPIOC->IDR);
    LL_DMA_ConfigTransfer(
        TUMOSCOPE_DMA,
        TUMOSCOPE_DMA_CHANNEL,
        LL_DMA_DIRECTION_PERIPH_TO_MEMORY | LL_DMA_MODE_CIRCULAR | LL_DMA_PERIPH_NOINCREMENT |
            LL_DMA_MEMORY_INCREMENT | LL_DMA_PDATAALIGN_HALFWORD | LL_DMA_MDATAALIGN_HALFWORD |
            LL_DMA_PRIORITY_VERYHIGH);
    LL_DMA_SetDataLength(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL, TUMOSCOPE_DMA_SAMPLE_COUNT);
    LL_DMA_SetPeriphRequest(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL, LL_DMAMUX_REQ_TIM16_CH1);

    LL_DMA_ClearFlag_GI2(TUMOSCOPE_DMA);
    furi_hal_interrupt_set_isr_ex(
        TUMOSCOPE_DMA_IRQ, FuriHalInterruptPriorityHighest, tumoscope_capture_dma_isr, capture);
    LL_DMA_EnableIT_HT(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL);
    LL_DMA_EnableIT_TC(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL);
    LL_DMA_EnableIT_TE(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL);
    LL_TIM_EnableDMAReq_CC1(TUMOSCOPE_TIMER);
    LL_TIM_CC_EnableChannel(TUMOSCOPE_TIMER, TUMOSCOPE_TIMER_CHANNEL);
    LL_DMA_EnableChannel(TUMOSCOPE_DMA, TUMOSCOPE_DMA_CHANNEL);
    LL_TIM_SetCounter(TUMOSCOPE_TIMER, 0U);
    LL_TIM_GenerateEvent_UPDATE(TUMOSCOPE_TIMER);
    LL_TIM_EnableCounter(TUMOSCOPE_TIMER);
    capture->hardware_active = true;
    return true;
}

void tumoscope_capture_stop(TumoScopeCapture* capture) {
    if(!capture) return;
    tumoscope_capture_cleanup_hardware(capture);
}

bool tumoscope_capture_is_running(const TumoScopeCapture* capture) {
    return capture && capture->running;
}

bool tumoscope_capture_is_triggered(const TumoScopeCapture* capture) {
    return capture && capture->ring.triggered;
}

bool tumoscope_capture_is_complete(const TumoScopeCapture* capture) {
    return capture && capture->complete;
}

bool tumoscope_capture_has_error(const TumoScopeCapture* capture) {
    return !capture || capture->error;
}

size_t tumoscope_capture_progress(const TumoScopeCapture* capture) {
    return capture ? tumoscope_ring_progress(&capture->ring) : 0U;
}

size_t tumoscope_capture_count(const TumoScopeCapture* capture) {
    return capture ? tumoscope_ring_count(&capture->ring) : 0U;
}

size_t tumoscope_capture_trigger_index(const TumoScopeCapture* capture) {
    return capture ? capture->ring.trigger_index : 0U;
}

bool tumoscope_capture_copy(const TumoScopeCapture* capture, uint8_t* output, size_t capacity) {
    return capture && tumoscope_ring_copy(&capture->ring, output, capacity);
}
