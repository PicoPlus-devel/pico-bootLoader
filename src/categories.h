/*
 * categories.h - Parse /emu/categories.txt on the SD card.
 *
 * Optional. When the file exists the picker gains a level above the
 * application carousel: the user browses categories first and enters one to see
 * the applications it holds. When it is absent the picker behaves exactly as it
 * always has, driven by the single INDEX file from /boot.txt.
 *
 * File format: one record per line, three fields separated by ';':
 *
 *   <category_name>;<image_key>;<config_file>
 *
 * Example:
 *   Console;console;console.txt
 *   Settings;settings;
 *
 * - category_name is the label shown in the text menu. The artwork carries its
 *   own label, so the graphical carousel does not draw it.
 * - image_key picks the artwork under
 *   /emu/assets/themes/<N>/Categories/<key>.444 (PicoDVI) or .555 (HSTX).
 *   Note the Categories subfolder -- application artwork sits in the theme root.
 * - config_file is a bare filename inside BASEDIR listing the applications of
 *   this category. It uses the emulators.txt format (see emulators_txt.h) and
 *   is read with the same parser.
 * - An EMPTY config_file marks the options row: entering it opens the same
 *   screen SELECT opens instead of an application list.
 *
 * Rows are kept in file order -- the carousel shows exactly what the user
 * wrote, including categories that turn out to hold nothing (entering one of
 * those puts a message on screen).
 *
 * Lines starting with '#' and blank lines are ignored. CR is tolerated.
 */
#ifndef CATEGORIES_H
#define CATEGORIES_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Read and parse the file. Safe to call once at startup. Returns false if the
 * file is missing, unreadable or holds no usable row -- in that case the picker
 * stays single-level. */
bool categories_load(const char *path);

int  categories_count(void);

/* Accessors by row index, in file order. All return "" when the index is out
 * of range, so callers never have to NULL-check. */
const char *categories_name(int i);
const char *categories_image_key(int i);
const char *categories_config(int i);      /* "" = the options row */

/* Index of the first row whose name matches (case-insensitive), or -1.
 * Used to restore /boot.txt's CATEGORY= key by name rather than by position,
 * so editing categories.txt cannot silently select the wrong row. */
int  categories_find(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* CATEGORIES_H */
