#include "evalsoc_uart.h"
#include "evalsoc.h"

int32_t uart_init(UART_TypeDef *uart, uint32_t baudrate)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    uart->DIV = SystemCoreClock / baudrate - 1;
    uart->TXCTRL |= UART_TXEN;
    uart->RXCTRL |= UART_RXEN;
    return 0;
}

int32_t uart_config_stopbit(UART_TypeDef *uart, UART_STOP_BIT stopbit)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    uint32_t stopval =
        ((uint32_t)stopbit << UART_TXCTRL_NSTOP_OFS) & UART_TXCTRL_NSTOP_MASK;
    uart->TXCTRL = (uart->TXCTRL & ~UART_TXCTRL_NSTOP_MASK) | stopval;
    return 0;
}

int32_t uart_write(UART_TypeDef *uart, uint8_t val)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    while (uart->TXFIFO & UART_TXFIFO_FULL)
        ;
    uart->TXFIFO = val;
    return 0;
}

uint8_t uart_read(UART_TypeDef *uart)
{
    uint32_t reg;
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    do {
        reg = uart->RXFIFO;
    } while (reg & UART_RXFIFO_EMPTY);
    return (uint8_t)(reg & 0xFF);
}

int32_t uart_set_tx_watermark(UART_TypeDef *uart, uint32_t watermark)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    watermark = (watermark << UART_TXCTRL_TXCNT_OFS) & UART_TXCTRL_TXCNT_MASK;
    uart->TXCTRL = (uart->TXCTRL & ~UART_TXCTRL_TXCNT_MASK) | watermark;
    return 0;
}

int32_t uart_enable_txint(UART_TypeDef *uart)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    uart->IE |= UART_IE_TXIE_MASK;
    return 0;
}

int32_t uart_disable_txint(UART_TypeDef *uart)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    uart->IE &= ~UART_IE_TXIE_MASK;
    return 0;
}

int32_t uart_set_rx_watermark(UART_TypeDef *uart, uint32_t watermark)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    watermark = (watermark << UART_RXCTRL_RXCNT_OFS) & UART_RXCTRL_RXCNT_MASK;
    uart->RXCTRL = (uart->RXCTRL & ~UART_RXCTRL_RXCNT_MASK) | watermark;
    return 0;
}

int32_t uart_enable_rxint(UART_TypeDef *uart)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    uart->IE |= UART_IE_RXIE_MASK;
    return 0;
}

int32_t uart_disable_rxint(UART_TypeDef *uart)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    uart->IE &= ~UART_IE_RXIE_MASK;
    return 0;
}

int32_t uart_get_status(UART_TypeDef *uart)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    return (uart->IP);
}

int32_t uart_clear_status(UART_TypeDef *uart, uint32_t mask)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    uart->IP &= ~mask;
    return 0;
}

static int32_t uart_pop_rx_fifo(UART_TypeDef *uart, uint8_t *byte)
{
    uint32_t reg = uart->RXFIFO;
    if ((reg & UART_RXFIFO_EMPTY) != 0U) {
        return -1;
    }
    *byte = (uint8_t)(reg & 0xFFU);
    return 0;
}

int32_t uart_drain_rx_fifo(UART_TypeDef *uart, uart_rx_byte_fn cb, void *ctx)
{
    if (__RARELY((uart == NULL) || (cb == NULL))) {
        return -1;
    }

    uint8_t byte;
    while (uart_pop_rx_fifo(uart, &byte) == 0) {
        cb(byte, ctx);
    }

    return 0;
}
