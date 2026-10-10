/*
 * flash_record.h - What the loader last wrote to flash, kept on the SD card so
 *                  a boot can tell "flash still matches the card" without
 *                  reading the whole .uf2.
 *
 * Without it, every boot CRCs the in-flash program's .uf2 end to end -- one
 * single-sector read per 512-byte UF2 block, thousands of them for a large
 * emulator -- and every A-press on an entry with an aux blob does the same for
 * the aux file.
 *
 * File: <BASEDIR>/<HW_CONFIG>/.flashed, written by the loader only. One row per
 * role; the filename comes last so it may contain ';':
 *
 *   emu;<fsize>;<fdate>;<ftime>;<image_base>;<image_size>;<crc>;<filename>
 *   aux;<fsize>;<fdate>;<ftime>;<image_base>;<image_size>;<crc>;<filename>
 *
 * fsize/fdate/ftime are the FatFs FILINFO fields of the .uf2 at the time the
 * row was written; image_base/image_size/crc are its uf2_fingerprint_t.
 *
 * A row is only a hint. The caller trusts it when the file still has the same
 * size and timestamp AND the flash range it names still CRCs to the recorded
 * value -- a flash-only check, no SD reads. Anything else (no file, a corrupt
 * row, another file, a newer copy, a card moved to a board holding a different
 * build) falls back to the full CRC walk. Deleting the file is always safe.
 */
#ifndef FLASH_RECORD_H
#define FLASH_RECORD_H

#include <stdbool.h>
#include <stdint.h>

#include "uf2_crc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FLASH_RECORD_FILE      ".flashed"
#define FLASH_RECORD_NAME_MAX  80

typedef enum {
    FLASH_RECORD_EMU = 0,   /* the application image at APP_BASE_ADDR */
    FLASH_RECORD_AUX,       /* an aux blob (emulators.txt column 4)   */
    FLASH_RECORD_COUNT
} flash_record_role_t;

typedef struct {
    bool              valid;
    char              filename[FLASH_RECORD_NAME_MAX];  /* basename in the config dir */
    uint32_t          fsize;
    uint16_t          fdate;
    uint16_t          ftime;
    uf2_fingerprint_t fp;
} flash_record_t;

typedef struct {
    flash_record_t row[FLASH_RECORD_COUNT];
} flash_records_t;

/* Read `path`. A missing file, or a row that does not parse, leaves that role
 * invalid; this never fails as a whole. */
void flash_record_load(const char *path, flash_records_t *out);

/* Write the valid rows to `path`, replacing it. False on any write error; the
 * caller carries on, the next boot just takes the slow path. */
bool flash_record_save(const char *path, const flash_records_t *in);

/* Fill `rec` for <dir>/<filename> from f_stat plus the given fingerprint.
 * False (and rec->valid = false) when the file cannot be stat'ed or the name
 * does not fit. */
bool flash_record_set(flash_record_t *rec, const char *dir, const char *filename,
                      const uf2_fingerprint_t *fp);

/* True when `rec` is valid, names `filename`, and <dir>/<filename> still has
 * the recorded size, date and time. Says nothing about flash -- the caller
 * compares rec->fp against an XIP CRC. */
bool flash_record_file_unchanged(const flash_record_t *rec, const char *dir,
                                 const char *filename);

/* True when both rows hold the same content (both invalid counts as equal),
 * so a caller can skip rewriting an unchanged file. */
bool flash_record_equal(const flash_record_t *a, const flash_record_t *b);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_RECORD_H */
