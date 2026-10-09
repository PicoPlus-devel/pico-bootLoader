/*
 * stdio_buffered.c - see stdio_buffered.h.
 */
#include "stdio_buffered.h"

#include "pico/stdlib.h"

#if LIB_PICO_STDIO_UART

#include "pico/stdio/driver.h"
#include "pico/stdio_uart.h"
#include "pico/critical_section.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "hardware/uart.h"

#define BUF_SIZE 8192u                      /* power of two */
#define BUF_MASK (BUF_SIZE - 1u)

/* Free-running indices: the buffer holds s_head - s_tail bytes. Writers move
 * s_head, whoever feeds the UART moves s_tail, both under s_cs. */
static char               s_buf[BUF_SIZE];
static volatile uint32_t  s_head;
static volatile uint32_t  s_tail;
static critical_section_t s_cs;
static bool               s_active;

/* Move bytes from the buffer into the TX FIFO while both allow, and keep the
 * TX interrupt enabled exactly as long as bytes are left over. The PL011 only
 * raises that interrupt when the FIFO level drops through its trigger level,
 * so the FIFO is always primed here, by the writer, before the interrupt is
 * relied on. Caller holds s_cs. */
static void __not_in_flash_func(fill_fifo)(uart_hw_t *hw)
{
    while (s_tail != s_head && !(hw->fr & UART_UARTFR_TXFF_BITS)) {
        hw->dr = (uint8_t)s_buf[s_tail & BUF_MASK];
        s_tail++;
    }
    if (s_tail != s_head) hw_set_bits(&hw->imsc, UART_UARTIMSC_TXIM_BITS);
    else                  hw_clear_bits(&hw->imsc, UART_UARTIMSC_TXIM_BITS);
}

static void __not_in_flash_func(on_uart_irq)(void)
{
    critical_section_enter_blocking(&s_cs);
    fill_fifo(uart_get_hw(uart_default));
    critical_section_exit(&s_cs);
}

/* Feed the UART from this context until everything is out. Works with
 * interrupts off, so it is also what a full buffer falls back on. */
static void drain(void)
{
    uart_hw_t *hw = uart_get_hw(uart_default);
    for (;;) {
        critical_section_enter_blocking(&s_cs);
        fill_fifo(hw);
        bool empty = (s_tail == s_head);
        critical_section_exit(&s_cs);
        if (empty) break;
    }
    uart_tx_wait_blocking(uart_default);
}

/* The TX interrupt cannot run for this writer: it is an interrupt handler
 * itself, or it has interrupts disabled. */
static bool irq_cannot_drain(void)
{
    if (__get_current_exception()) return true;
    uint32_t primask = save_and_disable_interrupts();
    restore_interrupts(primask);
    return (primask & 1u) != 0;
}

static void out_chars(const char *buf, int len)
{
    uart_hw_t *hw = uart_get_hw(uart_default);
    int i = 0;
    while (i < len) {
        critical_section_enter_blocking(&s_cs);
        while (i < len && s_head - s_tail < BUF_SIZE) {
            s_buf[s_head & BUF_MASK] = buf[i++];
            s_head++;
        }
        /* Prime the FIFO -- and, while the buffer is full, make room. */
        fill_fifo(hw);
        critical_section_exit(&s_cs);
    }
    if (irq_cannot_drain()) drain();
}

/* stdio's flush hook. The SDK calls it after every printf() and puts(), so it
 * must not wait for the UART -- that would make every line synchronous again.
 * It only sends straight away when the TX interrupt cannot. Waiting for real is
 * stdio_buffered_flush(). */
static void out_flush(void)
{
    if (irq_cannot_drain()) drain();
}

static int in_chars(char *buf, int len)
{
    int n = 0;
    while (n < len && uart_is_readable(uart_default)) {
        buf[n++] = (char)uart_getc(uart_default);
    }
    return n ? n : PICO_ERROR_NO_DATA;
}

static stdio_driver_t s_driver = {
    .out_chars = out_chars,
    .out_flush = out_flush,
    .in_chars  = in_chars,
#if PICO_STDIO_ENABLE_CRLF_SUPPORT
    .crlf_enabled = PICO_STDIO_DEFAULT_CRLF,
#endif
};

void stdio_buffered_init(void)
{
    if (s_active) return;
    critical_section_init(&s_cs);
    irq_add_shared_handler(UART_IRQ_NUM(uart_default), on_uart_irq,
                           PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY);
    irq_set_enabled(UART_IRQ_NUM(uart_default), true);
    stdio_set_driver_enabled(&stdio_uart, false);
    stdio_set_driver_enabled(&s_driver, true);
    s_active = true;
}

void stdio_buffered_flush(void)
{
    if (s_active) drain();
    else          stdio_flush();
}

/* panic() and exit() end here (it replaces the SDK's weak _exit). Send what is
 * still buffered, the panic message included, before stopping as the SDK
 * does. No lock and no interrupt: either may be what failed. */
void __attribute__((noreturn)) _exit(__unused int status)
{
    (void)save_and_disable_interrupts();
    if (s_active) {
        uart_hw_t *hw = uart_get_hw(uart_default);
        while (s_tail != s_head) {
            if (!(hw->fr & UART_UARTFR_TXFF_BITS)) {
                hw->dr = (uint8_t)s_buf[s_tail & BUF_MASK];
                s_tail++;
            }
        }
        uart_tx_wait_blocking(uart_default);
    }
    for (;;) __breakpoint();
}

#else

void stdio_buffered_init(void) {}
void stdio_buffered_flush(void) { stdio_flush(); }

#endif /* LIB_PICO_STDIO_UART */
