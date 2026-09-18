/*
 * emulators_txt.h - Parse /emu/emulators.txt on the SD card.
 *
 * File format: one record per line, three or four fields separated by ';':
 *
 *   <program_name>;<image_key>;<display_name>[;<aux_uf2>]
 *
 * Example (three columns):
 *   picogenesisPlus;md;Sega Genesis/Mega Drive
 *
 * Example (four columns - emulator with a companion data blob):
 *   doom_tiny;doom;Doom!;doom1-whx.uf2
 *
 * - program_name matches the value extracted from each UF2's binary_info
 *   (see program_name.h). Comparison is case-insensitive.
 * - image_key picks the artwork file under /emu/assets/themes/<N>/<key>.444 (PicoDVI)
 *   or .555 (HSTX).
 * - display_name is the human-readable label shown in the text menu.
 * - aux_uf2 (optional) names a second .uf2 in the same config dir that
 *   carries read-only data at its own target address (e.g. a Doom WAD
 *   packed as a DATA-family UF2). The bootloader flashes it alongside the
 *   emulator .uf2 if the in-flash bytes drift, else skips.
 *
 * Lines starting with '#' and blank lines are ignored. CR is tolerated.
 */
#ifndef EMULATORS_TXT_H
#define EMULATORS_TXT_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read and parse the file. Returns false if the file is missing or unreadable;
 * in that case lookups will always miss. Called once at startup for the INDEX
 * file, and again for each category's config file as the user enters it -- the
 * rows of the previously loaded file are discarded on every call. */
bool emulators_txt_load(const char *path);

/* Number of rows in the currently loaded file. */
int emulators_txt_count(void);

/* Read row `i` (0-based, in FILE ORDER). This is what the picker builds its
 * list from: the display order is the order the user wrote, not the order
 * FatFs happened to return the .uf2 files in. Any output pointer may be NULL;
 * every buffer written is NUL-terminated. Returns false when i is out of range. */
bool emulators_txt_row(int i,
                       char *prog_name, size_t prog_sz,
                       char *image_key, size_t key_sz,
                       char *display_name, size_t name_sz,
                       char *aux_uf2, size_t aux_sz);

/* Look up by program_name. Writes the matching image_key, display_name, and
 * (optional) aux_uf2 into the caller's buffers (always NUL-terminated;
 * aux_uf2 becomes "" if the row has no 4th column). Any of the output
 * pointers may be NULL. Returns true on match. */
bool emulators_txt_lookup(const char *prog_name,
                          char *image_key, size_t key_sz,
                          char *display_name, size_t name_sz,
                          char *aux_uf2, size_t aux_sz);

#ifdef __cplusplus
}
#endif

#endif /* EMULATORS_TXT_H */
