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

static int parse_core(const cJSON *d, ua_core *c, strv *ids, const char *path,
                      char *err, size_t errlen)
{
    const char *cid = str_field(d, "id");
    if (!cid)
        cid = "?";
    static const char *const keys[] = { "id", "name", "zips", "destination", "files" };
    for (int k = 0; k < 5; k++) {
        if (!cJSON_IsObject(d) || !cJSON_GetObjectItemCaseSensitive(d, keys[k])) {
            set_err(err, errlen, "%s: core '%s' has no '%s'", path, cid, keys[k]);
            return -1;
        }
    }
    const char *name = str_field(d, "name"), *destination = str_field(d, "destination");
    const cJSON *zips = cJSON_GetObjectItemCaseSensitive(d, "zips");
    const cJSON *files = cJSON_GetObjectItemCaseSensitive(d, "files");
    if (!str_field(d, "id") || !name || !destination || !cJSON_IsArray(zips) ||
        !cJSON_IsArray(files)) {
        set_err(err, errlen, "%s: core '%s': 'id', 'name' and 'destination' must be "
                "strings, 'zips' and 'files' lists", path, cid);
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
        free(dest);
        return -1;
    }
    c->dest_parts = parts.v;
    c->dest_shown = fmt("/%s", s);
    free(dest);

    if (!cJSON_GetArraySize(zips) || !cJSON_GetArraySize(files)) {
        set_err(err, errlen, "%s: core '%s' needs at least one zip and one file", path, cid);
        return -1;
    }
    strv zv = { 0 };
    const cJSON *z;
    cJSON_ArrayForEach(z, zips) {
        if (!cJSON_IsString(z) || bad_name(z->valuestring)) {
            set_err(err, errlen, "%s: core '%s' has an invalid zip name", path, cid);
            strv_free(&zv);
            return -1;
        }
        strv_push(&zv, xstrdup(z->valuestring));
    }
    c->zips = zv.v;
    c->nzips = zv.n;

    c->files = xmalloc((size_t)cJSON_GetArraySize(files) * sizeof *c->files);
    const cJSON *f;
    cJSON_ArrayForEach(f, files) {
        const char *fname = cJSON_IsObject(f) ? str_field(f, "name") : NULL;
        if (!fname || bad_name(fname)) {
            set_err(err, errlen, "%s: core '%s' has an invalid file name '%s'", path, cid,
                    fname ? fname : "");
            return -1;
        }
        ua_file *uf = &c->files[c->nfiles];
        if (parse_crc(cJSON_GetObjectItemCaseSensitive(f, "crc"), &uf->crc) ||
            parse_int(cJSON_GetObjectItemCaseSensitive(f, "size"), &uf->size)) {
            set_err(err, errlen, "%s: core '%s', file '%s' needs a numeric 'size' and a "
                    "hexadecimal 'crc'", path, cid, fname);
            return -1;
        }
        uf->name = xstrdup(fname);
        c->nfiles++;
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
    cfg->cores = xmalloc((size_t)cJSON_GetArraySize(cores) * sizeof *cfg->cores);
    const cJSON *d;
    cJSON_ArrayForEach(d, cores) {
        ua_core *c = &cfg->cores[cfg->ncores++];
        memset(c, 0, sizeof *c);
        if (parse_core(d, c, &ids, path, err, errlen))
            goto done;
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
        for (int k = 0; k < c->nfiles; k++)
            free(c->files[k].name);
        free(c->files);
    }
    free(cfg->cores);
    free_list(cfg->databases);
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
        if (ua_http_get(source, mem_sink, &m, &status, why, sizeof why)) {
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
            if (database_load(&d->loaded[d->nloaded], d->sources[i], &err)) {
                d->nerrors++;
                out(d->h, "ERROR: database %s: %s", d->sources[i], err);
                free(err);
            } else {
                d->nloaded++;
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
        s->h->on_bytes(s->h->ctx, s->got, total);
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
static long long download(const db_hit *hit, const char *dest, const ua_hooks *h, char **reason)
{
    char *tmp = fmt("%s.part", dest);
    char *why = xstrdup("no attempt made");
    long long result = -1;
    for (int attempt = 0; attempt < DOWNLOAD_ATTEMPTS; attempt++) {
        if (attempt && sleep_unless_cancelled(h, 2000 * attempt)) {
            free(why);
            why = xstrdup("cancelled");
            break;
        }
        dl_state s = { 0 };
        s.h = h;
        ua_md5_init(&s.md5);
        int stop = 0;
        char err[512] = "";
        s.out = ua_fopen(tmp, "wb");
        if (!s.out) {
            free(why);
            why = os_error(errno, tmp);
        } else {
            int status = 0;
            int rc = ua_http_get(hit->url, dl_sink, &s, &status, err, sizeof err);
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
                else if (!is_zip_file(tmp))
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

/* fetch_missing_zips(); returns whether a dry run would have downloaded. */
static int fetch_missing_zips(const ua_core *core, const char *zip_dir, strv *listing,
                              databases *dbs, int dry_run, strv *notes, strv *errors,
                              const ua_hooks *h)
{
    int pending = 0;
    for (int z = 0; z < core->nzips; z++) {
        const char *zname = core->zips[z];
        int have = 0;
        for (int i = 0; i < listing->n && !have; i++)
            have = ieq(listing->v[i], zname);
        if (have)
            continue;
        db_hit hit;
        if (!databases_find(dbs, zname, &hit))
            continue;
        if (dry_run) {
            strv_push(notes, fmt("would download %s from %s", zname, hit.db->id));
            pending = 1;
            hit_free(&hit);
            continue;
        }
        if (is_cancelled(h)) {
            hit_free(&hit);
            break;
        }
        char *why = NULL;
        long long got = -1;
        if (ua_mkdirs(zip_dir)) {
            why = xstrdup(ua_last_error());
        } else {
            char *dest = path_join(zip_dir, zname);
            got = download(&hit, dest, h, &why);
            free(dest);
        }
        if (got < 0) {
            strv_push(errors, fmt("download of %s from %s failed: %s", zname, hit.db->id, why));
            free(why);
        } else {
            strv_push(listing, xstrdup(zname));
            strv_push(notes, fmt("downloaded %s (%" PRId64 " KB) from %s", zname,
                                 (int64_t)((got + 1023) / 1024), hit.db->id));
        }
        hit_free(&hit);
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

int ua_count_current(const ua_core *core, const char *sd_root)
{
    char *dest = dest_dir(core, sd_root);
    int n = 0;
    for (int i = 0; i < core->nfiles; i++) {
        char *target = path_join(dest, core->files[i].name);
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
    strv missing, errors;
    char *skipped;
    strv used;
} result;

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

static int write_file(const char *dest, const char *target, const void *data, size_t len,
                      char **why)
{
    if (ua_mkdirs(dest)) {
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

    mz_zip_archive *archives = xmalloc((size_t)paths.n * sizeof *archives);
    int *opened = xmalloc((size_t)paths.n * sizeof *opened);
    zip_entry *entries = NULL;
    int nentries = 0, cap = 0;
    for (int a = 0; a < paths.n; a++) {
        mz_zip_zero_struct(&archives[a]);
        errno = 0;
        opened[a] = mz_zip_reader_init_file(&archives[a], paths.v[a], 0);
        if (!opened[a]) {
            char *why = open_error(&archives[a], paths.v[a]);
            strv_push(&res->errors, fmt("cannot open %s: %s", base_name(paths.v[a]), why));
            free(why);
            continue;
        }
        mz_uint n = mz_zip_reader_get_num_files(&archives[a]);
        for (mz_uint i = 0; i < n; i++) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&archives[a], i, &st) || st.m_is_directory)
                continue;
            if (nentries == cap)
                entries = xrealloc(entries, (size_t)(cap = cap ? cap * 2 : 64) * sizeof *entries);
            zip_entry *e = &entries[nentries++];
            e->arch = a;
            e->index = i;
            e->size = (long long)st.m_uncomp_size;
            e->crc = st.m_crc32;
            e->name = xstrdup(st.m_filename);
            e->in_subfolder = strchr(e->name, '/') || strchr(e->name, '\\');
            e->base = e->name;
            for (const char *s = e->name; *s; s++)
                if (*s == '/' || *s == '\\')
                    e->base = s + 1;
        }
    }

    char *dest = dest_dir(core, sd_root);
    for (int i = 0; i < core->nfiles && !is_cancelled(h); i++) {
        const ua_file *f = &core->files[i];
        char *target = path_join(dest, f->name);
        if (is_current(target, f->size, f->crc)) {
            res->current++;
            free(target);
            continue;
        }

        const zip_entry *best = NULL, *same_name = NULL;
        for (int k = 0; k < nentries; k++) {
            const zip_entry *e = &entries[k];
            if (e->size == f->size && e->crc == f->crc &&
                (!best || rank(e, f->name) < rank(best, f->name)))
                best = e;
            if (ieq(e->base, f->name) && (!same_name || rank(e, f->name) < rank(same_name, f->name)))
                same_name = e;
        }
        if (!best) {
            if (same_name)
                strv_push(&res->errors, fmt("%s:%s is not the expected file (damaged, or from a "
                                            "different version of the set)",
                                            res->used.v[same_name->arch], same_name->name));
            else
                strv_push(&res->missing, xstrdup(f->name));
            free(target);
            continue;
        }
        char *source = fmt("%s:%s", res->used.v[best->arch], best->name);

        if (dry_run) {
            res->written++;
        } else {
            size_t len = 0;
            void *data = mz_zip_reader_extract_to_heap(&archives[best->arch], best->index, &len, 0);
            char *why = NULL;
            if (!data) {
                why = extract_error(&archives[best->arch], best->name);
                strv_push(&res->errors, fmt("%s: %s", source, why));
            } else if ((long long)len != f->size ||
                       (uint32_t)mz_crc32(MZ_CRC32_INIT, data, len) != f->crc) {
                strv_push(&res->errors, fmt("%s: contents do not match the size and CRC", source));
            } else if (write_file(dest, target, data, len, &why)) {
                strv_push(&res->errors, fmt("cannot write %s: %s", target, why));
            } else {
                res->written++;
            }
            free(why);
            mz_free(data);
        }
        free(source);
        free(target);
    }
    free(dest);

    for (int k = 0; k < nentries; k++)
        free(entries[k].name);
    free(entries);
    for (int a = 0; a < paths.n; a++)
        if (opened[a])
            mz_zip_reader_end(&archives[a]);
    free(opened);
    free(archives);
    strv_free(&paths);
}

static void result_free(result *r)
{
    strv_free(&r->missing);
    strv_free(&r->errors);
    strv_free(&r->used);
    free(r->skipped);
}

/* ------------------------------------------------------------------------- */
/* main() and run()                                                          */
/* ------------------------------------------------------------------------- */

static int run(const ua_core **cores, int ncores, int requested, int dry_run,
               const char *sd, const char *zip_dir, databases *dbs, const ua_hooks *h)
{
    strv listing = { 0 };
    if (ua_is_dir(zip_dir)) {
        char **names = ua_list_files(zip_dir);
        for (char **p = names; p && *p; p++)
            strv_push(&listing, xstrdup(*p));
        ua_free_list(names);
    }

    int complete = 0, skipped = 0, failed = 0, to_download = 0, cancelled = 0;
    for (int c = 0; c < ncores; c++) {
        const ua_core *core = cores[c];
        if (h->on_set)
            h->on_set(h->ctx, c, ncores);
        if (is_cancelled(h)) {
            cancelled = 1;
            break;
        }
        /* A set that is complete on the card needs no zip, so nothing is
           opened or downloaded for it. */
        if (ua_count_current(core, sd) == core->nfiles) {
            complete++;
            out(h, "%s: %s", core->name, core->dest_shown);
            out(h, "  %d up to date", core->nfiles);
            continue;
        }

        strv notes = { 0 }, errors = { 0 };
        int pending = fetch_missing_zips(core, zip_dir, &listing, dbs, dry_run, &notes, &errors, h);
        result res;
        install_core(core, zip_dir, &listing, sd, dry_run, &res, h);
        for (int i = 0; i < res.errors.n; i++)
            strv_push(&errors, xstrdup(res.errors.v[i]));
        int dl_errors = errors.n - res.errors.n;

        if (res.skipped && !pending && !dl_errors) {
            skipped++;
            out(h, "%s: skipped, %s%s", core->name, res.skipped,
                dbs->nloaded ? ", and not in any database" : "");
        } else {
            char *used = res.used.n ? strv_join(res.used.v, res.used.n, ", ")
                                    : strv_join(core->zips, core->nzips, ", ");
            out(h, "%s: %s -> %s", core->name, used, core->dest_shown);
            free(used);
            for (int i = 0; i < notes.n; i++)
                out(h, "  %s", notes.v[i]);
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
            }
        }
        strv_free(&notes);
        strv_free(&errors);
        result_free(&res);
    }
    strv_free(&listing);

    out(h, "");
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

    const ua_core **cores = xmalloc((size_t)cfg->ncores * sizeof *cores);
    int ncores = 0, rc = 1, requested = 0;
    char *temp_dir = NULL;

    if (!opt->sd || !ua_is_dir(opt->sd)) {
        out_err(h, "ERROR: SD card root not found: %s", opt->sd ? opt->sd : "");
        goto done;
    }
    int may_download = sources.n > 0 && !opt->no_download;
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
        strv unknown = { 0 };
        for (const char **id = opt->core_ids; *id; id++) {
            int known = 0;
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
                if (ieq(cfg->cores[i].id, *id)) {
                    cores[ncores++] = &cfg->cores[i];
                    break;
                }
    } else {
        for (int i = 0; i < cfg->ncores; i++)
            cores[ncores++] = &cfg->cores[i];
    }

    char *abs = zips ? ua_abspath(zips) : NULL;
    out(h, "Zips from:    %s", abs ? abs : "downloads only");
    free(abs);
    abs = ua_abspath(opt->sd);
    out(h, "SD card:      %s", abs);
    free(abs);
    if (may_download)
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
    databases dbs = { 0 };
    dbs.sources = may_download ? sources.v : NULL;
    dbs.nsources = may_download ? sources.n : 0;
    dbs.h = h;
    rc = run(cores, ncores, requested, opt->dry_run, opt->sd, zips ? zips : temp_dir, &dbs, h);
    databases_free(&dbs);

done:
    if (temp_dir) {
        ua_remove_tree(temp_dir);
        free(temp_dir);
    }
    free(cores);
    strv_free(&sources);
    return rc;
}
