/*
 * ua_plat.h on Linux, for the host test build only. Error texts follow
 * Python's OSError, so that the output can be compared with updateAll.py.
 * HTTP goes through the curl command-line tool.
 */
#define _GNU_SOURCE
#include "../ua_plat.h"

#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static char last_error[2048];

static void os_error(int err, const char *path)
{
    snprintf(last_error, sizeof last_error, "[Errno %d] %s: '%s'", err, strerror(err), path);
}

const char *ua_last_error(void)
{
    return last_error;
}

FILE *ua_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

int ua_is_dir(const char *path)
{
    struct stat st;
    return !stat(path, &st) && S_ISDIR(st.st_mode);
}

int ua_file_size(const char *path, long long *size)
{
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode))
        return -1;
    *size = (long long)st.st_size;
    return 0;
}

char **ua_list_files(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return NULL;
    size_t n = 0, cap = 16;
    char **list = malloc(cap * sizeof *list);
    struct dirent *e;
    while ((e = readdir(d))) {
        char path[4096];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (stat(path, &st) || !S_ISREG(st.st_mode))
            continue;
        if (n + 2 > cap)
            list = realloc(list, (cap *= 2) * sizeof *list);
        list[n++] = strdup(e->d_name);
    }
    closedir(d);
    list[n] = NULL;
    return list;
}

void ua_free_list(char **list)
{
    if (!list)
        return;
    for (char **p = list; *p; p++)
        free(*p);
    free(list);
}

int ua_mkdirs(const char *path)
{
    char *p = strdup(path);
    for (char *s = p + 1;; s++) {
        if (*s == '/' || !*s) {
            char c = *s;
            *s = 0;
            if (mkdir(p, 0777) && errno != EEXIST) {
                os_error(errno, p);
                free(p);
                return -1;
            }
            *s = c;
            if (!c)
                break;
        }
    }
    free(p);
    if (!ua_is_dir(path)) {
        os_error(EEXIST, path);
        return -1;
    }
    return 0;
}

int ua_replace(const char *from, const char *to)
{
    if (rename(from, to)) {
        snprintf(last_error, sizeof last_error, "[Errno %d] %s: '%s' -> '%s'", errno,
                 strerror(errno), from, to);
        return -1;
    }
    return 0;
}

int ua_remove(const char *path)
{
    if (unlink(path)) {
        os_error(errno, path);
        return -1;
    }
    return 0;
}

char *ua_make_temp_dir(void)
{
    const char *tmp = getenv("TMPDIR");
    char path[4096];
    snprintf(path, sizeof path, "%s/updateAll-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(path)) {
        os_error(errno, path);
        return NULL;
    }
    return strdup(path);
}

static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *ftw)
{
    (void)st, (void)flag, (void)ftw;
    remove(path);
    return 0;
}

void ua_remove_tree(const char *path)
{
    nftw(path, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
}

/* os.path.abspath: joined to the working directory and normalised, without
   resolving links. */
char *ua_abspath(const char *path)
{
    char cwd[4096] = "";
    if (path[0] != '/' && !getcwd(cwd, sizeof cwd))
        cwd[0] = 0;
    size_t len = strlen(cwd) + strlen(path) + 2;
    char *in = malloc(len), *out = malloc(len + 1);
    snprintf(in, len, "%s/%s", cwd, path);
    size_t o = 0;
    for (char *tok = strtok(in, "/"); tok; tok = strtok(NULL, "/")) {
        if (!strcmp(tok, "."))
            continue;
        if (!strcmp(tok, "..")) {
            while (o && out[o - 1] != '/')
                o--;
            if (o)
                o--;
            continue;
        }
        out[o++] = '/';
        strcpy(out + o, tok);
        o += strlen(tok);
    }
    if (!o)
        out[o++] = '/';
    out[o] = 0;
    free(in);
    return out;
}

void ua_sleep_ms(int ms)
{
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void shell_quote(char *dst, size_t size, const char *s)
{
    size_t o = 0;
    dst[o++] = '\'';
    for (; *s && o + 5 < size; s++) {
        if (*s == '\'') {
            memcpy(dst + o, "'\\''", 4);
            o += 4;
        } else {
            dst[o++] = *s;
        }
    }
    dst[o++] = '\'';
    dst[o] = 0;
}

int ua_http_get(const char *url, ua_sink sink, void *ctx, int *status, char *err, size_t errlen)
{
    char hdr[] = "/tmp/ua_hdr_XXXXXX", body[] = "/tmp/ua_body_XXXXXX", cerr[] = "/tmp/ua_err_XXXXXX";
    int fds[3] = { mkstemp(hdr), mkstemp(body), mkstemp(cerr) };
    for (int i = 0; i < 3; i++)
        if (fds[i] >= 0)
            close(fds[i]);
    char qurl[8192], cmd[9000];
    shell_quote(qurl, sizeof qurl, url);
    snprintf(cmd, sizeof cmd,
             "curl -sS -L --max-time 60 -A 'updateAll (pico-bootLoader)' -D %s -o %s %s 2>%s",
             hdr, body, qurl, cerr);
    int rc = system(cmd);
    *status = 0;

    /* The last status line wins: curl -L writes the headers of every redirect. */
    int code = 0;
    char reason[256] = "";
    FILE *f = fopen(hdr, "r");
    char line[1024];
    while (f && fgets(line, sizeof line, f)) {
        int c, n = 0;
        if (sscanf(line, "HTTP/%*s %d%n", &c, &n) == 1) {
            code = c;
            char *r = line + n;
            while (*r == ' ')
                r++;
            r[strcspn(r, "\r\n")] = 0;
            snprintf(reason, sizeof reason, "%s", r);
        }
    }
    if (f)
        fclose(f);

    int result = -1;
    if (rc != 0 && !code) {
        snprintf(err, errlen, "curl failed");
        f = fopen(cerr, "r");
        if (f && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            snprintf(err, errlen, "%s", line);
        }
        if (f)
            fclose(f);
    } else if (code < 200 || code > 299) {
        *status = code;
        snprintf(err, errlen, "HTTP %d %s", code, reason);
    } else if (rc != 0) {
        snprintf(err, errlen, "transfer failed");
    } else if ((f = fopen(body, "rb"))) {
        struct stat st;
        long long total = stat(body, &st) ? -1 : (long long)st.st_size;
        char buf[65536];
        size_t n;
        result = 0;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
            if (sink(ctx, buf, n, total)) {
                snprintf(err, errlen, "cancelled");
                result = -1;
                break;
            }
        }
        fclose(f);
    }
    unlink(hdr);
    unlink(body);
    unlink(cerr);
    return result;
}
