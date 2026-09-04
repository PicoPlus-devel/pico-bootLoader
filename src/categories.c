#include "categories.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

#include "ff.h"

#define MAX_CATEGORIES    16
#define CAT_NAME_MAX      32
#define CAT_IMAGE_KEY_MAX 16
#define CAT_CONFIG_MAX    64

typedef struct {
    char name[CAT_NAME_MAX];
    char image_key[CAT_IMAGE_KEY_MAX];
    char config_file[CAT_CONFIG_MAX];   /* empty = the options row */
} cat_t;

static cat_t s_cats[MAX_CATEGORIES];
static int   s_count = 0;

/* Same trim / copy_field / parse_line shape as emulators_txt.c on purpose:
 * the two files parse sibling formats and should read as one family. */
static void trim(char *s)
{
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = '\0';
}

static void copy_field(char *dst, size_t cap, const char *src)
{
    if (cap == 0) return;
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    trim(dst);
}

static void parse_line(const char *line)
{
    if (s_count >= MAX_CATEGORIES) return;

    char buf[256];
    size_t n = strlen(line);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, line, n);
    buf[n] = '\0';
    trim(buf);
    if (buf[0] == '\0' || buf[0] == '#') return;

    char *s1 = strchr(buf, ';');
    if (!s1) return;
    *s1++ = '\0';
    char *s2 = strchr(s1, ';');
    if (!s2) return;
    *s2++ = '\0';

    /* The config_file field is allowed to be empty (the options row), so
     * unlike emulators.txt only the first two fields are required. */
    cat_t *c = &s_cats[s_count];
    copy_field(c->name,        sizeof(c->name),        buf);
    copy_field(c->image_key,   sizeof(c->image_key),   s1);
    copy_field(c->config_file, sizeof(c->config_file), s2);
    if (c->name[0]) s_count++;
}

bool categories_load(const char *path)
{
    s_count = 0;

    FIL fil;
    FRESULT fr = f_open(&fil, path, FA_READ);
    if (fr != FR_OK) {
        /* Absent is the normal case for a card that predates categories --
         * log it as information, not as a failure. */
        printf("[bootLoader] categories: %s not present (fr=%d); single-level picker\n",
               path, fr);
        return false;
    }

    char line[256];
    while (f_gets(line, sizeof(line), &fil)) {
        parse_line(line);
    }
    f_close(&fil);

    printf("[bootLoader] categories: %d row(s) loaded from %s\n", s_count, path);
    for (int i = 0; i < s_count; i++) {
        printf("[bootLoader]   [%2d] %-20s img=\"%s\" config=\"%s\"%s\n",
               i, s_cats[i].name, s_cats[i].image_key, s_cats[i].config_file,
               s_cats[i].config_file[0] ? "" : "  (options row)");
    }
    return s_count > 0;
}

int categories_count(void) { return s_count; }

static bool in_range(int i) { return i >= 0 && i < s_count; }

const char *categories_name(int i)      { return in_range(i) ? s_cats[i].name        : ""; }
const char *categories_image_key(int i) { return in_range(i) ? s_cats[i].image_key   : ""; }
const char *categories_config(int i)    { return in_range(i) ? s_cats[i].config_file : ""; }

int categories_find(const char *name)
{
    if (!name || !*name) return -1;
    for (int i = 0; i < s_count; i++) {
        if (strcasecmp(s_cats[i].name, name) == 0) return i;
    }
    return -1;
}
