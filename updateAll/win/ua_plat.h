/*
 * What the updateAll core needs from the operating system. All paths and
 * texts are UTF-8. ua_win32.c implements this for updateAll.exe, and
 * hosttest/ua_posix.c for the host test build.
 */
#ifndef UA_PLAT_H
#define UA_PLAT_H

#include <stddef.h>
#include <stdio.h>

#ifdef _WIN32
#define UA_SEP '\\'
#else
#define UA_SEP '/'
#endif

FILE *ua_fopen(const char *path, const char *mode);
int ua_is_dir(const char *path);
/* 0 and the size of a regular file, -1 otherwise. */
int ua_file_size(const char *path, long long *size);
/* The regular files in dir, as a NULL-terminated array that ua_free_list
   releases; NULL when dir cannot be read. */
char **ua_list_files(const char *dir);
void ua_free_list(char **list);
/* These return 0 on success; on failure ua_last_error() describes why. */
int ua_mkdirs(const char *path);
int ua_replace(const char *from, const char *to);   /* rename over an existing file */
int ua_remove(const char *path);
const char *ua_last_error(void);

char *ua_make_temp_dir(void);                  /* malloc'd; NULL on failure */
void ua_remove_tree(const char *path);
char *ua_abspath(const char *path);            /* malloc'd */
void ua_sleep_ms(int ms);

/* Receives the body of an HTTP response in chunks; total is the announced
   length, or -1. A non-zero return aborts the transfer. */
typedef int (*ua_sink)(void *ctx, const void *data, size_t len, long long total);

/* GET url, following redirects, and pass the body to sink. Returns 0 on a
   2xx response that was received in full. Otherwise err describes the
   failure, as "HTTP <code> <reason>" for an HTTP error, whose code is then
   stored in *status (0 for any other failure). */
int ua_http_get(const char *url, ua_sink sink, void *ctx, int *status,
                char *err, size_t errlen);

#ifdef _WIN32
#include <wchar.h>
/* UTF-8 <-> UTF-16, malloc'd; for the window, which works in UTF-16. */
wchar_t *ua_widen(const char *s);
char *ua_narrow(const wchar_t *s);
#endif

#endif
