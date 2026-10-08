/*
 * Test driver: runs the updateAll core with the command line of
 * updateAll.py, so that compare.sh can check that both give the same output.
 * Not shipped; updateAll.exe has a window instead.
 */
#include "../ua_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void log_line(void *ctx, int to_stderr, const char *line)
{
    (void)ctx;
    FILE *f = to_stderr ? stderr : stdout;
    fputs(line, f);
    fputc('\n', f);
}

/* Appends the comma-separated items of arg to list. */
static void split(const char ***list, int *n, char *arg)
{
    for (char *tok = strtok(arg, ","); tok; tok = strtok(NULL, ",")) {
        while (*tok == ' ')
            tok++;
        size_t len = strlen(tok);
        while (len && tok[len - 1] == ' ')
            tok[--len] = 0;
        if (!*tok)
            continue;
        *list = realloc(*list, (size_t)(*n + 2) * sizeof **list);
        (*list)[(*n)++] = tok;
        (*list)[*n] = NULL;
    }
}

int main(int argc, char **argv)
{
    ua_options opt = { 0 };
    const char *config = "updateAll.json";
    const char **cores = NULL, **dbs = NULL;
    int ncores = 0, ndbs = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--md5") && v) {          /* MD5 of a string, for the self-test */
            ua_md5 m;
            char hex[33];
            ua_md5_init(&m);
            ua_md5_update(&m, v, strlen(v));
            ua_md5_hex(&m, hex);
            puts(hex);
            return 0;
        } else if (!strcmp(a, "--zips") && v) {
            opt.zips = argv[++i];
        } else if (!strcmp(a, "--sd") && v) {
            opt.sd = argv[++i];
        } else if (!strcmp(a, "--core") && v) {
            split(&cores, &ncores, argv[++i]);
        } else if (!strcmp(a, "--db") && v) {
            split(&dbs, &ndbs, argv[++i]);
        } else if (!strcmp(a, "--config") && v) {
            config = argv[++i];
        } else if (!strcmp(a, "--no-download")) {
            opt.no_download = 1;
        } else if (!strcmp(a, "--dry-run")) {
            opt.dry_run = 1;
        } else {
            fprintf(stderr, "ua_host: unknown argument %s\n", a);
            return 2;
        }
    }
    opt.core_ids = cores;
    opt.extra_dbs = dbs;

    ua_config cfg;
    char err[1024];
    if (ua_config_load(config, &cfg, err, sizeof err)) {
        fprintf(stderr, "ERROR: %s\n", err);
        free(cores);
        free(dbs);
        return 1;
    }
    int rc;
    if (!opt.sd) {
        fprintf(stderr, "error: --sd is required (or use --list)\n");
        rc = 2;
    } else {
        ua_hooks hooks = { 0 };
        hooks.log = log_line;
        rc = ua_run(&cfg, &opt, &hooks);
    }
    ua_config_free(&cfg);
    free(cores);
    free(dbs);
    return rc;
}
