/*
 * program_name.h - Extract the pico_set_program_name() string from an RP2350
 *                  image, either via XIP (already in flash) or by streaming
 *                  the on-disk UF2 file without flashing anything.
 *
 * The string is the canonical identifier for an emulator across the bootloader
 * UX: the in-flash entry is highlighted by matching the in-flash program_name
 * against each SD-listed UF2's program_name. The future graphical menu uses
 * the same string as the lookup key for its PNG icon table.
 */
#ifndef PROGRAM_NAME_H
#define PROGRAM_NAME_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse the program name of an image currently resident in flash. `base` is
 * the absolute XIP address (e.g. APP_BASE_ADDR) and `region_size` bounds the
 * address range we will dereference into. `out` is always NUL-terminated
 * (empty on failure if cap > 0). Returns true on success.
 */
bool program_name_from_xip(uint32_t base, uint32_t region_size,
                           char *out, unsigned cap);

/*
 * Read the binary-end address (the SDK's __flash_binary_end, exclusive) of an
 * image resident in flash, from the same binary_info table. `end - base` is
 * the image size. Returns false if the image has no such entry or the value
 * falls outside [base, base + region_size].
 */
bool binary_end_from_xip(uint32_t base, uint32_t region_size, uint32_t *out_end);

/*
 * Parse the program name from an on-disk UF2 file. Does not touch flash.
 * Uses random file seeks (storage_seek) to read just the few blocks needed:
 * one for the binary_info marker, one for the entry-pointer array, one or
 * two for entry structs, and one for the program-name string. Returns false
 * if anything looks wrong (unsupported family, sparse layout, marker not
 * found in the first few blocks, etc.) so the caller can fall back to the
 * filename-derived label.
 *
 * When `out_end` is not NULL and the name was found, it also receives the
 * absolute address one past the image's last byte (the end of the last program
 * block, as uf2_extent_from_file_family() reports it), read in the same open;
 * 0 when that could not be determined.
 */
bool program_name_from_uf2_file(const char *path, char *out, unsigned cap,
                                uint32_t *out_end);

#ifdef __cplusplus
}
#endif

#endif /* PROGRAM_NAME_H */
