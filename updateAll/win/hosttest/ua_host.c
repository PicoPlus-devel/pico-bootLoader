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

/* UA_EVENTS names a file for what the list of files of updateAll.exe would be
   told, and UA_CANCEL_AFTER stops the run after that many calls of on_bytes;
   compare.py uses them. Without them the hooks stay empty, which the core has
   to cope with as well. */
static FILE *events;
static long bytes_calls, cancel_after;

static void event_set(void *ctx, int n, int nsets)
{
    (void)ctx;
    fprintf(events, "set\t%d\t%d\n", n, nsets);
}

static void event_file(void *ctx, const char *set, const char *file, int state, const char *detail)
{
    (void)ctx;
    fprintf(events, "file\t%d\t%s\t%s\t%s\n", state, set, file, detail);
}

static void count_bytes(void *ctx, long long done, long long total)
{
    (void)ctx, (void)done, (void)total;
    bytes_calls++;
}

static int cancel_now(void *ctx)
{
    (void)ctx;
    return bytes_calls >= cancel_after;
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
    const char **cores = NULL, **dbs = NULL, *program_to = NULL;
    int ncores = 0, ndbs = 0, program = 0;

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
        } else if (!strcmp(a, "--version-older") && v && i + 2 < argc) {
            /* What the Windows program compares itself with: running, newest. */
            puts(ua_version_older(v, argv[i + 2]) ? "older" : "not older");
            return 0;
        } else if (!strcmp(a, "--program")) {
            /* What the Windows program finds about itself, and downloads with --to. */
            program = 1;
        } else if (!strcmp(a, "--to") && v) {
            program_to = argv[++i];
        } else if (!strcmp(a, "--all")) {
            /* Only the scripts ask: the core does this without --core. */
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
    /* UA_SDCARD is what the row of updateAll.exe passes, which --core cannot. */
    if (getenv("UA_SDCARD"))
        opt.sdcard = atoi(getenv("UA_SDCARD"));

    ua_config cfg;
    char err[1024];
    if (ua_config_load(config, &cfg, err, sizeof err)) {
        fprintf(stderr, "ERROR: %s\n", err);
        free(cores);
        free(dbs);
        return 1;
    }
    ua_config_update(&cfg);
    int rc;
    if (program) {
        ua_hooks hooks = { 0 };
        hooks.log = log_line;
        const char *trace = getenv("UA_EVENTS"), *after = getenv("UA_CANCEL_AFTER");
        if (trace && (events = fopen(trace, "w")))
            hooks.on_file = event_file;
        if (after) {
            cancel_after = atol(after);
            hooks.on_bytes = count_bytes;
            hooks.cancelled = cancel_now;
        }
        ua_release rel, prog = { 0 };
        rc = cfg.sd_asset && ua_release_fetch(&cfg, &rel, &prog, err, sizeof err);
        printf("archive: %s\n", !cfg.sd_asset ? "not configured" : rc ? err : rel.tag);
        rc = 0;
        if (prog.tag) {
            printf("program: %s %s %lld\n", prog.tag, prog.url, prog.size);
            char *why = NULL;
            if (program_to && ua_program_download(&prog, "updateAll.exe", program_to, &hooks, &why))
                printf("failed: %s\n", why), rc = 1;
            else if (program_to)
                puts("downloaded");
            free(why);
        } else {
            puts("program: none");
        }
        if (cfg.sd_asset)
            ua_release_free(&rel);
        ua_release_free(&prog);
        if (events)
            fclose(events);
    } else if (!opt.sd) {
        fprintf(stderr, "error: --sd is required (or use --list)\n");
        rc = 2;
    } else {
        ua_hooks hooks = { 0 };
        hooks.log = log_line;
        const char *trace = getenv("UA_EVENTS"), *after = getenv("UA_CANCEL_AFTER");
        if (trace && (events = fopen(trace, "w"))) {
            hooks.on_set = event_set;
            hooks.on_file = event_file;
        }
        if (after) {
            cancel_after = atol(after);
            hooks.on_bytes = count_bytes;
            hooks.cancelled = cancel_now;
        }
        rc = ua_run(&cfg, &opt, &hooks);
        if (events)
            fclose(events);
    }
    ua_config_free(&cfg);
    free(cores);
    free(dbs);
    return rc;
}
