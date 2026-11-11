/*
 * sd_boot_ini.h - Parse /boot.txt on the SD root.
 *
 * File format (INI-style, optional). One key=value per line.
 * Lines starting with '#' or ';' are comments. Blank lines and CRLF tolerated.
 *
 *   BASEDIR=/uf2                  (default "/emu")
 *   INDEX=uf2list.txt             (default "emulators.txt")
 *   SCREENSAVER=STARFIELD         (BLOCKS or STARFIELD; default: see below)
 *   GUI=1                         (0 = text menu, 1 = graphical; default 1)
 *   THEME=0                       (active artwork theme 0..9; default 0)
 *   VIEW=APPS                     (CATEGORIES or APPS; default CATEGORIES)
 *   CATEGORY=Console              (last category; omitted when none)
 *   APP=picogenesisPlus           (last application; omitted when none)
 *   DISPLAY=DVI                   (HDMI or DVI; omitted = the board's default)
 *
 * BASEDIR is the SD-root folder under which the bootloader looks for the
 * per-HW_CONFIG subdir, the allow-list, and the assets folder.
 * INDEX is the filename of the allow-list inside BASEDIR. It is only consulted
 * when BASEDIR holds no categories.txt; with categories each one names its own
 * config file (see categories.h).
 * SCREENSAVER selects the idle visual mode.
 * DISPLAY selects the video signal on boards with HSTX video. It is parsed on
 * every board, so a card written on one board loads on any other; boards
 * without HSTX ignore it. Which mode an absent key means is the caller's
 * decision (it differs per board), so the parser only reports whether the
 * key was there.
 * GUI, THEME, VIEW, CATEGORY, APP and DISPLAY are written back by the
 * firmware -- see sd_boot_ini_save().
 *
 * Screensaver default is asymmetric:
 *   - When /boot.txt is absent altogether -> STARFIELD (historical default).
 *   - When /boot.txt is present but has no SCREENSAVER key -> BLOCKS.
 *   - Explicit SCREENSAVER= wins.
 * This is exactly why sd_boot_ini_save() writes SCREENSAVER= explicitly when it
 * has to create the file: materialising /boot.txt on a card that never had one
 * would otherwise silently flip the screensaver from STARFIELD to BLOCKS.
 *
 * Return values:
 *   SD_BOOT_INI_OK    - out[] is populated (either defaults, or parsed).
 *   SD_BOOT_INI_ERROR - a parse rule failed. err[] holds a single-line
 *                       diagnostic ready to display on a fatal screen.
 */
#ifndef SD_BOOT_INI_H
#define SD_BOOT_INI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SS_MODE_BLOCKS    = 0,   /* bouncing sprites */
    SS_MODE_STARFIELD = 1,   /* pin-hole depth projection */
} ss_mode_t;

typedef struct {
    char      base_dir[64];    /* no trailing slash, e.g. "/emu" */
    char      index_file[64];  /* bare filename, e.g. "emulators.txt" */
    ss_mode_t screensaver;
    bool      gui_graphical;   /* GUI=0|1;   default true (graphical) */
    uint8_t   theme;           /* THEME=0..9; default 0 */

    /* Where the user was when the picker last ran. Written back like GUI and
     * THEME. All three are matched by NAME rather than by position, so editing
     * or reordering categories.txt / a config file degrades to "first entry"
     * instead of selecting the wrong one. Empty = nothing remembered; the
     * parser rejects an empty VALUE, so an empty field is written by omitting
     * the key entirely. */
    bool      view_categories; /* VIEW=CATEGORIES|APPS; default CATEGORIES */
    char      category[32];    /* CATEGORY=<category_name> */
    char      app[32];         /* APP=<program_name> */

    /* DISPLAY=HDMI|DVI. display_set is false while the key is absent: the
     * board's default applies then, and the writer leaves the key out, so a
     * card moved between boards keeps each board's default until the user
     * picks a mode. Set display_set together with display_dvi to write it. */
    bool      display_dvi;     /* true = DVI, false = HDMI; valid if display_set */
    bool      display_set;

    /* Provenance, filled in by the loader. Not user-visible configuration --
     * the writer and the .guimode migration both need to know what was
     * actually present in the file as opposed to defaulted. */
    bool      file_present;
    bool      seen_gui;
    bool      seen_theme;
} sd_boot_ini_t;

typedef enum {
    SD_BOOT_INI_OK,
    SD_BOOT_INI_ERROR,
} sd_boot_ini_status_t;

/* Parse `path`. Also adopts a leftover "<path>.tmp" when `path` itself is
 * missing -- that is the one window sd_boot_ini_save() cannot close by
 * itself (a power cut between unlink and rename), and letting the file
 * silently revert to "absent" would re-fire the SCREENSAVER asymmetry. */
sd_boot_ini_status_t sd_boot_ini_load(const char *path,
                                      sd_boot_ini_t *out,
                                      char *err, size_t err_sz);

/* Rewrite `path` so GUI=, THEME=, VIEW=, CATEGORY=, APP= and DISPLAY= reflect
 * *ini.
 *
 * File present: only those six lines are rewritten, and keys that are absent
 *   get appended. Everything else -- comments, blank lines, ordering, spacing,
 *   CRLF, BASEDIR/INDEX/SCREENSAVER -- is copied through byte for byte.
 *   CATEGORY / APP with an empty value are dropped rather than written empty:
 *   the parser rejects an empty value. DISPLAY is dropped likewise while
 *   display_set is false.
 * File absent: the file is created carrying the EFFECTIVE value of every key,
 *   SCREENSAVER included (see the asymmetry note at the top of this header).
 *
 * The new file is built as "<path>.tmp", re-parsed with sd_boot_ini_load()
 * before being committed, and only then unlinked+renamed into place -- so a
 * formatting bug or a power cut can never leave /boot.txt in a state that
 * trips the BOOT.TXT INVALID fatal screen. On any failure the original file
 * is left untouched.
 *
 * Returns true on success. Callers should treat failure as non-fatal: a
 * write-protected or full card must not stop the picker from working. */
bool sd_boot_ini_save(const char *path, const sd_boot_ini_t *ini);

#ifdef __cplusplus
}
#endif

#endif /* SD_BOOT_INI_H */
