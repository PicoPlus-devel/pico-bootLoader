/*
 * stdio_buffered.h - UART stdio that does not wait for the UART.
 *
 * The SDK's stdio_uart driver hands each character to the UART and waits
 * whenever its 32-byte FIFO is full. At 115200 baud that is ~87 us per
 * character, and the boot log (the banner, initAll's own reports, the scan)
 * is several KB: about half a second of a ~1 s start-up spent waiting on the
 * serial port.
 *
 * This driver takes stdio_uart's place on the same UART. printf copies into
 * an 8 KB RAM buffer and returns; the UART's TX interrupt sends the buffer out
 * in the background. Every line is still sent, in order, at the same speed --
 * only the waiting is gone. The UART set-up (pins, baud rate) is stdio_uart's,
 * left as stdio_init_all() made it.
 *
 * When the buffer is full, the writer waits for room, as stdio_uart always
 * did. stdio_flush() does NOT wait: the SDK calls it after every printf() and
 * puts(), so a waiting flush would make every line synchronous again. To wait
 * until the buffer and the UART are empty, call stdio_buffered_flush(); the
 * loader does so before every reboot and before handing the board to an
 * application, or the tail of the log would be cut off. Output written from an
 * interrupt handler or with interrupts off is sent straight away, and panic()
 * (through _exit) empties the buffer before it stops, so a panic message is
 * not left behind in RAM. Only a hard fault loses whatever was still buffered.
 *
 * Builds without UART stdio (UART_ENABLED 0) get stubs: init does nothing and
 * stdio_buffered_flush() is plain stdio_flush().
 */
#ifndef STDIO_BUFFERED_H
#define STDIO_BUFFERED_H

#ifdef __cplusplus
extern "C" {
#endif

/* Swap stdio_uart for the buffered driver. Call once, after stdio_init_all(). */
void stdio_buffered_init(void);

/* Wait until everything written so far has left the UART. Plain stdio_flush()
 * when the buffered driver is not in use. */
void stdio_buffered_flush(void);

#ifdef __cplusplus
}
#endif

#endif /* STDIO_BUFFERED_H */
