/*
 * uf2_loader.c - see uf2_loader.h
 *
 * Strategy (two passes over the file):
 *   Pass 1  - validate every block and compute the exact flash address range
 *             the image occupies, so we erase only what we need. Skipped when
 *             the caller hands in the stats of a validation it just ran.
 *   (erase) - erase that range once: 64 KB blocks where the range covers a
 *             whole aligned block, 4 KB sectors at the ends.
 *   Pass 2  - program each in-range block, then read it back to verify.
 *
 * The pure per-block decision (in range? right family? where does it go?) lives
 * in uf2_classify_block() in uf2_format.h and is covered by host unit tests.
 */
#include "uf2_loader.h"

#include <string.h>
#include "boot_config.h"
#include "uf2_format.h"
#include "uf2_stream.h"
#include "flash_writer.h"
#include "hardware/flash.h" /* FLASH_PAGE_SIZE */

#ifndef EXPECTED_UF2_FAMILY
#define EXPECTED_UF2_FAMILY UF2_FAMILY_RP2350_ARM_S
#endif

/* Runtime upper bound for the app partition. Defaults to the build-time
 * APP_END_ADDR; main() narrows it to the real chip's capacity once the SD/PSRAM
 * init has run and storage_get_flash_capacity() is known good. */
static uint32_t g_app_end_addr = APP_END_ADDR;

void uf2_loader_set_app_end_addr(uint32_t app_end_addr)
{
    if (app_end_addr > APP_BASE_ADDR && app_end_addr <= APP_END_ADDR)
        g_app_end_addr = app_end_addr;
}

const char *uf2_load_result_str(uf2_load_result_t r)
{
    switch (r) {
    case UF2_LOAD_OK:                  return "OK";
    case UF2_LOAD_OPEN_FAILED:         return "could not open file";
    case UF2_LOAD_NO_MATCHING_BLOCKS:  return "no blocks for this chip/partition";
    case UF2_LOAD_READ_ERROR:          return "read error";
    case UF2_LOAD_BAD_FILE:            return "corrupt or misaligned UF2";
    case UF2_LOAD_VERIFY_FAILED:       return "flash verify mismatch";
    case UF2_LOAD_TOO_LARGE:           return "image exceeds available flash";
    case UF2_LOAD_WRONG_ADDRESS:       return "linked outside the app partition";
    default:                           return "unknown";
    }
}

/* Pass-1 walker: validate every block and populate st with the exact address
 * range the image occupies. Shared by uf2_validate_file_ex and uf2_load_file_ex.
 * Requires uf2_stream_open() to have been called by the caller; the caller also
 * owns closing the file on error/return. */
static uf2_load_result_t validate_pass(uint32_t region_base,
                                       uint32_t region_end,
                                       uint32_t expected_family,
                                       uf2_load_stats_t *st)
{
    const uf2_block_t *blk;
    int rc;
    while ((rc = uf2_stream_next(&blk)) == 1) {
        st->total_blocks++;
        uint32_t off, len;
        uf2_class_t c = uf2_classify_block(blk, expected_family,
                                           region_base, region_end, XIP_BASE,
                                           &off, &len);
        switch (c) {
        case UF2_CLS_PROGRAM: {
            if (off % FLASH_PAGE_SIZE != 0) return UF2_LOAD_BAD_FILE;
            uint32_t abs_lo = XIP_BASE + off;
            uint32_t abs_hi = abs_lo + len;
            if (abs_lo < st->lowest_addr)  st->lowest_addr  = abs_lo;
            if (abs_hi > st->highest_addr) st->highest_addr = abs_hi;
            st->programmed_blocks++;
            break;
        }
        case UF2_CLS_OUT_OF_RANGE:
            /* Past the region end: the image is too big for this partition.
             * Refuse rather than silently truncating. */
            if (blk->target_addr >= region_base) return UF2_LOAD_TOO_LARGE;

            /* Below region_base: this image was linked for the bootloader's
             * own area, i.e. built without -DBUILD_FOR_BOOTLOADER=ON. It used
             * to be a soft skip, which was only safe by accident -- a
             * standalone build SMALLER than the bootloader region ends up with
             * no in-range blocks at all and falls out below as
             * NO_MATCHING_BLOCKS. One that is LARGER straddles the boundary:
             * its low blocks were skipped, its tail classified as PROGRAM, and
             * the loader happily wrote that mid-image fragment to the start of
             * the partition, leaving an unbootable app (progress bar fills,
             * app_launch_present() then fails and reboots into the menu).
             * A correctly linked image never has a block down here, so reject
             * the whole file on the first one. */
            st->lowest_out_of_range = blk->target_addr;
            return UF2_LOAD_WRONG_ADDRESS;
        case UF2_CLS_SKIP:
            st->skipped_blocks++;
            break;
        case UF2_CLS_BAD_MAGIC:
        case UF2_CLS_BAD_PAYLOAD:
            return UF2_LOAD_BAD_FILE;
        }
    }
    if (rc < 0)                    return UF2_LOAD_READ_ERROR;
    if (st->programmed_blocks == 0) return UF2_LOAD_NO_MATCHING_BLOCKS;
    return UF2_LOAD_OK;
}

/* Stats a caller says came from validating this file with the same region and
 * family. Checked rather than trusted blindly: anything inconsistent and the
 * loader simply runs its own validation pass again. */
static bool stats_usable(const uf2_load_stats_t *v,
                         uint32_t region_base, uint32_t region_end)
{
    return v && v->programmed_blocks > 0 &&
           v->lowest_addr >= region_base && v->lowest_addr < v->highest_addr &&
           v->highest_addr <= region_end;
}

uf2_load_result_t uf2_load_file_ex(const char *name,
                                   uint32_t region_base,
                                   uint32_t region_end,
                                   uint32_t expected_family,
                                   const uf2_load_stats_t *validated,
                                   uf2_load_stats_t *stats,
                                   uf2_progress_cb progress)
{
    /* Defensive: never allow a caller to accidentally erase the bootloader
     * partition. region_base is externally supplied (e.g. from an aux UF2's
     * first block's target_addr), so clamp it here. */
    if (region_base < APP_BASE_ADDR) return UF2_LOAD_TOO_LARGE;
    if (region_end  > g_app_end_addr) region_end = g_app_end_addr;
    if (region_end  <= region_base)  return UF2_LOAD_TOO_LARGE;

    uf2_load_stats_t st = {0};
    st.lowest_addr = 0xFFFFFFFFu;

    if (!uf2_stream_open(name))
        return UF2_LOAD_OPEN_FAILED;

    /* ---------- Pass 1: validate + measure ---------- */
    if (stats_usable(validated, region_base, region_end)) {
        st = *validated;
    } else {
        uf2_load_result_t rr = validate_pass(region_base, region_end, expected_family, &st);
        if (rr != UF2_LOAD_OK) { uf2_stream_close(); return rr; }
        if (!uf2_stream_rewind()) { uf2_stream_close(); return UF2_LOAD_READ_ERROR; }
    }

    /* ---------- Erase exactly the range we will write ----------
     * A 64 KB block erase wherever the range covers a whole aligned block
     * (the SDK's flash_range_erase issues the D8h block command for those),
     * 4 KB sectors at the ends. A block takes ~150 ms against ~45 ms for each
     * of the 16 sectors it replaces. Erasing call by call, rather than the
     * whole range at once, keeps the progress callback firing so the bar
     * moves; it still counts 4 KB sectors, so a block advances it by 16. */
    {
        uint32_t off_lo  = st.lowest_addr  - XIP_BASE;
        uint32_t off_hi  = st.highest_addr - XIP_BASE;
        uint32_t sec_lo  = off_lo & ~(FLASH_SECTOR_SIZE - 1u);
        uint32_t sec_hi  = (off_hi + FLASH_SECTOR_SIZE - 1u) & ~(FLASH_SECTOR_SIZE - 1u);
        uint32_t total   = (sec_hi - sec_lo) / FLASH_SECTOR_SIZE;
        uint32_t done    = 0;
        if (progress) progress(UF2_PROGRESS_ERASE, 0, total);
        for (uint32_t off = sec_lo; off < sec_hi; ) {
            uint32_t len = FLASH_SECTOR_SIZE;
            if ((off & (FLASH_BLOCK_SIZE - 1u)) == 0 && sec_hi - off >= FLASH_BLOCK_SIZE)
                len = FLASH_BLOCK_SIZE;
            flash_writer_erase(off, len);
            off  += len;
            done += len / FLASH_SECTOR_SIZE;
            if (progress) progress(UF2_PROGRESS_ERASE, done, total);
        }
    }

    /* ---------- Pass 2: program + verify ---------- */
    const uf2_block_t *blk;
    int rc;
    uint32_t done = 0;
    while ((rc = uf2_stream_next(&blk)) == 1) {
        uint32_t off, len;
        if (uf2_classify_block(blk, expected_family,
                               region_base, region_end, XIP_BASE,
                               &off, &len) != UF2_CLS_PROGRAM)
            continue;

        /* Pad to a full 256-byte page (flash programs whole pages). */
        uint8_t page[FLASH_PAGE_SIZE];
        memset(page, 0xFF, sizeof(page));
        memcpy(page, blk->data, len);

        flash_writer_program(off, page, FLASH_PAGE_SIZE);
        if (!flash_writer_verify(off, page, FLASH_PAGE_SIZE)) {
            uf2_stream_close();
            return UF2_LOAD_VERIFY_FAILED;
        }
        if (progress) progress(UF2_PROGRESS_WRITE, ++done, st.programmed_blocks);
    }
    uf2_stream_close();
    if (rc < 0) return UF2_LOAD_READ_ERROR;

    if (stats) *stats = st;
    return UF2_LOAD_OK;
}

uf2_load_result_t uf2_validate_file_ex(const char *name,
                                       uint32_t region_base,
                                       uint32_t region_end,
                                       uint32_t expected_family,
                                       uf2_load_stats_t *stats)
{
    if (region_base < APP_BASE_ADDR) return UF2_LOAD_TOO_LARGE;
    if (region_end  > g_app_end_addr) region_end = g_app_end_addr;
    if (region_end  <= region_base)  return UF2_LOAD_TOO_LARGE;

    uf2_load_stats_t st = {0};
    st.lowest_addr = 0xFFFFFFFFu;

    if (!uf2_stream_open(name))
        return UF2_LOAD_OPEN_FAILED;

    uf2_load_result_t rr = validate_pass(region_base, region_end, expected_family, &st);
    uf2_stream_close();

    /* Copy the stats out even on failure: what the walk saw before it gave up
     * (notably lowest_out_of_range) is what the caller reports to the user. */
    if (stats) *stats = st;
    return rr;
}

uf2_load_result_t uf2_load_file(const char *name,
                                const uf2_load_stats_t *validated,
                                uf2_load_stats_t *stats,
                                uf2_progress_cb progress)
{
    return uf2_load_file_ex(name,
                            APP_BASE_ADDR, g_app_end_addr, EXPECTED_UF2_FAMILY,
                            validated, stats, progress);
}

uf2_load_result_t uf2_validate_file(const char *name, uf2_load_stats_t *stats)
{
    return uf2_validate_file_ex(name,
                                APP_BASE_ADDR, g_app_end_addr, EXPECTED_UF2_FAMILY,
                                stats);
}
