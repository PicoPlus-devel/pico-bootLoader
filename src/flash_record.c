/*
 * flash_record.c - see flash_record.h.
 */
#include "flash_record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ff.h"

static const char *const ROLE_TAG[FLASH_RECORD_COUNT] = { "emu", "aux" };

/* Parse one unsigned field terminated by ';'. Advances *p past the ';'. */
static bool parse_field(char **p, uint32_t max, uint32_t *out)
{
    char *end = NULL;
    unsigned long v = strtoul(*p, &end, 0);
    if (end == *p || *end != ';' || v > max) return false;
    *out = (uint32_t)v;
    *p = end + 1;
    return true;
}

/* Parse "<role>;<fsize>;<fdate>;<ftime>;<base>;<size>;<crc>;<filename>" into
 * out[role]. Lines that are not ours are ignored. */
static void parse_row(char *line, flash_records_t *out)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';

    int role = -1;
    for (int r = 0; r < FLASH_RECORD_COUNT; r++) {
        size_t tl = strlen(ROLE_TAG[r]);
        if (strncmp(line, ROLE_TAG[r], tl) == 0 && line[tl] == ';') {
            role = r;
            break;
        }
    }
    if (role < 0) return;

    char *p = line + strlen(ROLE_TAG[role]) + 1;
    uint32_t fsize, fdate, ftime, base, size, crc;
    if (!parse_field(&p, 0xFFFFFFFFu, &fsize)) return;
    if (!parse_field(&p, 0xFFFFu,     &fdate)) return;
    if (!parse_field(&p, 0xFFFFu,     &ftime)) return;
    if (!parse_field(&p, 0xFFFFFFFFu, &base))  return;
    if (!parse_field(&p, 0xFFFFFFFFu, &size))  return;
    if (!parse_field(&p, 0xFFFFFFFFu, &crc))   return;
    if (!p[0] || strlen(p) >= FLASH_RECORD_NAME_MAX || size == 0) return;

    flash_record_t *rec = &out->row[role];
    memset(rec, 0, sizeof(*rec));
    strcpy(rec->filename, p);
    rec->fsize         = fsize;
    rec->fdate         = (uint16_t)fdate;
    rec->ftime         = (uint16_t)ftime;
    rec->fp.image_base = base;
    rec->fp.image_size = size;
    rec->fp.crc        = crc;
    rec->valid         = true;
}

void flash_record_load(const char *path, flash_records_t *out)
{
    memset(out, 0, sizeof(*out));

    FIL fil;
    if (f_open(&fil, path, FA_READ) != FR_OK) return;

    /* A row longer than the buffer is not one we wrote: drop it, and the
     * continuation chunks f_gets hands back until its newline. */
    char line[192];
    bool skipping = false;
    while (f_gets(line, sizeof(line), &fil)) {
        size_t n = strlen(line);
        bool complete = (n > 0 && line[n - 1] == '\n') || f_eof(&fil);
        if (skipping || !complete) {
            skipping = !complete;
            continue;
        }
        if (line[0] == '#') continue;
        parse_row(line, out);
    }
    f_close(&fil);
}

bool flash_record_save(const char *path, const flash_records_t *in)
{
    FIL fil;
    if (f_open(&fil, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;

    /* Through f_write and snprintf, as sd_boot_ini.c does, so FatFs's own
     * formatter stays out of the image. */
    static const char header[] =
        "# Written by pico-bootLoader: what is in flash. Safe to delete.\n";
    UINT bw = 0;
    bool ok = f_write(&fil, header, sizeof(header) - 1, &bw) == FR_OK &&
              bw == sizeof(header) - 1;

    for (int r = 0; ok && r < FLASH_RECORD_COUNT; r++) {
        const flash_record_t *rec = &in->row[r];
        if (!rec->valid) continue;
        char line[192];
        int n = snprintf(line, sizeof(line), "%s;%lu;%u;%u;0x%08lX;%lu;0x%08lX;%s\n",
                         ROLE_TAG[r], (unsigned long)rec->fsize,
                         (unsigned)rec->fdate, (unsigned)rec->ftime,
                         (unsigned long)rec->fp.image_base,
                         (unsigned long)rec->fp.image_size,
                         (unsigned long)rec->fp.crc, rec->filename);
        if (n <= 0 || n >= (int)sizeof(line)) { ok = false; break; }
        ok = f_write(&fil, line, (UINT)n, &bw) == FR_OK && bw == (UINT)n;
    }

    if (f_close(&fil) != FR_OK) ok = false;
    if (!ok) f_unlink(path);   /* a torn file would only be ignored, but tidy up */
    return ok;
}

static bool stat_in_dir(const char *dir, const char *filename, FILINFO *fi)
{
    char full[FF_MAX_LFN + 1];
    int n = snprintf(full, sizeof(full), "%s/%s", dir, filename);
    if (n <= 0 || n >= (int)sizeof(full)) return false;
    return f_stat(full, fi) == FR_OK && !(fi->fattrib & AM_DIR);
}

bool flash_record_set(flash_record_t *rec, const char *dir, const char *filename,
                      const uf2_fingerprint_t *fp)
{
    memset(rec, 0, sizeof(*rec));
    if (!filename || strlen(filename) >= FLASH_RECORD_NAME_MAX) return false;
    if (!fp || fp->image_size == 0) return false;

    FILINFO fi;
    if (!stat_in_dir(dir, filename, &fi)) return false;
    if ((uint64_t)fi.fsize > 0xFFFFFFFFu) return false;

    strcpy(rec->filename, filename);
    rec->fsize = (uint32_t)fi.fsize;
    rec->fdate = (uint16_t)fi.fdate;
    rec->ftime = (uint16_t)fi.ftime;
    rec->fp    = *fp;
    rec->valid = true;
    return true;
}

bool flash_record_file_unchanged(const flash_record_t *rec, const char *dir,
                                 const char *filename)
{
    if (!rec->valid || !filename || strcmp(rec->filename, filename) != 0) return false;

    FILINFO fi;
    if (!stat_in_dir(dir, filename, &fi)) return false;
    return (uint64_t)fi.fsize == rec->fsize &&
           fi.fdate == rec->fdate &&
           fi.ftime == rec->ftime;
}

bool flash_record_equal(const flash_record_t *a, const flash_record_t *b)
{
    if (a->valid != b->valid) return false;
    if (!a->valid) return true;
    return a->fsize == b->fsize && a->fdate == b->fdate && a->ftime == b->ftime &&
           a->fp.image_base == b->fp.image_base &&
           a->fp.image_size == b->fp.image_size &&
           a->fp.crc == b->fp.crc &&
           strcmp(a->filename, b->filename) == 0;
}
