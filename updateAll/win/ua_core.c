/*
 * updateAll core: a port of updateAll.py, function by function, so that the
 * Windows program behaves and reports exactly as the scripts do. See
 * updateAll.py for the reasoning behind each rule; the comments here only
 * cover what is specific to this port.
 */
#include "ua_core.h"
#include "ua_plat.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "third_party/cJSON/cJSON.h"
#include "third_party/miniz/miniz.h"

#define DOWNLOAD_ATTEMPTS 3
#define TIMEOUT 60
#define CONFIG_TIMEOUT 15
#define RELEASE_MARKER "emu/release.txt"
#define OWN_CONFIG "updateAll/updateAll.json"

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

static void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        abort();
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p)
        abort();
    return p;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

static char *vfmt(const char *f, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    char probe[1];
    int n = vsnprintf(probe, sizeof probe, f, ap);
    char *s = xmalloc((size_t)n + 1);
    vsnprintf(s, (size_t)n + 1, f, ap2);
    va_end(ap2);
    return s;
}

static char *fmt(const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    char *s = vfmt(f, ap);
    va_end(ap);
    return s;
}

static void set_err(char *err, size_t errlen, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    vsnprintf(err, errlen, f, ap);
    va_end(ap);
}

/* A growable, NULL-terminated list of owned strings. */
typedef struct {
    char **v;
    int n, cap;
} strv;

static void strv_push(strv *s, char *owned)
{
    if (s->n + 2 > s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->v = xrealloc(s->v, (size_t)s->cap * sizeof *s->v);
    }
    s->v[s->n++] = owned;
    s->v[s->n] = NULL;
}

static void strv_free(strv *s)
{
    for (int i = 0; i < s->n; i++)
        free(s->v[i]);
    free(s->v);
    memset(s, 0, sizeof *s);
}

static char *strv_join(char *const *v, int n, const char *sep)
{
    size_t len = 1;
    for (int i = 0; i < n; i++)
        len += strlen(v[i]) + strlen(sep);
    char *s = xmalloc(len);
    s[0] = 0;
    for (int i = 0; i < n; i++) {
        if (i)
            strcat(s, sep);
        strcat(s, v[i]);
    }
    return s;
}

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int ieq(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
        a++, b++;
    return *a == *b;
}

static int iends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    return ls >= lx && ieq(s + ls - lx, suffix);
}

static int bad_name(const char *name)
{
    return !name[0] || !strcmp(name, ".") || !strcmp(name, "..") ||
           strpbrk(name, "/\\:*?\"<>|") != NULL;
}

/* Whether a part of a path with "/" between its folders is a bad name. */
static int bad_path(const char *name)
{
    int bad = 0;
    for (const char *p = name;; p++) {
        size_t n = strcspn(p, "/");
        char *part = fmt("%.*s", (int)n, p);
        bad |= bad_name(part);
        free(part);
        p += n;
        if (!*p)
            return bad;
    }
}

int ua_is_url(const char *s)
{
    const char *p = "http";
    while (*p && lower((unsigned char)*s) == *p)
        s++, p++;
    if (*p)
        return 0;
    if (lower((unsigned char)*s) == 's')
        s++;
    return !strncmp(s, "://", 3);
}

static int is_abs(const char *p)
{
#ifdef _WIN32
    if (p[0] && p[1] == ':' && (p[2] == '\\' || p[2] == '/'))
        return 1;
#endif
    return p[0] == '/' || p[0] == UA_SEP;
}

/* os.path.join of two components. */
static char *path_join(const char *a, const char *b)
{
    static const char sep[2] = { UA_SEP, 0 };
    size_t la = strlen(a);
    int need = la && a[la - 1] != '/' && a[la - 1] != UA_SEP;
#ifdef _WIN32
    if (la == 2 && a[1] == ':')
        need = 0;    /* "E:" + "x" is "E:x", as in ntpath.join */
#endif
    return fmt("%s%s%s", a, need ? sep : "", b);
}

/* The netloc of a URL, as urllib.parse.urlsplit finds it: from after "://"
   to the first "/", "?" or "#". *len gets its length. */
static const char *url_netloc(const char *url, size_t *len)
{
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    *len = strcspn(p, "/?#");
    return p;
}

static int hex_digit(int c)
{
    return c >= '0' && c <= '9' ? c - '0' : (c = lower(c)) >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* unquote(urlsplit(url).path.rsplit("/", 1)[-1]). */
static char *url_file_name(const char *url)
{
    size_t n;
    const char *path = url_netloc(url, &n) + n;
    size_t plen = strcspn(path, "?#");
    const char *last = path;
    for (size_t i = 0; i < plen; i++)
        if (path[i] == '/')
            last = path + i + 1;
    n = (size_t)(path + plen - last);
    char *name = xmalloc(n + 1), *o = name;
    for (size_t i = 0; i < n; i++) {
        int hi, lo;
        if (last[i] == '%' && i + 2 < n && (hi = hex_digit((unsigned char)last[i + 1])) >= 0 &&
            (lo = hex_digit((unsigned char)last[i + 2])) >= 0) {
            *o++ = (char)(hi * 16 + lo);
            i += 2;
        } else {
            *o++ = last[i];
        }
    }
    *o = 0;
    return name;
}

/* urlsplit(url).hostname, or the URL itself when it has none. */
static char *url_host(const char *url)
{
    size_t n;
    const char *p = url_netloc(url, &n);
    for (size_t i = n; i > 0; i--) {
        if (p[i - 1] == '@') {
            n -= i;
            p += i;
            break;
        }
    }
    size_t hn = 0;
    while (hn < n && p[hn] != ':')
        hn++;
    if (!hn)
        return xstrdup(url);
    char *host = fmt("%.*s", (int)hn, p);
    for (char *s = host; *s; s++)
        *s = (char)lower((unsigned char)*s);
    return host;
}

static const char *base_name(const char *p)
{
    const char *b = p;
    for (const char *s = p; *s; s++)
        if (*s == '/' || *s == UA_SEP)
            b = s + 1;
    return b;
}

/* The text of an OSError, as Python shows it. */
static char *os_error(int err, const char *path)
{
    return fmt("[Errno %d] %s: '%s'", err, strerror(err), path);
}

static int is_cancelled(const ua_hooks *h)
{
    return h->cancelled && h->cancelled(h->ctx);
}

static void out(const ua_hooks *h, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    char *s = vfmt(f, ap);
    va_end(ap);
    h->log(h->ctx, 0, s);
    free(s);
}

static void out_err(const ua_hooks *h, const char *f, ...)
{
    va_list ap;
    va_start(ap, f);
    char *s = vfmt(f, ap);
    va_end(ap);
    h->log(h->ctx, 1, s);
    free(s);
}

static void file_event(const ua_hooks *h, const char *set, const char *file, int state,
                       const char *detail)
{
    if (h->on_file)
        h->on_file(h->ctx, set, file, state, detail);
}

static int read_file(const char *path, char **data, size_t *len, char **err)
{
    FILE *f = ua_fopen(path, "rb");
    if (!f) {
        *err = os_error(errno, path);
        return -1;
    }
    size_t cap = 65536, n = 0, got;
    char *buf = xmalloc(cap + 1);
    while ((got = fread(buf + n, 1, cap - n, f)) > 0) {
        n += got;
        if (n == cap)
            buf = xrealloc(buf, (cap *= 2) + 1);
    }
    int bad = ferror(f);
    fclose(f);
    if (bad) {
        free(buf);
        *err = os_error(EIO, path);
        return -1;
    }
    buf[n] = 0;
    *data = buf;
    *len = n;
    return 0;
}

/* Where cJSON stopped, as "line L column C". */
static char *json_where(const char *text, const char *at)
{
    int line = 1, col = 1;
    for (const char *p = text; p && at && p < at && *p; p++) {
        if (*p == '\n')
            line++, col = 1;
        else
            col++;
    }
    return fmt("invalid JSON at line %d column %d", line, col);
}

static cJSON *parse_json(const char *text, size_t len, char **err)
{
    if (len >= 3 && !memcmp(text, "\xEF\xBB\xBF", 3))
        text += 3, len -= 3;
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(text, len, &end, 0);
    if (!root)
        *err = json_where(text, end);
    return root;
}

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/* ------------------------------------------------------------------------- */

/* int(x) of a JSON value: a number, or a string holding a decimal number. */
static int parse_int(const cJSON *v, long long *out)
{
    if (cJSON_IsNumber(v)) {
        *out = (long long)v->valuedouble;
        return 0;
    }
    if (cJSON_IsString(v)) {
        char *end;
        errno = 0;
        long long n = strtoll(v->valuestring, &end, 10);
        while (*end == ' ' || *end == '\t')
            end++;
        if (!errno && end != v->valuestring && !*end) {
            *out = n;
            return 0;
        }
    }
    return -1;
}

/* int(x, 16) of a string; "0x" is allowed, as in Python. */
static int parse_crc(const cJSON *v, uint32_t *out)
{
    if (!cJSON_IsString(v))
        return -1;
    const char *s = v->valuestring;
    while (*s == ' ' || *s == '\t')
        s++;
    if (s[0] == '0' && lower(s[1]) == 'x')
        s += 2;
    char *end;
    errno = 0;
    unsigned long long n = strtoull(s, &end, 16);
    while (*end == ' ' || *end == '\t')
        end++;
    if (errno || end == s || *end || *s == '-' || *s == '+' || n > 0xFFFFFFFFull)
        return -1;
    *out = (uint32_t)n;
    return 0;
}

static const char *str_field(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

enum { KIND_CORES, KIND_DATA, KIND_BIOS };

/* One entry of "cores", "data" or "bios", as kind says; a BIOS entry has the
   form of game data. */
static int parse_core(const cJSON *d, int kind, ua_core *c, strv *ids, const char *path,
                      char *err, size_t errlen)
{
    int is_data = kind != KIND_CORES;
    const char *cid = str_field(d, "id");
    if (!cid)
        cid = "?";
    const char *const keys[] = { "id", "name", is_data ? "url" : "zips", "destination", "files" };
    for (int k = 0; k < 5; k++) {
        if (!cJSON_IsObject(d) || !cJSON_GetObjectItemCaseSensitive(d, keys[k])) {
            set_err(err, errlen, "%s: core '%s' has no '%s'", path, cid, keys[k]);
            return -1;
        }
    }
    const char *name = str_field(d, "name"), *destination = str_field(d, "destination");
    const cJSON *zips = cJSON_GetObjectItemCaseSensitive(d, "zips");
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(d, "files");
    if (!str_field(d, "id") || !name || !destination || (!is_data && !cJSON_IsArray(zips)) ||
        !cJSON_IsArray(files)) {
        set_err(err, errlen, "%s: core '%s': 'id', 'name' and 'destination' must be "
                "strings, 'zips' and 'files' lists", path, cid);
        return -1;
    }
    if (ieq(cid, "bios") || ieq(cid, "sdcard")) {
        set_err(err, errlen, "%s: core '%s' has a reserved id", path, cid);
        return -1;
    }
    for (int i = 0; i < ids->n; i++) {
        if (ieq(ids->v[i], cid)) {
            set_err(err, errlen, "%s: core '%s' is defined twice", path, cid);
            return -1;
        }
    }
    strv_push(ids, xstrdup(cid));
    c->id = xstrdup(cid);
    c->name = xstrdup(name);
    c->set = xstrdup(str_field(d, "set") ? str_field(d, "set") : "");
    c->bios = kind == KIND_BIOS;

    strv zv = { 0 };
    if (is_data) {
        /* The file is kept in the zip folder under the last part of the URL. */
        const char *url = str_field(d, "url");
        char *zname = url && ua_is_url(url) ? url_file_name(url) : xstrdup("");
        if (bad_name(zname)) {
            set_err(err, errlen, "%s: core '%s' has an invalid url '%s'", path, cid,
                    url ? url : "");
            free(zname);
            return -1;
        }
        strv_push(&zv, zname);
        const cJSON *archives = cJSON_GetObjectItemCaseSensitive(d, "archives");
        strv av = { 0 };
        int ok = !archives || cJSON_IsArray(archives);
        const cJSON *a;
        if (archives && ok) {
            cJSON_ArrayForEach(a, archives) {
                if (!cJSON_IsString(a) || bad_name(a->valuestring)) {
                    ok = 0;
                    break;
                }
                strv_push(&av, xstrdup(a->valuestring));
            }
        }
        if (!ok) {
            set_err(err, errlen, "%s: core '%s' has an invalid archive name", path, cid);
            strv_free(&av);
            strv_free(&zv);
            return -1;
        }
        if (!av.v)
            av.v = xmalloc(sizeof *av.v), av.v[0] = NULL;
        c->url = xstrdup(url);
        c->archives = av.v;
        c->narchives = av.n;
        const cJSON *uf2 = cJSON_GetObjectItemCaseSensitive(d, "uf2");
        if (uf2 && !cJSON_IsNull(uf2)) {
            if (!cJSON_IsObject(uf2) ||
                parse_crc(cJSON_GetObjectItemCaseSensitive(uf2, "address"), &c->uf2_address) ||
                parse_crc(cJSON_GetObjectItemCaseSensitive(uf2, "family"), &c->uf2_family)) {
                set_err(err, errlen, "%s: core '%s' needs a hexadecimal 'address' and 'family' "
                        "in 'uf2'", path, cid);
                strv_free(&zv);
                return -1;
            }
            c->uf2 = 1;
        }
    }

    /* destination.replace("\\", "/").strip("/").split("/") */
    char *dest = xstrdup(destination);
    for (char *p = dest; *p; p++)
        if (*p == '\\')
            *p = '/';
    char *s = dest;
    while (*s == '/')
        s++;
    size_t n = strlen(s);
    while (n && s[n - 1] == '/')
        s[--n] = 0;
    strv parts = { 0 };
    int bad = !*s;
    for (char *p = s; !bad;) {
        char *slash = strchr(p, '/');
        char *part = slash ? fmt("%.*s", (int)(slash - p), p) : xstrdup(p);
        bad = bad_name(part);
        strv_push(&parts, part);
        if (!slash)
            break;
        p = slash + 1;
    }
    if (bad) {
        set_err(err, errlen, "%s: core '%s' has an invalid destination '%s'", path, cid,
                destination);
        strv_free(&parts);
        strv_free(&zv);
        free(dest);
        return -1;
    }
    c->dest_parts = parts.v;
    c->dest_shown = fmt("/%s", s);
    free(dest);

    if ((!is_data && !cJSON_GetArraySize(zips)) || !cJSON_GetArraySize(files)) {
        set_err(err, errlen, "%s: core '%s' needs at least one zip and one file", path, cid);
        strv_free(&zv);
        return -1;
    }
    const cJSON *z;
    if (!is_data) {
        cJSON_ArrayForEach(z, zips) {
            if (!cJSON_IsString(z) || bad_name(z->valuestring)) {
                set_err(err, errlen, "%s: core '%s' has an invalid zip name", path, cid);
                strv_free(&zv);
                return -1;
            }
            strv_push(&zv, xstrdup(z->valuestring));
        }
    }
    c->zips = zv.v;
    c->nzips = zv.n;

    c->files = xmalloc((size_t)cJSON_GetArraySize(files) * sizeof *c->files);
    const cJSON *f;
    cJSON_ArrayForEach(f, files) {
        const char *fname = cJSON_IsObject(f) ? str_field(f, "name") : NULL;
        /* Game data may go into subfolders of its destination; ROM sets may
           not, as older copies of updateAll would reject that. */
        int bad = !fname || bad_name(fname);
        if (fname && is_data)
            bad = bad_path(fname);
        if (bad) {
            set_err(err, errlen, "%s: core '%s' has an invalid file name '%s'", path, cid,
                    fname ? fname : "");
            return -1;
        }
        ua_file *uf = &c->files[c->nfiles];
        memset(uf, 0, sizeof *uf);
        if (parse_crc(cJSON_GetObjectItemCaseSensitive(f, "crc"), &uf->crc) ||
            parse_int(cJSON_GetObjectItemCaseSensitive(f, "size"), &uf->size)) {
            set_err(err, errlen, "%s: core '%s', file '%s' needs a numeric 'size' and a "
                    "hexadecimal 'crc'", path, cid, fname);
            return -1;
        }
        /* A BIOS file may be part of a download that is one file, or the
           entries of a zip joined; older copies do not know either. */
        const cJSON *offset = cJSON_GetObjectItemCaseSensitive(f, "offset");
        if (kind == KIND_BIOS && offset) {
            if (parse_crc(offset, &uf->offset)) {
                set_err(err, errlen, "%s: core '%s', file '%s' needs a hexadecimal 'offset'",
                        path, cid, fname);
                return -1;
            }
            uf->has_offset = 1;
        }
        const cJSON *join = cJSON_GetObjectItemCaseSensitive(f, "parts");
        if (kind == KIND_BIOS && join) {
            strv pv = { 0 };
            int ok = cJSON_IsArray(join) && cJSON_GetArraySize(join) > 0;
            const cJSON *p;
            if (ok) {
                cJSON_ArrayForEach(p, join) {
                    if (!cJSON_IsString(p) || bad_name(p->valuestring)) {
                        ok = 0;
                        break;
                    }
                    strv_push(&pv, xstrdup(p->valuestring));
                }
            }
            if (!ok) {
                set_err(err, errlen, "%s: core '%s', file '%s' has an invalid 'parts' list",
                        path, cid, fname);
                strv_free(&pv);
                return -1;
            }
            uf->parts = pv.v;
            uf->nparts = pv.n;
        }
        uf->name = xstrdup(fname);
        c->nfiles++;
    }
    int joined = 0;
    for (int i = 0; i < c->nfiles; i++) {
        c->sliced |= c->files[i].has_offset;
        joined |= c->files[i].nparts > 0;
    }
    if (c->sliced + joined + c->uf2 > 1) {
        set_err(err, errlen, "%s: core '%s' cannot combine 'offset', 'parts' and 'uf2'", path,
                cid);
        return -1;
    }
    return 0;
}

int ua_config_parse(const char *text, size_t len, const char *path, const char *config_dir,
                    ua_config *cfg, char *err, size_t errlen)
{
    memset(cfg, 0, sizeof *cfg);
    char *why = NULL;
    cJSON *root = parse_json(text, len, &why);
    if (!root) {
        set_err(err, errlen, "cannot read %s: %s", path, why);
        free(why);
        return -1;
    }
    int rc = -1;
    strv ids = { 0 }, dbs = { 0 };
    const cJSON *cores = cJSON_IsObject(root) ? cJSON_GetObjectItemCaseSensitive(root, "cores")
                                              : NULL;
    if (!cJSON_IsArray(cores) || !cJSON_GetArraySize(cores)) {
        set_err(err, errlen, "%s: no cores defined", path);
        goto done;
    }
    /* Game data from one URL each; a key of its own, because copies of
       updateAll that predate it read this file too, and skip it. */
    const cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (data && !cJSON_IsArray(data)) {
        set_err(err, errlen, "%s: 'data' must be a list", path);
        goto done;
    }
    /* The BIOS files, in the form of game data; a key of its own for the
       same reason, and so that they can be selected as one set. */
    const cJSON *bios = cJSON_GetObjectItemCaseSensitive(root, "bios");
    if (bios && !cJSON_IsArray(bios)) {
        set_err(err, errlen, "%s: 'bios' must be a list", path);
        goto done;
    }
    cfg->cores = xmalloc((size_t)(cJSON_GetArraySize(cores) + cJSON_GetArraySize(data) +
                                  cJSON_GetArraySize(bios)) * sizeof *cfg->cores);
    for (int kind = KIND_CORES; kind <= KIND_BIOS; kind++) {
        const cJSON *list = kind == KIND_BIOS ? bios : kind == KIND_DATA ? data : cores, *d;
        cJSON_ArrayForEach(d, list) {
            ua_core *c = &cfg->cores[cfg->ncores++];
            memset(c, 0, sizeof *c);
            if (parse_core(d, kind, c, &ids, path, err, errlen))
                goto done;
        }
    }

    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "databases");
    if (list) {
        const cJSON *e;
        int ok = cJSON_IsArray(list);
        if (ok)
            cJSON_ArrayForEach(e, list)
                ok &= cJSON_IsString(e);
        if (!ok) {
            set_err(err, errlen, "%s: 'databases' must be a list of URLs or paths", path);
            goto done;
        }
        cJSON_ArrayForEach(e, list) {
            const char *s = e->valuestring;
            strv_push(&dbs, ua_is_url(s) || is_abs(s) ? xstrdup(s) : path_join(config_dir, s));
        }
    }
    cfg->databases = dbs.v;
    cfg->ndatabases = dbs.n;
    memset(&dbs, 0, sizeof dbs);

    /* null and "" mean none, as in updateAll.py */
    const cJSON *url = cJSON_GetObjectItemCaseSensitive(root, "config_url");
    if (url && !cJSON_IsNull(url) && !(cJSON_IsString(url) && !*url->valuestring)) {
        if (!cJSON_IsString(url) || !ua_is_url(url->valuestring)) {
            set_err(err, errlen, "%s: 'config_url' must be an http or https URL", path);
            goto done;
        }
        cfg->config_url = xstrdup(url->valuestring);
    }

    /* Where the SD-card archive is released; a key of its own, for the same
       reason as "data". */
    const cJSON *sdcard = cJSON_GetObjectItemCaseSensitive(root, "sdcard");
    if (sdcard && !cJSON_IsNull(sdcard)) {
        if (!cJSON_IsObject(sdcard)) {
            set_err(err, errlen, "%s: 'sdcard' must be an object", path);
            goto done;
        }
        const char *sd_name = str_field(sdcard, "name"), *sd_url = str_field(sdcard, "releases_url");
        const char *sd_asset = str_field(sdcard, "asset");
        if (!sd_name || !*sd_name) {
            set_err(err, errlen, "%s: 'sdcard' needs a 'name'", path);
            goto done;
        }
        if (!sd_url || !ua_is_url(sd_url)) {
            set_err(err, errlen, "%s: 'sdcard' needs an http or https 'releases_url'", path);
            goto done;
        }
        if (!sd_asset || bad_name(sd_asset) || !iends_with(sd_asset, ".zip")) {
            set_err(err, errlen, "%s: 'sdcard' needs the name of a zip as 'asset'", path);
            goto done;
        }
        cfg->sd_name = xstrdup(sd_name);
        cfg->sd_url = xstrdup(sd_url);
        cfg->sd_asset = xstrdup(sd_asset);
    }
    cfg->config_dir = xstrdup(config_dir);
    cfg->source = xstrdup(path);
    rc = 0;
done:
    strv_free(&ids);
    strv_free(&dbs);
    cJSON_Delete(root);
    if (rc)
        ua_config_free(cfg);
    return rc;
}

int ua_config_load(const char *path, ua_config *cfg, char *err, size_t errlen)
{
    char *text, *why;
    size_t len;
    memset(cfg, 0, sizeof *cfg);
    if (read_file(path, &text, &len, &why)) {
        set_err(err, errlen, "cannot read %s: %s", path, why);
        free(why);
        return -1;
    }
    char *abs = ua_abspath(path);
    char *dir = fmt("%.*s", (int)(base_name(abs) - abs), abs);
    size_t n = strlen(dir);
    if (n > 1 && (dir[n - 1] == '/' || dir[n - 1] == UA_SEP) && dir[n - 2] != ':')
        dir[n - 1] = 0;
    int rc = ua_config_parse(text, len, path, dir, cfg, err, errlen);
    if (!rc) {
        free(cfg->source);
        cfg->source = abs;
        abs = NULL;
    }
    free(dir);
    free(abs);
    free(text);
    return rc;
}

static void free_list(char **v)
{
    if (v)
        for (char **p = v; *p; p++)
            free(*p);
    free(v);
}

void ua_config_free(ua_config *cfg)
{
    for (int i = 0; i < cfg->ncores; i++) {
        ua_core *c = &cfg->cores[i];
        free(c->id);
        free(c->name);
        free(c->set);
        free_list(c->zips);
        free_list(c->dest_parts);
        free(c->dest_shown);
        for (int k = 0; k < c->nfiles; k++) {
            free(c->files[k].name);
            free_list(c->files[k].parts);
        }
        free(c->files);
        free(c->url);
        free_list(c->archives);
    }
    free(cfg->cores);
    free_list(cfg->databases);
    free(cfg->config_url);
    free(cfg->config_dir);
    free(cfg->source);
    free(cfg->fetch_error);
    free(cfg->sd_name);
    free(cfg->sd_url);
    free(cfg->sd_asset);
    memset(cfg, 0, sizeof *cfg);
}

/* ------------------------------------------------------------------------- */
/* MD5 (RFC 1321)                                                            */
/* ------------------------------------------------------------------------- */

static const uint32_t md5_k[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
    0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
    0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
    0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
    0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
    0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
    0xeb86d391,
};
static const unsigned char md5_r[16] = { 7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21 };

static void md5_block(ua_md5 *m, const unsigned char *p)
{
    uint32_t w[16], a = m->a, b = m->b, c = m->c, d = m->d;
    for (int i = 0; i < 16; i++)
        w[i] = p[4 * i] | (uint32_t)p[4 * i + 1] << 8 | (uint32_t)p[4 * i + 2] << 16 |
               (uint32_t)p[4 * i + 3] << 24;
    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16)
            f = (b & c) | (~b & d), g = i;
        else if (i < 32)
            f = (d & b) | (~d & c), g = (5 * i + 1) & 15;
        else if (i < 48)
            f = b ^ c ^ d, g = (3 * i + 5) & 15;
        else
            f = c ^ (b | ~d), g = (7 * i) & 15;
        uint32_t x = a + f + md5_k[i] + w[g], r = md5_r[(i >> 4) * 4 + (i & 3)];
        a = d, d = c, c = b;
        b += x << r | x >> (32 - r);
    }
    m->a += a, m->b += b, m->c += c, m->d += d;
}

void ua_md5_init(ua_md5 *m)
{
    m->a = 0x67452301, m->b = 0xefcdab89, m->c = 0x98badcfe, m->d = 0x10325476;
    m->len = 0;
}

void ua_md5_update(ua_md5 *m, const void *data, size_t len)
{
    const unsigned char *p = data;
    while (len) {
        size_t used = m->len & 63, take = 64 - used < len ? 64 - used : len;
        memcpy(m->buf + used, p, take);
        m->len += take, p += take, len -= take;
        if (!(m->len & 63))
            md5_block(m, m->buf);
    }
}

void ua_md5_hex(ua_md5 *m, char out[33])
{
    uint64_t bits = m->len * 8;
    unsigned char pad[72] = { 0x80 };
    size_t padlen = ((m->len & 63) < 56 ? 56 : 120) - (m->len & 63);
    ua_md5_update(m, pad, padlen);
    for (int i = 0; i < 8; i++)
        pad[i] = (unsigned char)(bits >> (8 * i));
    ua_md5_update(m, pad, 8);
    uint32_t v[4] = { m->a, m->b, m->c, m->d };
    for (int i = 0; i < 16; i++)
        sprintf(out + 2 * i, "%02x", (v[i / 4] >> (8 * (i % 4))) & 0xff);
}

/* ------------------------------------------------------------------------- */
/* Databases and downloads                                                   */
/* ------------------------------------------------------------------------- */

typedef struct {
    char *buf;
    size_t len, cap;
} membuf;

static int mem_sink(void *ctx, const void *data, size_t len, long long total)
{
    (void)total;
    membuf *m = ctx;
    if (m->len + len + 1 > m->cap) {
        while (m->len + len + 1 > m->cap)
            m->cap = m->cap ? m->cap * 2 : 65536;
        m->buf = xrealloc(m->buf, m->cap);
    }
    memcpy(m->buf + m->len, data, len);
    m->len += len;
    return 0;
}

/* fetch_config(); here, with the downloads it shares mem_sink with. */
int ua_config_fetch(const char *url, const char *config_dir, ua_config *cfg, char *err,
                    size_t errlen)
{
    memset(cfg, 0, sizeof *cfg);
    membuf m = { 0 };
    char why[512];
    int status;
    if (ua_http_get(url, mem_sink, &m, &status, CONFIG_TIMEOUT, why, sizeof why)) {
        free(m.buf);
        set_err(err, errlen, "cannot read %s: %s", url, why);
        return -1;
    }
    int rc = ua_config_parse(m.buf ? m.buf : "", m.len, url, config_dir, cfg, err, errlen);
    free(m.buf);
    if (!rc) {
        free(cfg->config_url);   /* not followed: it only says where this came from */
        cfg->config_url = xstrdup(url);
    }
    return rc;
}

void ua_config_update(ua_config *cfg)
{
    if (!cfg->config_url)
        return;
    ua_config latest;
    char err[1024];
    if (ua_config_fetch(cfg->config_url, cfg->config_dir, &latest, err, sizeof err)) {
        free(cfg->fetch_error);
        cfg->fetch_error = xstrdup(err);
        return;
    }
    ua_config_free(cfg);
    *cfg = latest;
}

typedef struct {
    char *source;
    char *id;
    char *base;
    cJSON *root;
    const cJSON *files;
} database;

/* Database.__init__; returns 0, or -1 with *err set. */
static int database_load(database *db, const char *source, char **err)
{
    memset(db, 0, sizeof *db);
    char *data = NULL;
    size_t len = 0;
    if (ua_is_url(source)) {
        membuf m = { 0 };
        char why[512];
        int status;
        if (ua_http_get(source, mem_sink, &m, &status, TIMEOUT, why, sizeof why)) {
            free(m.buf);
            *err = xstrdup(why);
            return -1;
        }
        data = m.buf ? m.buf : xstrdup("");
        len = m.len;
    } else if (read_file(source, &data, &len, err)) {
        return -1;
    }

    if (len >= 4 && !memcmp(data, "PK\3\4", 4)) {   /* a zipped database, as MiSTer publishes them */
        mz_zip_archive za;
        mz_zip_zero_struct(&za);
        if (!mz_zip_reader_init_mem(&za, data, len, 0)) {
            free(data);
            *err = xstrdup("File is not a zip file");
            return -1;
        }
        char *json = NULL;
        size_t jlen = 0;
        int found = 0;
        mz_uint n = mz_zip_reader_get_num_files(&za);
        for (mz_uint i = 0; i < n && !found; i++) {
            mz_zip_archive_file_stat st;
            if (mz_zip_reader_file_stat(&za, i, &st) && iends_with(st.m_filename, ".json")) {
                found = 1;
                json = mz_zip_reader_extract_to_heap(&za, i, &jlen, 0);
                if (!json)
                    *err = fmt("cannot read %s from the zip", st.m_filename);
            }
        }
        if (!found)
            *err = xstrdup("the zip holds no .json file");
        mz_zip_reader_end(&za);
        free(data);
        if (!json)
            return -1;
        data = json;
        len = jlen;
    }

    db->root = parse_json(data, len, err);
    free(data);
    if (!db->root)
        return -1;
    db->files = cJSON_IsObject(db->root) ? cJSON_GetObjectItemCaseSensitive(db->root, "files")
                                         : NULL;
    if (!cJSON_IsObject(db->files)) {
        cJSON_Delete(db->root);
        *err = xstrdup("no 'files' map");
        return -1;
    }
    const char *id = str_field(db->root, "db_id"), *base = str_field(db->root, "base_files_url");
    db->source = xstrdup(source);
    db->id = xstrdup(id && *id ? id : source);
    db->base = xstrdup(base ? base : "");
    return 0;
}

static void database_free(database *db)
{
    cJSON_Delete(db->root);
    free(db->source);
    free(db->id);
    free(db->base);
}

typedef struct {
    char *url;
    int has_size;
    long long size;
    char *size_text;     /* the size as the database gives it */
    char *md5;
    const database *db;
} db_hit;

static void hit_free(db_hit *h)
{
    free(h->url);
    free(h->size_text);
    free(h->md5);
    memset(h, 0, sizeof *h);
}

/* urllib.parse.quote(path): everything but letters, digits, "_.-~/" is escaped. */
static char *url_quote(const char *s)
{
    char *q = xmalloc(strlen(s) * 3 + 1), *o = q;
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
            strchr("_.-~/", *p))
            *o++ = (char)*p;
        else
            o += sprintf(o, "%%%02X", *p);
    }
    *o = 0;
    return q;
}

static const char *strip_bars(const char *key)
{
    while (*key == '|')   /* MiSTer marks paths for external storage with '|' */
        key++;
    return key;
}

/* The folder the zip sits in is "mame", ignoring case. */
static int in_mame_folder(const char *path)
{
    const char *last = strrchr(path, '/');
    if (!last)
        return 0;
    const char *prev = last;
    while (prev > path && prev[-1] != '/')
        prev--;
    return last - prev == 4 && lower(prev[0]) == 'm' && lower(prev[1]) == 'a' &&
           lower(prev[2]) == 'm' && lower(prev[3]) == 'e';
}

static int database_find(const database *db, const char *zip_name, db_hit *hit)
{
    /* A MiSTer database can hold the same zip name under games/mame and
       games/hbmame; the MAME one is the set the applications expect. */
    for (int pass = 0; pass < 2; pass++) {
        const cJSON *e;
        cJSON_ArrayForEach(e, db->files) {
            const char *path = strip_bars(e->string);
            if (!cJSON_IsObject(e) || !ieq(base_name(path), zip_name))
                continue;
            if (in_mame_folder(path) != (pass == 0))
                continue;
            const char *url = str_field(e, "url");
            char *u = url && *url ? xstrdup(url) : NULL;
            if (!u && *db->base) {
                char *q = url_quote(path);
                u = fmt("%s%s", db->base, q);
                free(q);
            }
            if (!u)
                continue;
            memset(hit, 0, sizeof *hit);
            hit->url = u;
            hit->db = db;
            const cJSON *size = cJSON_GetObjectItemCaseSensitive(e, "size");
            if (size && !cJSON_IsNull(size) && !parse_int(size, &hit->size)) {
                hit->has_size = 1;
                hit->size_text = cJSON_IsString(size) ? xstrdup(size->valuestring)
                                                      : fmt("%" PRId64, (int64_t)hit->size);
            }
            const char *md5 = str_field(e, "hash");
            hit->md5 = md5 && *md5 ? xstrdup(md5) : NULL;
            return 1;
        }
    }
    return 0;
}

/* Databases: loaded on first use, so that nothing is fetched while every zip
   is at hand. */
typedef struct {
    char **sources;
    int nsources;
    database *loaded;
    int nloaded;
    int load_tried;
    int nerrors;
    const ua_hooks *h;
} databases;

static int databases_find(databases *d, const char *zip_name, db_hit *hit)
{
    if (!d->load_tried) {
        d->load_tried = 1;
        d->loaded = xmalloc((size_t)(d->nsources ? d->nsources : 1) * sizeof *d->loaded);
        for (int i = 0; i < d->nsources; i++) {
            char *err = NULL;
            const char *file = base_name(d->sources[i]);
            if (ua_is_url(d->sources[i]))
                file_event(d->h, "Database", file, UA_FILE_DOWNLOADING, "Downloading");
            else
                file_event(d->h, "Database", file, UA_FILE_BUSY, "Reading");
            if (database_load(&d->loaded[d->nloaded], d->sources[i], &err)) {
                d->nerrors++;
                out(d->h, "ERROR: database %s: %s", d->sources[i], err);
                file_event(d->h, "Database", file, UA_FILE_FAILED, err);
                free(err);
            } else {
                d->nloaded++;
                file_event(d->h, "Database", file, UA_FILE_DONE, "Loaded");
            }
        }
    }
    for (int i = 0; i < d->nloaded; i++)
        if (database_find(&d->loaded[i], zip_name, hit))
            return 1;
    return 0;
}

static void databases_free(databases *d)
{
    for (int i = 0; i < d->nloaded; i++)
        database_free(&d->loaded[i]);
    free(d->loaded);
}

typedef struct {
    FILE *out;
    ua_md5 md5;
    long long got;
    long long size;     /* the database's size, for a server that announces none */
    int write_errno;
    int cancelled;
    const ua_hooks *h;
} dl_state;

static int dl_sink(void *ctx, const void *data, size_t len, long long total)
{
    dl_state *s = ctx;
    if (fwrite(data, 1, len, s->out) != len) {
        s->write_errno = errno ? errno : EIO;
        return 1;
    }
    ua_md5_update(&s->md5, data, len);
    s->got += (long long)len;
    if (s->h->on_bytes)
        s->h->on_bytes(s->h->ctx, s->got, total >= 0 ? total : s->size);
    if (is_cancelled(s->h)) {
        s->cancelled = 1;
        return 1;
    }
    return 0;
}

static int is_zip_file(const char *path)
{
    mz_zip_archive za;
    mz_zip_zero_struct(&za);
    if (!mz_zip_reader_init_file(&za, path, 0))
        return 0;
    mz_zip_reader_end(&za);
    return 1;
}

static int sleep_unless_cancelled(const ua_hooks *h, int ms)
{
    for (; ms > 0; ms -= 100) {
        if (is_cancelled(h))
            return 1;
        ua_sleep_ms(ms < 100 ? ms : 100);
    }
    return is_cancelled(h);
}

/* download(); returns the number of bytes, or -1 with *reason set. */
static long long download(const db_hit *hit, const char *dest, int need_zip, const char *set,
                          const char *name, const ua_hooks *h, char **reason)
{
    char *tmp = fmt("%s.part", dest);
    char *why = xstrdup("no attempt made");
    long long result = -1;
    for (int attempt = 0; attempt < DOWNLOAD_ATTEMPTS; attempt++) {
        if (attempt) {
            char *retry = fmt("Retrying: %s", why);
            file_event(h, set, name, UA_FILE_BUSY, retry);
            free(retry);
            if (sleep_unless_cancelled(h, 2000 * attempt)) {
                free(why);
                why = xstrdup("cancelled");
                break;
            }
            file_event(h, set, name, UA_FILE_DOWNLOADING, "Downloading");
        }
        dl_state s = { 0 };
        s.h = h;
        s.size = hit->has_size ? hit->size : -1;
        ua_md5_init(&s.md5);
        int stop = 0;
        char err[512] = "";
        s.out = ua_fopen(tmp, "wb");
        if (!s.out) {
            free(why);
            why = os_error(errno, tmp);
        } else {
            int status = 0;
            int rc = ua_http_get(hit->url, dl_sink, &s, &status, TIMEOUT, err, sizeof err);
            if (fclose(s.out) && !rc) {
                rc = -1;
                s.write_errno = errno ? errno : EIO;
            }
            free(why);
            why = NULL;
            if (s.cancelled) {
                why = xstrdup("cancelled");
                stop = 1;
            } else if (s.write_errno) {
                why = os_error(s.write_errno, tmp);
            } else if (rc) {
                why = xstrdup(err);
                stop = status >= 400 && status < 500;   /* retrying will not help */
            } else {
                char md5[33];
                ua_md5_hex(&s.md5, md5);
                if (hit->has_size && s.got != hit->size)
                    why = fmt("received %" PRId64 " bytes, the database says %s", (int64_t)s.got,
                              hit->size_text);
                else if (hit->md5 && !ieq(md5, hit->md5))
                    why = xstrdup("the MD5 does not match the database");
                else if (need_zip && !is_zip_file(tmp))
                    why = xstrdup("the file received is not a zip");
                else if (ua_replace(tmp, dest))
                    why = xstrdup(ua_last_error());
                else
                    result = s.got;
            }
        }
        if (result < 0)
            ua_remove(tmp);
        if (result >= 0 || stop)
            break;
    }
    free(tmp);
    if (result < 0)
        *reason = why;
    else
        free(why);
    return result;
}

static char *uf2_note(const char *name, uint32_t address, int dry_run)
{
    return fmt("%s %s to a UF2 image at 0x%08X", dry_run ? "would convert" : "converted", name,
               (unsigned)address);
}

/* fetch_missing_zips(): from the databases, or from the core's own URL when
   url_downloads is set; returns whether a dry run would have downloaded. */
static int fetch_missing_zips(const ua_core *core, const char *zip_dir, strv *listing,
                              databases *dbs, int dry_run, int url_downloads, strv *notes,
                              strv *errors, const ua_hooks *h)
{
    int pending = 0;
    for (int z = 0; z < core->nzips; z++) {
        const char *zname = core->zips[z];
        int have = 0;
        for (int i = 0; i < listing->n && !have; i++)
            have = ieq(listing->v[i], zname);
        if (have)
            continue;
        db_hit hit = { 0 };
        char *source;
        if (core->url) {
            if (!url_downloads)
                continue;
            hit.url = xstrdup(core->url);
            if (core->release) {
                hit.has_size = 1;
                hit.size = core->release->size;
                hit.size_text = fmt("%lld", core->release->size);
            }
            source = url_host(core->url);
        } else {
            if (!databases_find(dbs, zname, &hit))
                continue;
            source = xstrdup(hit.db->id);
        }
        if (dry_run) {
            strv_push(notes, fmt("would download %s from %s", zname, source));
            if (core->uf2)
                strv_push(notes, uf2_note(zname, core->uf2_address, dry_run));
            char *would = fmt("Would download from %s", source);
            file_event(h, core->name, zname, UA_FILE_NOTE, would);
            free(would);
            pending = 1;
            hit_free(&hit);
            free(source);
            continue;
        }
        if (is_cancelled(h)) {
            hit_free(&hit);
            free(source);
            break;
        }
        char *why = NULL;
        long long got = -1;
        file_event(h, core->name, zname, UA_FILE_DOWNLOADING, "Downloading");
        if (ua_mkdirs(zip_dir)) {
            why = xstrdup(ua_last_error());
        } else {
            char *dest = path_join(zip_dir, zname);
            got = download(&hit, dest, !core->url || core->release, core->name, zname, h, &why);
            free(dest);
        }
        if (got < 0) {
            strv_push(errors, fmt("download of %s from %s failed: %s", zname, source, why));
            if (!strcmp(why, "cancelled"))
                file_event(h, core->name, zname, UA_FILE_CANCELLED, "Cancelled");
            else
                file_event(h, core->name, zname, UA_FILE_FAILED, why);
            free(why);
        } else {
            strv_push(listing, xstrdup(zname));
            strv_push(notes, fmt("downloaded %s (%" PRId64 " KB) from %s", zname,
                                 (int64_t)((got + 1023) / 1024), source));
            file_event(h, core->name, zname, UA_FILE_BUSY, "Downloaded");     /* the install can still fail */
        }
        hit_free(&hit);
        free(source);
    }
    return pending;
}

/* ------------------------------------------------------------------------- */
/* Extraction                                                                */
/* ------------------------------------------------------------------------- */

static int file_crc(const char *path, uint32_t *crc)
{
    FILE *f = ua_fopen(path, "rb");
    if (!f)
        return -1;
    unsigned char *buf = xmalloc(65536);
    mz_ulong c = MZ_CRC32_INIT;
    size_t n;
    while ((n = fread(buf, 1, 65536, f)) > 0)
        c = mz_crc32(c, buf, n);
    int bad = ferror(f);
    fclose(f);
    free(buf);
    *crc = (uint32_t)c;
    return bad ? -1 : 0;
}

static int is_current(const char *path, long long size, uint32_t crc)
{
    long long have;
    uint32_t c;
    return !ua_file_size(path, &have) && have == size && !file_crc(path, &c) && c == crc;
}

static char *dest_dir(const ua_core *core, const char *sd_root)
{
    char *p = xstrdup(sd_root);
    for (char **part = core->dest_parts; *part; part++) {
        char *q = path_join(p, *part);
        free(p);
        p = q;
    }
    return p;
}

/* os.path.join(dest, *name.split("/")): game data may sit in subfolders. */
static char *file_target(const char *dest, const char *name)
{
    char *t = path_join(dest, name);
    for (char *p = t + strlen(dest); *p; p++)
        if (*p == '/')
            *p = UA_SEP;
    return t;
}

int ua_count_current(const ua_core *core, const char *sd_root)
{
    char *dest = dest_dir(core, sd_root);
    int n = 0;
    for (int i = 0; i < core->nfiles; i++) {
        char *target = file_target(dest, core->files[i].name);
        n += is_current(target, core->files[i].size, core->files[i].crc);
        free(target);
    }
    free(dest);
    return n;
}

typedef struct {
    int arch;
    mz_uint index;
    long long size;
    uint32_t crc;
    char *name;        /* as stored in the zip */
    int in_subfolder;
    const char *base;  /* points into name */
} zip_entry;

/* entry_rank(): the root of the zip before clone subfolders, and the
   expected name before any other file with the same contents. */
static int rank(const zip_entry *e, const char *wanted)
{
    return e->in_subfolder * 2 + !ieq(e->base, wanted);
}

typedef struct {
    int written, current;
    strv missing, errors, notes;
    char *skipped;
    strv used;
} result;

/* One place to search for files: a zip, a zip inside one, or game data that
   is not a zip at all and stands for itself (PlainFile in updateAll.py). */
typedef struct {
    mz_zip_archive za;
    int opened;     /* za is open */
    void *mem;      /* the bytes of a zip inside a zip, kept while za is open */
    unsigned char *plain;   /* a plain file's bytes, or its UF2 image; or NULL */
    size_t plain_size;
    uint32_t plain_crc;
    char *plain_name;       /* the name of that one file, when not label: a BIOS
                               file cut out of a download, or joined from a zip */
    char *label;    /* "x.zip", or "x.zip:INNER.ZIP", as messages name it */
} archive;

/* MemoryArchive.add(): one more archive holding just data, as name. */
static void add_plain(archive **arcs, int *narcs, int *cap, const char *label, const char *name,
                      unsigned char *data, size_t len)
{
    if (*narcs == *cap)
        *arcs = xrealloc(*arcs, (size_t)(*cap = *cap + 8) * sizeof **arcs);
    archive *a = &(*arcs)[(*narcs)++];
    memset(a, 0, sizeof *a);
    a->label = xstrdup(label);
    a->plain = data;
    a->plain_size = len;
    a->plain_crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, data, len);
    a->plain_name = name ? xstrdup(name) : NULL;
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

/* to_uf2(): data as a UF2 image at address with this family ID, as picotool's
   "uf2 convert -t bin" writes it: 256 bytes in each 512-byte block, the last
   one padded with zeros. The progress goes to on_bytes, as for a download. */
static unsigned char *to_uf2(const unsigned char *data, size_t len, uint32_t address,
                             uint32_t family, size_t *out_len, const ua_hooks *h)
{
    size_t n = (len + 255) / 256;
    unsigned char *out = xmalloc(n * 512);
    memset(out, 0, n * 512);
    for (size_t i = 0; i < n; i++) {
        if (h->on_bytes && i % 64 == 0)
            h->on_bytes(h->ctx, (long long)(i * 512), (long long)(n * 512));
        unsigned char *b = out + i * 512;
        const uint32_t head[8] = { 0x0A324655, 0x9E5D5157, 0x00002000,
                                   address + (uint32_t)(256 * i), 256, (uint32_t)i,
                                   (uint32_t)n, family };
        for (int k = 0; k < 8; k++)
            put32(b + 4 * k, head[k]);
        memcpy(b + 32, data + 256 * i, len - 256 * i < 256 ? len - 256 * i : 256);
        put32(b + 508, 0x0AB16F30);
    }
    if (h->on_bytes)
        h->on_bytes(h->ctx, (long long)(n * 512), (long long)(n * 512));
    *out_len = n * 512;
    return out;
}

static char *open_error(mz_zip_archive *za, const char *path)
{
    if (mz_zip_get_last_error(za) == MZ_ZIP_FILE_OPEN_FAILED)
        return os_error(errno ? errno : ENOENT, path);
    return xstrdup("File is not a zip file");
}

static char *extract_error(mz_zip_archive *za, const char *name)
{
    switch (mz_zip_get_last_error(za)) {
    case MZ_ZIP_CRC_CHECK_FAILED:
        return fmt("Bad CRC-32 for file '%s'", name);
    case MZ_ZIP_UNSUPPORTED_METHOD:
        return xstrdup("That compression method is not supported");
    case MZ_ZIP_UNSUPPORTED_ENCRYPTION:
        return fmt("File '%s' is encrypted, password required for extraction", name);
    case MZ_ZIP_DECOMPRESSION_FAILED:
        return xstrdup("Error -3 while decompressing data");
    default:
        return xstrdup(mz_zip_get_error_string(mz_zip_get_last_error(za)));
    }
}

static int write_file(const char *target, const void *data, size_t len, char **why)
{
    char *dir = fmt("%.*s", (int)(base_name(target) - target - 1), target);
    int mk = ua_mkdirs(dir);
    free(dir);
    if (mk) {
        *why = xstrdup(ua_last_error());
        return -1;
    }
    char *tmp = fmt("%s.tmp", target);
    FILE *f = ua_fopen(tmp, "wb");
    int rc = -1;
    if (!f) {
        *why = os_error(errno, tmp);
    } else {
        int ok = fwrite(data, 1, len, f) == len;
        int err = errno;
        if (fclose(f))
            ok = 0;
        if (!ok) {
            *why = os_error(err ? err : EIO, tmp);
            ua_remove(tmp);
        } else if (ua_replace(tmp, target)) {
            *why = xstrdup(ua_last_error());
            ua_remove(tmp);
        } else {
            rc = 0;
        }
    }
    free(tmp);
    return rc;
}

/* ------------------------------------------------------------------------- */
/* The SD-card archive                                                       */
/* ------------------------------------------------------------------------- */

/* parse_tag(): three numbers to compare, or -1 for any other text. */
static int parse_tag(const char *tag, unsigned key[3])
{
    key[0] = key[1] = key[2] = 0;
    if (*tag++ != 'v')
        return -1;
    for (int n = 0;;) {
        int digits = 0;
        unsigned v = 0;
        while (*tag >= '0' && *tag <= '9' && digits < 10)
            v = v * 10 + (unsigned)(*tag++ - '0'), digits++;
        if (!digits || digits > 9)
            return -1;
        key[n++] = v;
        if (!*tag)
            return n >= 2 ? 0 : -1;
        if (*tag++ != '.' || n == 3)
            return -1;
    }
}

static int key_cmp(const unsigned a[3], const unsigned b[3])
{
    for (int i = 0; i < 3; i++)
        if (a[i] != b[i])
            return a[i] < b[i] ? -1 : 1;
    return 0;
}

/* read_marker() */
char *ua_card_release(const char *sd_root)
{
    char *path = file_target(sd_root, RELEASE_MARKER), *data = NULL, *why = NULL;
    size_t len = 0;
    int rc = read_file(path, &data, &len, &why);
    free(path);
    if (rc) {
        free(why);
        return NULL;
    }
    if (len > 256)
        data[256] = 0;
    data[strcspn(data, "\n")] = 0;
    char *s = data;
    while (*s && strchr(" \t\r\v\f", *s))
        s++;
    size_t n = strlen(s);
    while (n && strchr(" \t\r\v\f", s[n - 1]))
        s[--n] = 0;
    unsigned key[3];
    char *tag = parse_tag(s, key) ? NULL : xstrdup(s);
    free(data);
    return tag;
}

/* _release_asset(): the usable asset of this name in a release, or NULL. */
static const cJSON *release_asset(const cJSON *release, const char *asset)
{
    const cJSON *assets = cJSON_GetObjectItemCaseSensitive(release, "assets"), *a;
    if (!cJSON_IsArray(assets))
        return NULL;
    cJSON_ArrayForEach(a, assets) {
        if (!cJSON_IsObject(a) || !str_field(a, "name") || strcmp(str_field(a, "name"), asset))
            continue;
        const cJSON *state = cJSON_GetObjectItemCaseSensitive(a, "state");
        const cJSON *size = cJSON_GetObjectItemCaseSensitive(a, "size");
        const char *url = str_field(a, "browser_download_url");
        if ((!state || (cJSON_IsString(state) && !strcmp(state->valuestring, "uploaded"))) &&
            url && ua_is_url(url) && cJSON_IsNumber(size) && size->valuedouble >= 0)
            return a;
    }
    return NULL;
}

int ua_release_fetch(const ua_config *cfg, ua_release *rel, char *err, size_t errlen)
{
    memset(rel, 0, sizeof *rel);
    membuf m = { 0 };
    char why[512];
    int status;
    if (ua_http_get(cfg->sd_url, mem_sink, &m, &status, CONFIG_TIMEOUT, why, sizeof why)) {
        free(m.buf);
        set_err(err, errlen, "%s", why);
        return -1;
    }
    char *json_err = NULL;
    cJSON *root = parse_json(m.buf ? m.buf : "", m.len, &json_err);
    free(json_err);
    free(m.buf);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        set_err(err, errlen, "the answer is not a list of releases");
        return -1;
    }
    unsigned best[3] = { 0, 0, 0 };
    const cJSON *r;
    cJSON_ArrayForEach(r, root) {
        /* The workflow does not mark a dry run such as v0.2.1-alpha as a
           prerelease, so the form of the tag is what sets it apart. */
        if (!cJSON_IsObject(r) || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "draft")) ||
            cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(r, "prerelease")))
            continue;
        const char *tag = str_field(r, "tag_name"), *page = str_field(r, "html_url");
        const cJSON *asset = release_asset(r, cfg->sd_asset);
        unsigned key[3];
        if (!tag || parse_tag(tag, key) || !asset || !page || !ua_is_url(page) ||
            (rel->tag && key_cmp(key, best) <= 0))
            continue;
        ua_release_free(rel);
        memcpy(best, key, sizeof best);
        rel->tag = xstrdup(tag);
        rel->page = xstrdup(page);
        rel->url = xstrdup(str_field(asset, "browser_download_url"));
        rel->size = (long long)cJSON_GetObjectItemCaseSensitive(asset, "size")->valuedouble;
    }
    cJSON_Delete(root);
    if (!rel->tag) {
        set_err(err, errlen, "no release holds %s", cfg->sd_asset);
        return -1;
    }
    return 0;
}

void ua_release_compare(ua_release *rel, const char *card_tag)
{
    free(rel->card);
    rel->card = card_tag ? xstrdup(card_tag) : NULL;
    rel->newer = rel->firmware = 1;
    unsigned tag[3], card[3];
    if (rel->card && !parse_tag(rel->tag, tag) && !parse_tag(rel->card, card)) {
        rel->newer = key_cmp(tag, card) > 0;
        /* v0.N.M has the firmware of v0.N, so only the first two numbers count. */
        rel->firmware = tag[0] != card[0] || tag[1] != card[1];
    }
}

void ua_release_free(ua_release *rel)
{
    free(rel->tag);
    free(rel->page);
    free(rel->url);
    free(rel->card);
    memset(rel, 0, sizeof *rel);
}

int ua_card_has_emu(const char *sd_root)
{
    char *emu = path_join(sd_root, "emu");
    int has = ua_is_dir(emu);
    free(emu);
    return has;
}

/* forget_stale_zip(): a release can be cut again, so a kept archive of the
   wrong size is taken from the listing, which downloads it again. */
static void forget_stale_zip(const ua_core *core, const char *zip_dir, strv *listing)
{
    for (int i = 0; i < listing->n; i++) {
        if (!ieq(listing->v[i], core->zips[0]))
            continue;
        char *path = path_join(zip_dir, listing->v[i]);
        long long size;
        if (ua_file_size(path, &size) || size != core->release->size) {
            free(listing->v[i]);
            memmove(listing->v + i, listing->v + i + 1,
                    (size_t)(listing->n - i) * sizeof *listing->v);
            listing->n--;
            i--;
        }
        free(path);
    }
}

/* keeps_own_config(): whether the card's updateAll.json stays. */
static int keeps_own_config(const char *sd_root)
{
    char *path = file_target(sd_root, OWN_CONFIG), *data = NULL, *why = NULL, *json_err = NULL;
    long long size;
    size_t len;
    int keep = 0;
    if (!ua_file_size(path, &size)) {
        keep = 1;
        if (read_file(path, &data, &len, &why)) {
            free(why);
        } else {
            cJSON *root = parse_json(data, len, &json_err);
            const char *url = cJSON_IsObject(root) ? str_field(root, "config_url") : NULL;
            keep = !root || !url || !*url;
            cJSON_Delete(root);
            free(json_err);
            free(data);
        }
    }
    free(path);
    return keep;
}

/* is_list_to_save(): a list in emu that a user may have edited. */
static int is_list_to_save(const char *name)
{
    if (!(lower((unsigned char)name[0]) == 'e' && lower((unsigned char)name[1]) == 'm' &&
          lower((unsigned char)name[2]) == 'u' && name[3] == '/'))
        return 0;
    const char *file = name + 4;
    return !strchr(file, '/') && iends_with(file, ".txt") && !ieq(file, "versions.txt") &&
           !ieq(file, "boot.example.txt") && !ieq(file, "release.txt");
}

static void free_files(ua_core *core)
{
    for (int i = 0; i < core->nfiles; i++)
        free(core->files[i].name);
    free(core->files);
    core->files = NULL;
    core->nfiles = 0;
}

/* fill_release_files(): one unsafe name means that nothing is installed. */
static void fill_release_files(ua_core *core, const archive *a, const char *sd_root, result *res)
{
    int keep_config = keeps_own_config(sd_root);
    mz_zip_archive *za = (mz_zip_archive *)&a->za;
    mz_uint n = mz_zip_reader_get_num_files(za);
    core->files = xmalloc((size_t)n * sizeof *core->files);
    core->nfiles = 0;
    for (mz_uint i = 0; i < n; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(za, i, &st))
            continue;
        const char *name = st.m_filename;
        size_t len = strlen(name);
        if (len && name[len - 1] == '/')
            continue;
        int bad = bad_path(name);
        for (const char *p = name; *p; p++)
            bad |= (unsigned char)*p > 127;
        if (bad) {
            strv_push(&res->errors, fmt("%s holds an unsafe file name, so nothing was "
                                        "installed from it", a->label));
            free_files(core);
            return;
        }
        /* The marker is written last, by run(), once the install is complete. */
        if (ieq(name, RELEASE_MARKER) || (ieq(name, OWN_CONFIG) && keep_config))
            continue;
        ua_file *uf = &core->files[core->nfiles++];
        memset(uf, 0, sizeof *uf);
        uf->name = xstrdup(name);
        uf->size = (long long)st.m_uncomp_size;
        uf->crc = st.m_crc32;
        uf->backup = is_list_to_save(name);
    }
}

/* backup_name(): malloc'd; NULL when there is nothing to save, or a backup
   already holds the same. */
static char *backup_name(const char *target)
{
    char *current, *why = NULL;
    size_t len;
    if (read_file(target, &current, &len, &why)) {
        free(why);
        return NULL;
    }
    char *bak = NULL;
    for (int n = 1;; n++) {
        bak = n == 1 ? fmt("%s.bak", target) : fmt("%s.bak%d", target, n);
        char *other;
        size_t olen;
        if (read_file(bak, &other, &olen, &why)) {
            free(why);
            break;
        }
        int same = olen == len && !memcmp(other, current, len);
        free(other);
        free(bak);
        bak = NULL;
        if (same)
            break;
    }
    free(current);
    return bak;
}

/* Saves the card's copy of a file of the archive, before it is replaced. */
static int save_backup(const ua_file *f, const char *target, result *res, char **why)
{
    char *bak = backup_name(target);
    if (!bak)
        return 0;
    char *data;
    size_t len;
    int rc = read_file(target, &data, &len, why);
    if (!rc) {
        rc = write_file(bak, data, len, why);
        free(data);
    }
    if (!rc)
        strv_push(&res->notes, fmt("saved %s as %s%s", f->name, f->name, bak + strlen(target)));
    free(bak);
    return rc;
}

static int write_marker(const char *sd_root, const char *tag, char **why)
{
    char *path = file_target(sd_root, RELEASE_MARKER), *line = fmt("%s\n", tag);
    int rc = write_file(path, line, strlen(line), why);
    free(line);
    free(path);
    return rc;
}

/* open_archives(): the zip at path, or the file alone when game data is not a
   zip or is to become a UF2 image, plus the zips inside it that the core names
   in "archives". They are appended to *arcs; problems go to res->errors, and a
   conversion to res->notes. */
static void open_archives(const ua_core *core, const char *path, const char *name,
                          archive **arcs, int *narcs, int *cap, int dry_run, result *res,
                          const ua_hooks *h)
{
    if (*narcs + 1 + core->narchives > *cap) {
        *cap = *narcs + 1 + core->narchives + 8;
        *arcs = xrealloc(*arcs, (size_t)*cap * sizeof **arcs);
    }
    archive *a = &(*arcs)[*narcs];
    memset(a, 0, sizeof *a);
    a->label = xstrdup(name);
    if (core->url && !core->release && (core->uf2 || core->sliced || !is_zip_file(path))) {
        char *data, *why;
        size_t len;
        if (read_file(path, &data, &len, &why)) {
            strv_push(&res->errors, fmt("cannot open %s: %s", name, why));
            free(why);
            free(a->label);
            return;
        }
        if (core->uf2) {
            file_event(h, core->name, name, UA_FILE_BUSY, "Converting to a UF2 image");
            a->plain = to_uf2((unsigned char *)data, len, core->uf2_address, core->uf2_family,
                              &a->plain_size, h);
            free(data);
            strv_push(&res->notes, uf2_note(name, core->uf2_address, dry_run));
        } else {
            a->plain = (unsigned char *)data;
            a->plain_size = len;
        }
        a->plain_crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, a->plain, a->plain_size);
        int whole = (*narcs)++;
        /* A BIOS file with an offset is that part of the download, under its
           own name. */
        for (int i = 0; i < core->nfiles; i++) {
            const ua_file *f = &core->files[i];
            if (!f->has_offset)
                continue;
            const archive *w = &(*arcs)[whole];
            size_t from = f->offset < w->plain_size ? f->offset : w->plain_size;
            size_t want = f->size > 0 ? (size_t)f->size : 0;
            size_t n = w->plain_size - from < want ? w->plain_size - from : want;
            unsigned char *part = xmalloc(n + 1);
            memcpy(part, w->plain + from, n);
            add_plain(arcs, narcs, cap, name, base_name(f->name), part, n);
        }
        return;
    }
    mz_zip_zero_struct(&a->za);
    errno = 0;
    if (!mz_zip_reader_init_file(&a->za, path, 0)) {
        char *why = open_error(&a->za, path);
        strv_push(&res->errors, fmt("cannot open %s: %s", name, why));
        free(why);
        free(a->label);
        return;
    }
    a->opened = 1;
    (*narcs)++;
    mz_zip_archive *outer = &a->za;

    for (int k = 0; k < core->narchives; k++) {
        const char *inner = core->archives[k];
        mz_uint n = mz_zip_reader_get_num_files(outer), found = n;
        mz_zip_archive_file_stat st;
        for (mz_uint i = 0; i < n && found == n; i++)
            if (mz_zip_reader_file_stat(outer, i, &st) && ieq(base_name(st.m_filename), inner))
                found = i;
        if (found == n) {
            strv_push(&res->errors, fmt("%s holds no %s", name, inner));
            continue;
        }
        char *label = fmt("%s:%s", name, st.m_filename);
        size_t len = 0;
        void *mem = mz_zip_reader_extract_to_heap(outer, found, &len, 0);
        archive *b = &(*arcs)[*narcs];
        memset(b, 0, sizeof *b);
        mz_zip_zero_struct(&b->za);
        if (!mem) {
            char *why = extract_error(outer, st.m_filename);
            strv_push(&res->errors, fmt("cannot open %s: %s", label, why));
            free(why);
            free(label);
            continue;
        }
        if (!mz_zip_reader_init_mem(&b->za, mem, len, 0)) {
            strv_push(&res->errors, fmt("cannot open %s: File is not a zip file", label));
            mz_free(mem);
            free(label);
            continue;
        }
        b->opened = 1;
        b->mem = mem;
        b->label = label;
        (*narcs)++;
    }
}

/* The files of arcs[from..to) that are not folders, appended to *entries. */
static void index_archives(archive *arcs, int from, int to, zip_entry **entries,
                           int *nentries, int *cap)
{
    for (int a = from; a < to; a++) {
        mz_uint n = arcs[a].plain ? 1 : mz_zip_reader_get_num_files(&arcs[a].za);
        for (mz_uint i = 0; i < n; i++) {
            mz_zip_archive_file_stat st;
            if (*nentries == *cap)
                *entries = xrealloc(*entries, (size_t)(*cap = *cap ? *cap * 2 : 64) *
                                                  sizeof **entries);
            zip_entry *e = &(*entries)[*nentries];
            if (arcs[a].plain) {
                e->size = (long long)arcs[a].plain_size;
                e->crc = arcs[a].plain_crc;
                e->name = xstrdup(arcs[a].plain_name ? arcs[a].plain_name : arcs[a].label);
            } else {
                if (!mz_zip_reader_file_stat(&arcs[a].za, i, &st) || st.m_is_directory)
                    continue;
                e->size = (long long)st.m_uncomp_size;
                e->crc = st.m_crc32;
                e->name = xstrdup(st.m_filename);
            }
            (*nentries)++;
            e->arch = a;
            e->index = i;
            e->in_subfolder = strchr(e->name, '/') || strchr(e->name, '\\');
            e->base = e->name;
            for (const char *s = e->name; *s; s++)
                if (*s == '/' || *s == '\\')
                    e->base = s + 1;
        }
    }
}

/* join_parts(): the BIOS file f, made of the entries that its "parts" name,
   joined in that order, as one more archive holding just that file; nothing
   when a part is not there or cannot be read, which goes to res->errors. */
static void join_parts(const ua_file *f, archive **arcs, int *narcs, int *cap,
                       const zip_entry *entries, int nentries, result *res)
{
    unsigned char *data = xmalloc(1);
    size_t len = 0;
    for (int p = 0; p < f->nparts; p++) {
        const char *part = f->parts[p];
        const zip_entry *best = NULL;
        for (int k = 0; k < nentries; k++)
            if (ieq(entries[k].base, part) && (!best || rank(&entries[k], part) < rank(best, part)))
                best = &entries[k];
        if (!best) {
            strv_push(&res->errors, fmt("%s holds no %s", (*arcs)[0].label, part));
            free(data);
            return;
        }
        archive *a = &(*arcs)[best->arch];
        size_t n = a->plain_size;
        void *bytes = a->plain;
        if (!a->plain && !(bytes = mz_zip_reader_extract_to_heap(&a->za, best->index, &n, 0))) {
            char *why = extract_error(&a->za, best->name);
            strv_push(&res->errors, fmt("%s:%s: %s", a->label, best->name, why));
            free(why);
            free(data);
            return;
        }
        data = xrealloc(data, len + n + 1);
        memcpy(data + len, bytes, n);
        len += n;
        if (!a->plain)
            mz_free(bytes);
    }
    add_plain(arcs, narcs, cap, (*arcs)[0].label, base_name(f->name), data, len);
}

static void install_core(const ua_core *core, const char *zip_dir, const strv *listing,
                         const char *sd_root, int dry_run, result *res, const ua_hooks *h)
{
    memset(res, 0, sizeof *res);
    strv paths = { 0 };
    for (int z = 0; z < core->nzips; z++) {
        for (int i = 0; i < listing->n; i++) {
            if (ieq(listing->v[i], core->zips[z])) {
                strv_push(&paths, path_join(zip_dir, listing->v[i]));
                strv_push(&res->used, xstrdup(listing->v[i]));
                break;
            }
        }
    }
    if (!paths.n) {
        char *zips = strv_join(core->zips, core->nzips, " or ");
        res->skipped = fmt("%s not found", zips);
        free(zips);
        return;
    }

    const char *doing = dry_run ? "Checking" : "Installing";
    archive *arcs = NULL;
    int narcs = 0, acap = 0;
    for (int i = 0; i < paths.n; i++) {
        file_event(h, core->name, res->used.v[i], UA_FILE_BUSY, doing);
        open_archives(core, paths.v[i], res->used.v[i], &arcs, &narcs, &acap, dry_run, res, h);
    }

    /* What the archive holds is known once it is open. */
    ua_core with_files;
    if (core->release && narcs) {
        with_files = *core;
        fill_release_files(&with_files, &arcs[0], sd_root, res);
        core = &with_files;
    }

    zip_entry *entries = NULL;
    int nentries = 0, cap = 0;
    index_archives(arcs, 0, narcs, &entries, &nentries, &cap);
    int opened = narcs;
    for (int i = 0; i < core->nfiles && opened; i++)
        if (core->files[i].nparts)
            join_parts(&core->files[i], &arcs, &narcs, &acap, entries, nentries, res);
    index_archives(arcs, opened, narcs, &entries, &nentries, &cap);

    /* The bytes hook follows the set's files from here, on its last zip's row. */
    file_event(h, core->name, res->used.v[res->used.n - 1], UA_FILE_BUSY, doing);
    char *dest = dest_dir(core, sd_root);
    for (int i = 0; i < core->nfiles && !is_cancelled(h); i++) {
        if (h->on_bytes)
            h->on_bytes(h->ctx, i, core->nfiles);
        const ua_file *f = &core->files[i];
        const char *wanted = base_name(f->name);
        char *target = file_target(dest, f->name);
        if (is_current(target, f->size, f->crc)) {
            res->current++;
            free(target);
            continue;
        }

        const zip_entry *best = NULL, *same_name = NULL;
        for (int k = 0; k < nentries; k++) {
            const zip_entry *e = &entries[k];
            if (e->size == f->size && e->crc == f->crc &&
                (!best || rank(e, wanted) < rank(best, wanted)))
                best = e;
            if (ieq(e->base, wanted) && (!same_name || rank(e, wanted) < rank(same_name, wanted)))
                same_name = e;
        }
        if (!best) {
            if (core->uf2 && narcs)
                strv_push(&res->errors, fmt("the UF2 image of %s is not the expected file "
                                            "(damaged, or from a different version)",
                                            arcs[0].label));
            else if (same_name)
                strv_push(&res->errors, fmt("%s:%s is not the expected file (damaged, or from a "
                                            "different version of the set)",
                                            arcs[same_name->arch].label, same_name->name));
            else
                strv_push(&res->missing, xstrdup(f->name));
            free(target);
            continue;
        }
        archive *a = &arcs[best->arch];
        char *source = fmt("%s:%s", a->label, best->name);

        if (dry_run) {
            char *bak = f->backup ? backup_name(target) : NULL;
            if (bak) {
                strv_push(&res->notes, fmt("would save %s as %s%s", f->name, f->name,
                                           bak + strlen(target)));
                free(bak);
            }
            res->written++;
        } else {
            size_t len = 0;
            void *data = NULL;
            char *why = NULL;
            if (a->plain) {
                data = a->plain;
                len = a->plain_size;
            } else {
                data = mz_zip_reader_extract_to_heap(&a->za, best->index, &len, 0);
                if (!data)
                    why = extract_error(&a->za, best->name);
            }
            if (!data) {
                strv_push(&res->errors, fmt("%s: %s", source, why));
            } else if ((long long)len != f->size ||
                       (uint32_t)mz_crc32(MZ_CRC32_INIT, data, len) != f->crc) {
                strv_push(&res->errors, fmt("%s: contents do not match the size and CRC", source));
            } else if ((f->backup && save_backup(f, target, res, &why)) ||
                       write_file(target, data, len, &why)) {
                strv_push(&res->errors, fmt("cannot write %s: %s", target, why));
            } else {
                res->written++;
            }
            free(why);
            if (!a->plain)
                mz_free(data);
        }
        free(source);
        free(target);
    }
    free(dest);
    if (core == &with_files)
        free_files(&with_files);

    for (int k = 0; k < nentries; k++)
        free(entries[k].name);
    free(entries);
    for (int a = 0; a < narcs; a++) {
        if (arcs[a].opened)
            mz_zip_reader_end(&arcs[a].za);
        mz_free(arcs[a].mem);
        free(arcs[a].plain);
        free(arcs[a].plain_name);
        free(arcs[a].label);
    }
    free(arcs);
    strv_free(&paths);
}

static void result_free(result *r)
{
    strv_free(&r->missing);
    strv_free(&r->errors);
    strv_free(&r->notes);
    strv_free(&r->used);
    free(r->skipped);
}

/* ------------------------------------------------------------------------- */
/* main() and run()                                                          */
/* ------------------------------------------------------------------------- */

static int run(const ua_core **cores, int ncores, int requested, int dry_run,
               const char *sd, const char *zip_dir, databases *dbs, int url_downloads,
               const ua_hooks *h)
{
    strv listing = { 0 };
    if (ua_is_dir(zip_dir)) {
        char **names = ua_list_files(zip_dir);
        for (char **p = names; p && *p; p++)
            strv_push(&listing, xstrdup(*p));
        ua_free_list(names);
    }

    int complete = 0, skipped = 0, failed = 0, to_download = 0, cancelled = 0;
    const ua_release *firmware = NULL;
    for (int c = 0; c < ncores; c++) {
        const ua_core *core = cores[c];
        if (h->on_set)
            h->on_set(h->ctx, c, ncores);
        if (is_cancelled(h)) {
            cancelled = 1;
            break;
        }
        const ua_release *release = core->release;
        if (release && !release->newer) {
            complete++;
            out(h, "%s: %s", core->name, core->dest_shown);
            out(h, "  %s up to date", release->card);
            continue;
        }
        /* A set that is complete on the card needs no zip, so nothing is
           opened or downloaded for it. What the archive holds is not known yet. */
        if (!release && ua_count_current(core, sd) == core->nfiles) {
            complete++;
            out(h, "%s: %s", core->name, core->dest_shown);
            out(h, "  %d up to date", core->nfiles);
            continue;
        }

        if (release)
            forget_stale_zip(core, zip_dir, &listing);
        strv notes = { 0 }, errors = { 0 };
        int pending = fetch_missing_zips(core, zip_dir, &listing, dbs, dry_run, url_downloads,
                                         &notes, &errors, h);
        result res;
        install_core(core, zip_dir, &listing, sd, dry_run, &res, h);
        int finished = !res.skipped && !res.missing.n && !res.errors.n && !is_cancelled(h);
        if (release && finished && !dry_run) {
            char *why = NULL;
            if (write_marker(sd, release->tag, &why)) {
                strv_push(&res.errors, fmt("cannot write %s: %s", RELEASE_MARKER, why));
                free(why);
                finished = 0;
            }
        }
        if (release && release->firmware && (finished || pending))
            firmware = release;
        for (int i = 0; i < res.errors.n; i++)
            strv_push(&errors, xstrdup(res.errors.v[i]));
        int dl_errors = errors.n - res.errors.n;

        if (res.skipped && !pending && !dl_errors) {
            skipped++;
            out(h, "%s: skipped, %s%s", core->name, res.skipped,
                dbs->nloaded && !core->url ? ", and not in any database" : "");
            /* A cancelled run did not look in the databases for it. */
            if (!is_cancelled(h)) {
                char *zips = strv_join(core->zips, core->nzips, " or ");
                file_event(h, core->name, zips, UA_FILE_NOTE,
                           dbs->nloaded && !core->url ? "Not found, and not in any database"
                                                      : "Not found");
                free(zips);
            }
        } else {
            char *used = res.used.n ? strv_join(res.used.v, res.used.n, ", ")
                                    : strv_join(core->zips, core->nzips, ", ");
            out(h, "%s: %s -> %s", core->name, used, core->dest_shown);
            free(used);
            for (int i = 0; i < notes.n; i++)
                out(h, "  %s", notes.v[i]);
            for (int i = 0; i < res.notes.n; i++)
                out(h, "  %s", res.notes.v[i]);
            if (res.skipped) {
                for (int i = 0; i < errors.n; i++)
                    out(h, "  ERROR: %s", errors.v[i]);
                if (pending)
                    to_download++;
                else
                    failed++;
            } else {
                char parts[64] = "";
                if (res.written)
                    snprintf(parts, sizeof parts, "%d %s", res.written,
                             dry_run ? "to write" : "written");
                if (res.current)
                    snprintf(parts + strlen(parts), sizeof parts - strlen(parts), "%s%d up to date",
                             res.written ? ", " : "", res.current);
                if (*parts)
                    out(h, "  %s", parts);
                if (res.missing.n) {
                    char *list = strv_join(res.missing.v, res.missing.n, ", ");
                    out(h, "  INCOMPLETE: %d of %d files not in the zip: %s", res.missing.n,
                        core->nfiles, list);
                    free(list);
                }
                for (int i = 0; i < errors.n; i++)
                    out(h, "  ERROR: %s", errors.v[i]);
                if (!res.missing.n && !errors.n && !is_cancelled(h))
                    complete++;
                else
                    failed++;
                /* res.errors, not errors: a good zip is not to blame for a
                   download of another zip of the set that failed. */
                char *absent = fmt("%d of %d files not in the zip", res.missing.n, core->nfiles);
                for (int i = 0; i < res.used.n; i++) {
                    if (res.errors.n)
                        file_event(h, core->name, res.used.v[i], UA_FILE_FAILED, res.errors.v[0]);
                    else if (res.missing.n)
                        file_event(h, core->name, res.used.v[i], UA_FILE_FAILED, absent);
                    else if (is_cancelled(h))
                        file_event(h, core->name, res.used.v[i], UA_FILE_CANCELLED, "Cancelled");
                    else
                        file_event(h, core->name, res.used.v[i],
                                   dry_run ? UA_FILE_NOTE : UA_FILE_DONE, *parts ? parts : "Done");
                }
                free(absent);
            }
        }
        strv_free(&notes);
        strv_free(&errors);
        result_free(&res);
    }
    strv_free(&listing);

    out(h, "");
    if (firmware) {
        out(h, "Firmware:     flash the board with the bootloader of %s as well; "
               "updateAll cannot do that: %s", firmware->tag, firmware->page);
        char *file = fmt("Bootloader of %s", firmware->tag);
        char *detail = fmt("Flash the board as well: %s", firmware->page);
        file_event(h, "Firmware", file, UA_FILE_NOTE, detail);
        free(detail);
        free(file);
    }
    char *summary = fmt("%d complete, %d incomplete, %d skipped", complete, failed, skipped);
    if (to_download)
        out(h, "%s, %d to download", summary, to_download);
    else
        out(h, "%s", summary);
    free(summary);
    if (cancelled || is_cancelled(h)) {
        out(h, "Cancelled");
        return 1;
    }

    int requested_skipped = requested && skipped > 0;
    int ok = (complete || to_download) && !failed && !requested_skipped && !dbs->nerrors;
    return ok ? 0 : 1;
}

int ua_run(const ua_config *cfg, const ua_options *opt, const ua_hooks *h)
{
    strv sources = { 0 };
    for (int i = 0; i < cfg->ndatabases; i++)
        strv_push(&sources, xstrdup(cfg->databases[i]));
    for (const char **d = opt->extra_dbs; d && *d; d++)
        strv_push(&sources, ua_is_url(*d) ? xstrdup(*d) : ua_abspath(*d));

    const ua_core **cores = xmalloc((size_t)(cfg->ncores + 1) * sizeof *cores);
    int ncores = 0, rc = 1, requested = 0;
    char *temp_dir = NULL, *archive_zip = NULL;
    ua_release release = { 0 };

    if (!opt->sd || !ua_is_dir(opt->sd)) {
        out_err(h, "ERROR: SD card root not found: %s", opt->sd ? opt->sd : "");
        goto done;
    }
    /* Game data has its own URL, so it can be downloaded without a database. */
    int use_databases = sources.n > 0 && !opt->no_download;
    int url_downloads = !opt->no_download, any_url = 0;
    for (int i = 0; i < cfg->ncores; i++)
        any_url |= cfg->cores[i].url != NULL;
    int may_download = use_databases || (url_downloads && (any_url || cfg->sd_asset));
    const char *zips = opt->zips && *opt->zips ? opt->zips : NULL;
    if (!zips && !may_download) {
        out_err(h, "ERROR: nothing to install from: give a zip folder with --zips, "
                   "or configure a database to download from");
        goto done;
    }
    if (zips && !ua_is_dir(zips) && !may_download) {
        out_err(h, "ERROR: zip folder not found: %s", zips);
        goto done;
    }

    if (opt->core_ids && opt->core_ids[0]) {
        requested = 1;
        int any_bios = 0;
        for (int i = 0; i < cfg->ncores; i++)
            any_bios |= cfg->cores[i].bios;
        strv unknown = { 0 };
        for (const char **id = opt->core_ids; *id; id++) {
            int known = (any_bios && ieq(*id, "bios")) ||  /* every BIOS file */
                        (cfg->sd_asset && ieq(*id, "sdcard"));  /* the SD-card archive */
            for (int i = 0; i < cfg->ncores && !known; i++)
                known = ieq(cfg->cores[i].id, *id);
            if (!known) {
                char *l = xstrdup(*id);
                for (char *p = l; *p; p++)
                    *p = (char)lower((unsigned char)*p);
                strv_push(&unknown, l);
            }
        }
        if (unknown.n) {
            strv ids = { 0 };
            for (int i = 0; i < cfg->ncores; i++)
                strv_push(&ids, xstrdup(cfg->cores[i].id));
            char *u = strv_join(unknown.v, unknown.n, ", "), *k = strv_join(ids.v, ids.n, ", ");
            out_err(h, "ERROR: unknown core: %s. Configured: %s", u, k);
            free(u);
            free(k);
            strv_free(&ids);
            strv_free(&unknown);
            goto done;
        }
        for (int i = 0; i < cfg->ncores; i++)
            for (const char **id = opt->core_ids; *id; id++)
                if (ieq(cfg->cores[i].id, *id) || (cfg->cores[i].bios && ieq(*id, "bios"))) {
                    cores[ncores++] = &cfg->cores[i];
                    break;
                }
    } else {
        for (int i = 0; i < cfg->ncores; i++)
            cores[ncores++] = &cfg->cores[i];
    }

    /* The check is separate from installing: a newer archive is reported on a
       card with an emu folder even when this run does not install it. */
    int has_emu = ua_card_has_emu(opt->sd), asked = opt->sdcard > 0;
    for (const char **id = opt->core_ids; id && *id; id++)
        asked |= ieq(*id, "sdcard");
    char release_error[1024] = "";
    if (cfg->sd_asset && (has_emu || asked)) {
        if (opt->release) {
            release.tag = xstrdup(opt->release->tag);
            release.page = xstrdup(opt->release->page);
            release.url = xstrdup(opt->release->url);
            release.size = opt->release->size;
        } else if (ua_release_fetch(cfg, &release, release_error, sizeof release_error)) {
            memset(&release, 0, sizeof release);
        }
        if (release.tag) {
            char *card = ua_card_release(opt->sd);
            ua_release_compare(&release, card);
            free(card);
        }
    }

    if (cfg->config_url && cfg->fetch_error)
        out(h, "Sets from:    %s (%s)", cfg->source, cfg->fetch_error);
    else if (cfg->config_url)
        out(h, "Sets from:    %s", cfg->source);
    char *abs = zips ? ua_abspath(zips) : NULL;
    out(h, "Zips from:    %s", abs ? abs : "downloads only");
    free(abs);
    abs = ua_abspath(opt->sd);
    out(h, "SD card:      %s", abs);
    free(abs);
    if (*release_error)
        out(h, "New archive:  could not check: %s", release_error);
    else if (release.tag && has_emu && release.newer)
        out(h, "New archive:  %s %s is available (card: %s): %s", cfg->sd_asset, release.tag,
            release.card ? release.card : "unknown", release.page);
    if (use_databases)
        out(h, "Databases:    %d, used for zips that are not in the zip folder", sources.n);
    if (opt->dry_run)
        out(h, "Dry run:      nothing is downloaded or written");
    out(h, "");

    /* Without a zip folder, downloads go to a temporary folder that is
       removed again. */
    if (!zips && !(temp_dir = ua_make_temp_dir())) {
        out_err(h, "ERROR: cannot create a temporary folder: %s", ua_last_error());
        goto done;
    }
    /* The archive as a set, first; the name of its zip holds the tag, as a
       kept zip is otherwise never downloaded again. */
    ua_core archive_core;
    char *zip_list[2] = { NULL, NULL }, *no_parts[1] = { NULL };
    if (release.tag && (asked || (!opt->sdcard && !requested && has_emu && !opt->no_download))) {
        size_t stem = strlen(cfg->sd_asset) - 4;
        archive_zip = fmt("%.*s-%s%s", (int)stem, cfg->sd_asset, release.tag, cfg->sd_asset + stem);
        zip_list[0] = archive_zip;
        memset(&archive_core, 0, sizeof archive_core);
        archive_core.id = (char *)"sdcard";
        archive_core.name = cfg->sd_name;
        archive_core.zips = zip_list;
        archive_core.nzips = 1;
        archive_core.dest_parts = no_parts;
        archive_core.dest_shown = (char *)"/";
        archive_core.url = release.url;
        archive_core.archives = no_parts;
        archive_core.release = &release;
        memmove(cores + 1, cores, (size_t)ncores * sizeof *cores);
        cores[0] = &archive_core;
        ncores++;
    }
    databases dbs = { 0 };
    dbs.sources = use_databases ? sources.v : NULL;
    dbs.nsources = use_databases ? sources.n : 0;
    dbs.h = h;
    rc = run(cores, ncores, requested, opt->dry_run, opt->sd, zips ? zips : temp_dir, &dbs,
             url_downloads, h);
    databases_free(&dbs);

done:
    if (temp_dir) {
        ua_remove_tree(temp_dir);
        free(temp_dir);
    }
    free(archive_zip);
    ua_release_free(&release);
    free(cores);
    strv_free(&sources);
    return rc;
}
