#include "gui.h"

#include <cstdio>
#include <cstring>

#include "FrensFonts.h"
#include "FrensHelpers.h"
#include "ff.h"
#include "image_convert.h"

namespace {
    uint16_t *s_buf_cur          = nullptr;
    uint16_t *s_buf_next         = nullptr;
    bool      s_next_is_half_res = false;   // true on SRAM-only configs
    const char *s_footer         = nullptr; // one-line button-hint overlay
    char      s_asset_dir[64]    = "/emu";  // overridable via gui_set_asset_dir()

    // Directory of the active artwork theme, and of theme 0 which acts as the
    // fallback when the active theme has no artwork for a key. Both are seeded
    // from s_asset_dir so gui works even if the theme setters are never called.
    char      s_theme_dir[80]    = "/emu/assets/themes/0";
    char      s_theme_fb_dir[80] = "/emu/assets/themes/0";

    // Subdirectory inside a theme, "" for the theme root. Application artwork
    // sits in the root; the category carousel sets this to "Categories".
    char      s_img_subdir[24]   = "";
}

// Half-resolution dimensions for the slide-only buffer used when there is
// no PSRAM. 160x120 = 38,400 bytes, 4x smaller than the full image.
#define GUI_HALF_W (SCREENWIDTH  / 2)
#define GUI_HALF_H (SCREENHEIGHT / 2)

// Footer band: one 8-pixel character row at the very bottom of the screen.
#define GUI_FOOTER_H   FONT_CHAR_HEIGHT                  // 8
#define GUI_FOOTER_Y0  (SCREENHEIGHT - GUI_FOOTER_H)     // 232

// Backend-specific 16-bit pixel encodings for the footer band -- same
// per-backend split main.cpp uses for the error overlay (RGB555 on HSTX,
// RGB444 packed 0x0RGB on PicoDVI).
#if HSTX
#define GUI_FOOTER_FG  0x7FFFu
#define GUI_FOOTER_BG  0x0000u
#else
#define GUI_FOOTER_FG  0x0FFFu
#define GUI_FOOTER_BG  0x0000u
#endif

extern "C" {

// Returns the saved mode. When the file is absent or unreadable the default
// is graphical -- that's the experience first-time users should land on.
// Read-only now: /boot.txt's GUI key owns the setting, and main.cpp calls this
// once to migrate a legacy card before deleting the file.
bool gui_load_mode(const char *path)
{
    FIL fil;
    if (f_open(&fil, path, FA_READ) != FR_OK) return true;
    char c = '1';
    UINT br = 0;
    f_read(&fil, &c, 1, &br);
    f_close(&fil);
    if (br != 1) return true;
    return c != '0';
}

void gui_set_footer(const char *text) { s_footer = text; }

void gui_set_asset_dir(const char *dir)
{
    if (!dir || !*dir) return;
    size_t n = strlen(dir);
    if (n >= sizeof(s_asset_dir)) n = sizeof(s_asset_dir) - 1;
    memcpy(s_asset_dir, dir, n);
    s_asset_dir[n] = '\0';

    // Keep the theme dirs consistent with the new base until the caller
    // overrides them with the theme it actually resolved.
    snprintf(s_theme_dir,    sizeof(s_theme_dir),    "%s/assets/themes/0", s_asset_dir);
    snprintf(s_theme_fb_dir, sizeof(s_theme_fb_dir), "%s/assets/themes/0", s_asset_dir);
}

void gui_set_theme_dir(const char *dir)
{
    if (!dir || !*dir) return;
    snprintf(s_theme_dir, sizeof(s_theme_dir), "%s", dir);
}

void gui_set_theme_fallback_dir(const char *dir)
{
    if (!dir || !*dir) { s_theme_fb_dir[0] = '\0'; return; }
    snprintf(s_theme_fb_dir, sizeof(s_theme_fb_dir), "%s", dir);
}

void gui_set_image_subdir(const char *sub)
{
    if (!sub || !*sub) { s_img_subdir[0] = '\0'; return; }
    snprintf(s_img_subdir, sizeof(s_img_subdir), "%s", sub);
}

bool gui_buffers_alloc(void)
{
    const size_t sz_full = SCREENWIDTH * SCREENHEIGHT * sizeof(uint16_t);
    if (!s_buf_cur)  s_buf_cur  = (uint16_t *)Frens::f_malloc(sz_full);
    if (!s_buf_cur) {
        printf("[bootLoader] gui: cur buffer alloc failed (size=%u)\n", (unsigned)sz_full);
        return false;
    }
    // Second buffer enables the slide animation. With PSRAM we mirror cur
    // at full resolution and the swap-pointers-after-slide pattern works.
    // Without PSRAM we'd OOM (215 KB base + 2x150 KB > 512 KB SRAM), so
    // allocate the next buffer at half res -- still enough to drive a
    // slide, just visibly chunkier. The caller reloads cur at full res
    // after the slide to restore the static-display sharpness.
    if (!s_buf_next) {
        if (Frens::isPsramEnabled()) {
            s_buf_next = (uint16_t *)Frens::f_malloc(sz_full);
            s_next_is_half_res = false;
        } else {
            const size_t sz_half = GUI_HALF_W * GUI_HALF_H * sizeof(uint16_t);
            s_buf_next = (uint16_t *)Frens::f_malloc(sz_half);
            s_next_is_half_res = true;
        }
    }
    if (!s_buf_next) {
        printf("[bootLoader] gui: slide buffer alloc failed; animation disabled\n");
    } else {
        printf("[bootLoader] gui: buffers ready (cur=full, next=%s)\n",
               s_next_is_half_res ? "half" : "full");
    }
    return true;
}

bool gui_next_is_half_res(void) { return s_next_is_half_res; }

uint16_t *gui_buf_cur(void)  { return s_buf_cur;  }
uint16_t *gui_buf_next(void) { return s_buf_next; }

void gui_swap_buffers(void)
{
    uint16_t *t = s_buf_cur;
    s_buf_cur  = s_buf_next;
    s_buf_next = t;
}

void gui_fill_solid(uint16_t *dest, uint16_t color)
{
    if (!dest) return;
    const size_t pixels = SCREENWIDTH * SCREENHEIGHT;
    for (size_t i = 0; i < pixels; i++) dest[i] = color;
}

void gui_fill_solid_half_res(uint16_t *dest, uint16_t color)
{
    if (!dest) return;
    const size_t pixels = GUI_HALF_W * GUI_HALF_H;
    for (size_t i = 0; i < pixels; i++) dest[i] = color;
}

// Open "<theme>[/<subdir>]/<image_key><FILEXTFORSEARCH>", converting a
// same-named PNG/JPG on demand (streaming, works on both PSRAM and SRAM-only
// builds). When the active theme has no artwork for this key, falls back to the
// theme-0 dir. Returns true with *fil open and positioned at offset 0.
//
// Both loaders below share this; before themes existed each of them built the
// same two paths inline, verbatim.
static bool gui_open_asset(const char *image_key, FIL *fil)
{
    const char *dirs[2] = { s_theme_dir, s_theme_fb_dir };

    // Sized from what can actually appear here rather than FF_MAX_LFN. This
    // runs on the deepest stack path in the loader -- gui_load_image_half_res
    // (a 640-byte scanline plus a ~560-byte FIL) calls it, and it in turn
    // calls image_convert_ensure(), which builds three FF_MAX_LFN paths of its
    // own. Two 256-byte buffers here cost more of the 3 KB stack than the
    // inputs can ever need: a theme dir is bounded by s_theme_dir[80], the
    // subdirectory by s_img_subdir[24], and an image_key by the 16-character
    // column-2 limit in the index file.
    char dir[sizeof(s_theme_dir) + sizeof(s_img_subdir)];       // 104
    char path[sizeof(dir) + 40];                                // + "/<key>.444"

    for (int i = 0; i < 2; i++) {
        if (!dirs[i] || !*dirs[i]) continue;
        if (i == 1 && strcmp(dirs[i], dirs[0]) == 0) break;   // fallback == active

        // The subdirectory has to be part of the directory handed to
        // image_convert_ensure(), not just of the path we open: that is what
        // makes a hand-built card with only .png in Categories/ convert in
        // place on first view.
        if (s_img_subdir[0]) snprintf(dir, sizeof(dir), "%s/%s", dirs[i], s_img_subdir);
        else                 snprintf(dir, sizeof(dir), "%s", dirs[i]);

        image_convert_ensure(dir, image_key, SCREENWIDTH, SCREENHEIGHT, /*letterbox=*/true);
        snprintf(path, sizeof(path), "%s/%s%s", dir, image_key, FILEXTFORSEARCH);
        if (f_open(fil, path, FA_READ) == FR_OK) return true;
    }

    printf("[bootLoader] gui: no artwork for '%s' in %s%s%s (or theme 0)\n",
           image_key, s_theme_dir, s_img_subdir[0] ? "/" : "", s_img_subdir);
    return false;
}

bool gui_load_image(const char *image_key, uint16_t *dest)
{
    if (!dest || !image_key || !*image_key) return false;

    FIL fil;
    if (!gui_open_asset(image_key, &fil)) return false;

    uint16_t hdr[2];
    UINT br = 0;
    FRESULT fr = f_read(&fil, hdr, sizeof(hdr), &br);
    if (fr != FR_OK || br != sizeof(hdr)) {
        printf("[bootLoader] gui: '%s' header read failed (fr=%d br=%u)\n",
               image_key, (int)fr, (unsigned)br);
        f_close(&fil);
        return false;
    }
    if (hdr[0] != SCREENWIDTH || hdr[1] != SCREENHEIGHT) {
        printf("[bootLoader] gui: '%s' is %u x %u, expected %d x %d\n",
               image_key, (unsigned)hdr[0], (unsigned)hdr[1], SCREENWIDTH, SCREENHEIGHT);
        f_close(&fil);
        return false;
    }

    const UINT want = SCREENWIDTH * SCREENHEIGHT * sizeof(uint16_t);
    fr = f_read(&fil, dest, want, &br);
    f_close(&fil);
    if (fr != FR_OK || br != want) {
        printf("[bootLoader] gui: '%s' pixel read short (fr=%d br=%u/%u)\n",
               image_key, (int)fr, (unsigned)br, (unsigned)want);
        return false;
    }
    return true;
}

// Load + downsample 2:1 in each axis on the fly. Reads the file scanline by
// scanline (640-byte stack scratch) so it never needs a large temporary
// buffer -- the half-res path exists exactly to avoid one of those.
bool gui_load_image_half_res(const char *image_key, uint16_t *dest)
{
    if (!dest || !image_key || !*image_key) return false;

    FIL fil;
    if (!gui_open_asset(image_key, &fil)) return false;

    uint16_t hdr[2];
    UINT br = 0;
    FRESULT fr = f_read(&fil, hdr, sizeof(hdr), &br);
    if (fr != FR_OK || br != sizeof(hdr) ||
        hdr[0] != SCREENWIDTH || hdr[1] != SCREENHEIGHT) {
        printf("[bootLoader] gui: '%s' header bad (fr=%d br=%u, %ux%u)\n",
               image_key, (int)fr, (unsigned)br, (unsigned)hdr[0], (unsigned)hdr[1]);
        f_close(&fil);
        return false;
    }

    uint16_t row[SCREENWIDTH];   // 640 bytes on the stack
    const UINT row_bytes = SCREENWIDTH * sizeof(uint16_t);

    for (int y_out = 0; y_out < GUI_HALF_H; y_out++) {
        // Read the even source row, downsample horizontally into dest.
        fr = f_read(&fil, row, row_bytes, &br);
        if (fr != FR_OK || br != row_bytes) {
            printf("[bootLoader] gui: '%s' short read row %d\n", image_key, 2 * y_out);
            f_close(&fil);
            return false;
        }
        uint16_t *out = dest + y_out * GUI_HALF_W;
        for (int x = 0; x < GUI_HALF_W; x++) out[x] = row[2 * x];

        // Read and discard the odd row (nearest-neighbour vertical decimation).
        fr = f_read(&fil, row, row_bytes, &br);
        if (fr != FR_OK || br != row_bytes) {
            printf("[bootLoader] gui: '%s' short read row %d\n", image_key, 2 * y_out + 1);
            f_close(&fil);
            return false;
        }
    }
    f_close(&fil);
    return true;
}

// Helper: write n pixels of b's scanline into dst, starting at b-column
// b_start. b is the full-resolution row at output-row y, so this is just
// a memcpy.
static inline void copy_b_full(uint16_t *dst, const uint16_t *brow,
                               int b_start, int n)
{
    memcpy(dst, brow + b_start, n * sizeof(uint16_t));
}

// Helper: write n pixels of half-res b into dst, starting at output-column
// b_start in 320-px space. b's row is shared between two output rows
// (y/2) and each b pixel covers two output columns. Each output column
// x reads brow_half[x / 2].
static inline void copy_b_half(uint16_t *dst, const uint16_t *brow_half,
                               int b_start, int n)
{
    for (int i = 0; i < n; i++) {
        dst[i] = brow_half[(b_start + i) >> 1];
    }
}

// Paint one scanline of the footer band on top of whatever compose_row()
// just wrote into dst. y is in [GUI_FOOTER_Y0, SCREENHEIGHT). Fills the
// whole row with bg so the band is opaque, then overlays glyph pixels from
// the shared 8x8 font (same pattern as menu.cpp's charcell pipeline).
static void overlay_footer_row(uint16_t *dst, int y, const char *text)
{
    const uint16_t fg = GUI_FOOTER_FG;
    const uint16_t bg = GUI_FOOTER_BG;

    for (int x = 0; x < SCREENWIDTH; x++) dst[x] = bg;

    const int len      = (int)strlen(text);
    const int text_w   = len * FONT_CHAR_WIDTH;
    const int x_start  = (SCREENWIDTH - text_w) / 2;
    const int row_in_g = y - GUI_FOOTER_Y0;   // 0..7 within the glyph

    if (x_start < 0 || text_w > SCREENWIDTH) return;   // string too wide

    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < FONT_FIRST_ASCII || c >= FONT_FIRST_ASCII + FONT_N_CHARS)
            c = '?';
        char slice = getcharslicefrom8x8font((char)c, row_in_g);
        uint16_t *out = dst + x_start + i * FONT_CHAR_WIDTH;
        for (int bit = 0; bit < 8; bit++) {
            out[bit] = (slice & 1) ? fg : bg;
            slice >>= 1;
        }
    }
}

// Per-scanline composition. `a` is always full 320x240. `b` may be NULL,
// 320x240 (b_half_res=false), or 160x120 (b_half_res=true).
static inline void compose_row(uint16_t *dst, int y,
                               const uint16_t *a, const uint16_t *b,
                               int p, int dir, bool b_half_res)
{
    const uint16_t *arow = a + y * SCREENWIDTH;

    if (!b || p <= 0 || dir == 0) {
        memcpy(dst, arow, SCREENWIDTH * sizeof(uint16_t));
        return;
    }

    // Vertical slides pick a whole row from one image or the other -- there is
    // no split within a row, unlike the horizontal case below. `src_y` is the
    // row to take, in the 240-row output space; the half-res expansion is the
    // same >>1 the horizontal path uses, just applied to the row index too.
    if (dir == GUI_SLIDE_UP || dir == GUI_SLIDE_DOWN) {
        if (p > SCREENHEIGHT) p = SCREENHEIGHT;
        bool from_b;
        int  src_y;
        if (dir == GUI_SLIDE_UP) {
            // b enters along the bottom edge; a scrolls off the top.
            from_b = (y >= SCREENHEIGHT - p);
            src_y  = from_b ? y - (SCREENHEIGHT - p) : y + p;
        } else {
            // b enters along the top edge; a scrolls off the bottom.
            from_b = (y < p);
            src_y  = from_b ? SCREENHEIGHT - p + y : y - p;
        }
        if (!from_b) {
            memcpy(dst, a + src_y * SCREENWIDTH, SCREENWIDTH * sizeof(uint16_t));
        } else if (b_half_res) {
            copy_b_half(dst, b + (src_y >> 1) * GUI_HALF_W, 0, SCREENWIDTH);
        } else {
            copy_b_full(dst, b + src_y * SCREENWIDTH, 0, SCREENWIDTH);
        }
        return;
    }

    // Locate b's row for this output scanline.
    const uint16_t *brow = b_half_res
        ? (b + (y >> 1) * GUI_HALF_W)
        : (b + y * SCREENWIDTH);

    if (p >= SCREENWIDTH) {
        // All b.
        if (b_half_res) copy_b_half(dst, brow, 0, SCREENWIDTH);
        else            copy_b_full(dst, brow, 0, SCREENWIDTH);
    } else if (dir > 0) {
        // b slides in from the right. Old image scrolls left.
        // dst = arow[p .. W-1] then b mapped to columns 0 .. p-1.
        memcpy(dst, arow + p, (SCREENWIDTH - p) * sizeof(uint16_t));
        if (b_half_res) copy_b_half(dst + (SCREENWIDTH - p), brow, 0, p);
        else            copy_b_full(dst + (SCREENWIDTH - p), brow, 0, p);
    } else {
        // b slides in from the left. Old image scrolls right.
        // dst = b mapped to columns W-p .. W-1 then arow[0 .. W-1-p].
        if (b_half_res) copy_b_half(dst, brow, SCREENWIDTH - p, p);
        else            copy_b_full(dst, brow, SCREENWIDTH - p, p);
        memcpy(dst + p, arow, (SCREENWIDTH - p) * sizeof(uint16_t));
    }
}

int gui_slide_extent(int direction)
{
    switch (direction) {
    case GUI_SLIDE_UP:
    case GUI_SLIDE_DOWN:  return SCREENHEIGHT;
    case GUI_SLIDE_LEFT:
    case GUI_SLIDE_RIGHT: return SCREENWIDTH;
    default:              return 0;
    }
}

void gui_draw_frame(const uint16_t *a, const uint16_t *b,
                    int slide_px, int direction, bool b_half_res)
{
    if (!a) return;
    // Clamp along the slide's own axis: a vertical slide runs to SCREENHEIGHT,
    // which is shorter than SCREENWIDTH, so clamping to the width would let it
    // overrun.
    const int extent = gui_slide_extent(direction);
    if (slide_px < 0) slide_px = 0;
    if (extent && slide_px > extent) slide_px = extent;

    for (int y = 0; y < SCREENHEIGHT; y++) {
        uint16_t *dst;
#if !HSTX
        dvi::DVI::LineBuffer *lb = nullptr;
#if FRAMEBUFFERISPOSSIBLE
        if (Frens::isFrameBufferUsed()) {
            dst = &Frens::framebuffer[y * SCREENWIDTH];
        } else
#endif
        {
            lb  = dvi_->getLineBuffer();
            dst = lb->data();
        }
#else
        dst = hstx_getlineFromFramebuffer(y);
#endif

        compose_row(dst, y, a, b, slide_px, direction, b_half_res);

        // Footer band: composed into dst before the line-stream push so
        // every backend (HSTX FB, PicoDVI FB, PicoDVI line-stream) gets it.
        if (s_footer && *s_footer && y >= GUI_FOOTER_Y0) {
            overlay_footer_row(dst, y, s_footer);
        }

#if !HSTX
#if FRAMEBUFFERISPOSSIBLE
        if (!Frens::isFrameBufferUsed())
#endif
            dvi_->setLineBuffer(y, lb);
#endif
    }
}

} // extern "C"
