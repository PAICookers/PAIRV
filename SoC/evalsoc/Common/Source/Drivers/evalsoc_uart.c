#include "evalsoc_uart.h"
#include "evalsoc.h"

typedef struct uart_rx_cache {
    UART_TypeDef *uart;
    uint8_t byte;
    bool valid;
} uart_rx_cache_t;

static uart_rx_cache_t uart_rx_cache[] = {{UART0, 0U, false},
                                          {UART1, 0U, false}};

static uart_rx_cache_t *uart_get_rx_cache(UART_TypeDef *uart)
{
    for (uint32_t i = 0U;
         i < (sizeof(uart_rx_cache) / sizeof(uart_rx_cache[0])); i++) {
        if (uart_rx_cache[i].uart == uart) {
            return &uart_rx_cache[i];
        }
    }
    return NULL;
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

int32_t uart_write_byte(UART_TypeDef *uart, uint8_t byte)
{
    if (__RARELY(uart == NULL)) {
        return -1;
    }
    while (uart_tx_fifo_is_full(uart))
        ;
    uart->TXFIFO = byte;
    return 0;
}

int32_t uart_write(UART_TypeDef *uart, uint8_t val)
{
    return uart_write_byte(uart, val);
}

int32_t uart_read_byte(UART_TypeDef *uart, uint8_t *byte)
{
    if (__RARELY((uart == NULL) || (byte == NULL))) {
        return -1;
    }
    uart_rx_cache_t *cache = uart_get_rx_cache(uart);
    if ((cache != NULL) && cache->valid) {
        *byte = cache->byte;
        cache->valid = false;
        return 0;
    }
    while (uart_pop_rx_fifo(uart, byte) != 0)
        ;
    return 0;
}

uint8_t uart_read(UART_TypeDef *uart)
{
    uint8_t byte = 0U;
    if (uart_read_byte(uart, &byte) != 0) {
        return 0U;
    }
    return byte;
}

int32_t uart_read_byte_nonblock(UART_TypeDef *uart, uint8_t *byte)
{
    if (__RARELY((uart == NULL) || (byte == NULL))) {
        return -1;
    }
    uart_rx_cache_t *cache = uart_get_rx_cache(uart);
    if ((cache != NULL) && cache->valid) {
        *byte = cache->byte;
        cache->valid = false;
        return 0;
    }
    return uart_pop_rx_fifo(uart, byte);
}

bool uart_tx_fifo_is_full(UART_TypeDef *uart)
{
    if (__RARELY(uart == NULL)) {
        return true;
    }
    return (uart->TXFIFO & UART_TXFIFO_FULL) != 0U;
}

bool uart_rx_fifo_is_empty(UART_TypeDef *uart)
{
    if (__RARELY(uart == NULL)) {
        return true;
    }
    uart_rx_cache_t *cache = uart_get_rx_cache(uart);
    if (__RARELY(cache == NULL)) {
        return true;
    }
    if (cache->valid) {
        return false;
    }
    uint8_t byte;
    if (uart_pop_rx_fifo(uart, &byte) != 0) {
        return true;
    }
    cache->byte = byte;
    cache->valid = true;
    return false;
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

uint32_t uart_irq_pending(UART_TypeDef *uart)
{
    if (__RARELY(uart == NULL)) {
        return 0U;
    }
    return (uint32_t)uart->IP;
}

bool uart_rx_irq_pending(UART_TypeDef *uart)
{
    return (uart_irq_pending(uart) & UART_IP_RXWM) != 0U;
}

bool uart_tx_irq_pending(UART_TypeDef *uart)
{
    return (uart_irq_pending(uart) & UART_IP_TXWM) != 0U;
}

int32_t uart_drain_rx_fifo(UART_TypeDef *uart, uart_rx_byte_fn cb, void *ctx)
{
    if (__RARELY((uart == NULL) || (cb == NULL))) {
        return -1;
    }

    uint8_t byte;
    while (uart_read_byte_nonblock(uart, &byte) == 0) {
        cb(byte, ctx);
    }

    return 0;
}
