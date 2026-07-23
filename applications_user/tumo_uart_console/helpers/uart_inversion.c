#include "uart_inversion.h"

#include <stm32wbxx.h>

void hermes_inversion_apply(HermesPort port, bool rx_inverted, bool tx_inverted) {
    USART_TypeDef* uart = (port == HermesPortLpuart) ? LPUART1 : USART1;
    const bool enabled = (uart->CR1 & USART_CR1_UE) != 0;

    if(enabled) CLEAR_BIT(uart->CR1, USART_CR1_UE);
    MODIFY_REG(
        uart->CR2,
        USART_CR2_RXINV | USART_CR2_TXINV,
        (rx_inverted ? USART_CR2_RXINV : 0U) | (tx_inverted ? USART_CR2_TXINV : 0U));
    if(enabled) SET_BIT(uart->CR1, USART_CR1_UE);
}
