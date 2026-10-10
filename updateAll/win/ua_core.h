/*
 * updateAll core: a port of updateAll.py that the Windows program and the
 * host test build share. It prints the same lines as the Python script,
 * through ua_hooks.log, and returns the same exit status.
 */
#ifndef UA_CORE_H
#define UA_CORE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *name;
    long long size;
    uint32_t crc;
} ua_file;

typedef struct {
    char *id;
    char *name;
    char *set;
    char **zips;        /* NULL-terminated */
    int nzips;
    char **dest_parts;  /* NULL-terminated */
    char *dest_shown;   /* "/roms/arcade/PHOENIX" */
    ua_file *files;
    int nfiles;
} ua_core;

typedef struct {
    ua_core *cores;
    int ncores;
    char **databases;   /* NULL-terminated; relative paths already resolved */
    int ndatabases;
} ua_config;

/* Parses a configuration. path names it in messages; relative database paths
   are taken relative to config_dir. Returns 0, or -1 with err set. */
int ua_config_parse(const char *text, size_t len, const char *path,
                    const char *config_dir, ua_config *cfg, char *err, size_t errlen);
int ua_config_load(const char *path, ua_config *cfg, char *err, size_t errlen);
void ua_config_free(ua_config *cfg);

typedef struct {
    /* One line of output, without the newline. to_stderr marks the lines the
       Python script writes to stderr. */
    void (*log)(void *ctx, int to_stderr, const char *line);
    /* Progress of the run: set n of nsets is being processed. */
    void (*on_set)(void *ctx, int n, int nsets);
    /* Progress of a download; total is -1 when unknown. */
    void (*on_bytes)(void *ctx, long long done, long long total);
    /* Non-zero to stop the run. */
    int (*cancelled)(void *ctx);
    void *ctx;
} ua_hooks;

typedef struct {
    const char *sd;            /* root of the SD card */
    const char *zips;          /* zip folder, or NULL for a temporary one */
    const char **core_ids;     /* NULL-terminated, or NULL for every set */
    const char **extra_dbs;    /* NULL-terminated, or NULL */
    int no_download;
    int dry_run;
} ua_options;

/* What main() and run() of updateAll.py do once the configuration is read.
   Returns the exit status: 0 when every set that was found is complete. */
int ua_run(const ua_config *cfg, const ua_options *opt, const ua_hooks *hooks);

/* Number of a set's files that are on the card with the right contents. */
int ua_count_current(const ua_core *core, const char *sd_root);

int ua_is_url(const char *s);

/* MD5 of a buffer, as 32 lower-case hex digits. */
typedef struct {
    uint32_t a, b, c, d;
    uint64_t len;
    unsigned char buf[64];
} ua_md5;
void ua_md5_init(ua_md5 *m);
void ua_md5_update(ua_md5 *m, const void *data, size_t len);
void ua_md5_hex(ua_md5 *m, char out[33]);

#endif
