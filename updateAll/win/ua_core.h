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
    char *name;         /* game data may give a subfolder: "baseq2/pak0.pak" */
    long long size;
    uint32_t crc;
    int has_offset;     /* BIOS file: the size bytes at offset of the download */
    uint32_t offset;
    char **parts;       /* BIOS file: these entries of the zip, joined; */
    int nparts;         /* NULL-terminated, or NULL */
    int backup;         /* SD-card archive: save the card's copy before replacing it */
} ua_file;

/* The newest release that holds the SD-card archive, or, from ua_release_fetch(),
   the highest version of the program: its tag is then that version. */
typedef struct {
    char *tag;          /* "v0.10" */
    char *page;         /* the release's web page */
    char *url;          /* where its archive is downloaded from */
    long long size;     /* of the archive */
    char *card;         /* the release the card holds, or NULL when not known */
    int newer;          /* the archive is newer than what the card holds */
    int firmware;       /* and comes with other bootloader firmware */
} ua_release;

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
    char *url;          /* game data ("data" in the configuration): where its
                           one zip or file comes from; NULL for a ROM set */
    char **archives;    /* NULL-terminated: zips inside it to search too */
    int narchives;
    int uf2;            /* game data: install the download as a UF2 image */
    uint32_t uf2_address, uf2_family;
    int bios;           /* from "bios": selected together as the set "bios" */
    int sliced;         /* a file has an offset: the download is one file */
    const ua_release *release;  /* the SD-card archive: its files come from the zip */
} ua_core;

typedef struct {
    ua_core *cores;
    int ncores;
    char **databases;   /* NULL-terminated; relative paths already resolved */
    int ndatabases;
    char *config_url;   /* where the latest configuration is read from, or NULL */
    char *config_dir;   /* what relative database paths are relative to */
    char *source;       /* the file or URL this configuration came from */
    char *fetch_error;  /* why config_url was not used, or NULL */
    char *sd_name;      /* "sdcard": the name of the SD-card archive's set, */
    char *sd_url;       /* where its releases are listed, and */
    char *sd_asset;     /* the name of the zip; all NULL when not configured */
    char *sd_program;   /* the program's name among the assets, with * for its
                           version as in "updateAll-*.exe", or NULL */
} ua_config;

/* Parses a configuration. path names it in messages; relative database paths
   are taken relative to config_dir. Returns 0, or -1 with err set. */
int ua_config_parse(const char *text, size_t len, const char *path,
                    const char *config_dir, ua_config *cfg, char *err, size_t errlen);
int ua_config_load(const char *path, ua_config *cfg, char *err, size_t errlen);
/* Reads the configuration at url, as fetch_config of updateAll.py does; its
   own config_url is not followed. Returns 0, or -1 with err set. */
int ua_config_fetch(const char *url, const char *config_dir, ua_config *cfg, char *err,
                    size_t errlen);
/* Replaces cfg with the configuration at its config_url, or, when that cannot
   be read, keeps it and sets fetch_error. */
void ua_config_update(ua_config *cfg);
void ua_config_free(ua_config *cfg);

/* What is happening to a file of the run. DOWNLOADING and BUSY are under way,
   and on_bytes reports on that file; the others are final. NOTE is final
   without anything having been done: a dry run, or a set that was skipped. */
enum { UA_FILE_DOWNLOADING, UA_FILE_BUSY, UA_FILE_DONE, UA_FILE_NOTE, UA_FILE_FAILED,
       UA_FILE_CANCELLED };

typedef struct {
    /* One line of output, without the newline. to_stderr marks the lines the
       Python script writes to stderr. */
    void (*log)(void *ctx, int to_stderr, const char *line);
    /* Progress of the run: set n of nsets is being processed. */
    void (*on_set)(void *ctx, int n, int nsets);
    /* Progress of a download, of converting game data into a UF2 image, or of
       a set's files on the card, counted in files instead of bytes; total is
       -1 when unknown. */
    void (*on_bytes)(void *ctx, long long done, long long total);
    /* A file of the run changes state, for a list of files; detail is the text
       to show with it. The set's name does not identify it: names can repeat. */
    void (*on_file)(void *ctx, const char *set, const char *file, int state, const char *detail);
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
    int sdcard;        /* the SD-card archive: 1 installs it, -1 leaves it out, and
                          0 follows core_ids and whether the card has an emu folder */
    const ua_release *release;  /* the release if already read, or NULL to read it */
} ua_options;

/* What main() and run() of updateAll.py do once the configuration is read.
   Returns the exit status: 0 when every set that was found is complete. */
int ua_run(const ua_config *cfg, const ua_options *opt, const ua_hooks *hooks);

/* fetch_release() of updateAll.py, without comparing it with a card: 0, or -1
   with err set. The Windows program asks about itself in the same request: with
   program set, that gets the highest version of the program that a release
   holds, whether or not one holds the archive; it stays empty when there is none. */
int ua_release_fetch(const ua_config *cfg, ua_release *rel, ua_release *program, char *err,
                     size_t errlen);
/* The release the card at sd_root holds according to its marker, malloc'd, or
   NULL when not known. */
char *ua_card_release(const char *sd_root);
/* Compares the release with what ua_card_release() found; reads nothing. */
void ua_release_compare(ua_release *rel, const char *card);
void ua_release_free(ua_release *rel);
/* Whether the card has an emu folder, as a pico-bootLoader card does. */
int ua_card_has_emu(const char *sd_root);

/* Whether a program of version running, as build.sh gives it, is older than
   version. A test build with anything but vN.M or vN.M.P as its version is not
   older than anything. */
int ua_version_older(const char *running, const char *version);
/* Downloads the program that ua_release_fetch() found to dest, shown as name in
   on_file, and checks that it is that version: 0, or -1 with *why set, which
   the caller frees, and no file left at dest. A cancelled download is "cancelled". */
int ua_program_download(const ua_release *rel, const char *name, const char *dest,
                        const ua_hooks *h, char **why);

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
