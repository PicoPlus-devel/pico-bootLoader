/*
 * ua_plat.h on Windows. The core works in UTF-8; every call converts to
 * UTF-16 and uses the wide API, so that any path works. Downloads use
 * WinHTTP, which brings the system's TLS and proxy settings.
 */
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>

#include "ua_plat.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

#define USER_AGENT L"updateAll (pico-bootLoader)"
#define TIMEOUT_MS 60000

static char last_error[2048];

wchar_t *ua_widen(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    wchar_t *w = malloc((size_t)(n > 0 ? n : 1) * sizeof *w);
    if (!w)
        abort();
    if (n <= 0 || !MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n))
        w[0] = 0;
    return w;
}

char *ua_narrow(const wchar_t *w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = malloc((size_t)(n > 0 ? n : 1));
    if (!s)
        abort();
    if (n <= 0 || !WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL))
        s[0] = 0;
    return s;
}

/* A path for the wide API, with forward slashes turned into backslashes. */
static wchar_t *wpath(const char *path)
{
    wchar_t *w = ua_widen(path);
    for (wchar_t *p = w; *p; p++)
        if (*p == L'/')
            *p = L'\\';
    return w;
}

/* The system's text for an error code, without the trailing period. */
static void error_text(DWORD code, HMODULE module, char *out, size_t size)
{
    wchar_t *msg = NULL;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS |
                  (module ? FORMAT_MESSAGE_FROM_HMODULE : FORMAT_MESSAGE_FROM_SYSTEM);
    if (FormatMessageW(flags, module, code, 0, (LPWSTR)&msg, 0, NULL) && msg) {
        char *s = ua_narrow(msg);
        size_t n = strlen(s);
        while (n && (s[n - 1] == '\r' || s[n - 1] == '\n' || s[n - 1] == ' ' || s[n - 1] == '.'))
            s[--n] = 0;
        snprintf(out, size, "%s", s);
        free(s);
        LocalFree(msg);
    } else {
        snprintf(out, size, "error %lu", (unsigned long)code);
    }
}

/* As Python shows an OSError on Windows: "[WinError 5] Access is denied: 'E:\x'". */
static void win_error(DWORD code, const char *path, const char *path2)
{
    char text[512];
    error_text(code, NULL, text, sizeof text);
    if (path2)
        snprintf(last_error, sizeof last_error, "[WinError %lu] %s: '%s' -> '%s'",
                 (unsigned long)code, text, path, path2);
    else
        snprintf(last_error, sizeof last_error, "[WinError %lu] %s: '%s'", (unsigned long)code,
                 text, path);
}

const char *ua_last_error(void)
{
    return last_error;
}

FILE *ua_fopen(const char *path, const char *mode)
{
    wchar_t *w = wpath(path), *m = ua_widen(mode);
    FILE *f = _wfopen(w, m);
    free(w);
    free(m);
    return f;
}

int ua_is_dir(const char *path)
{
    wchar_t *w = wpath(path);
    DWORD a = GetFileAttributesW(w);
    free(w);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int ua_file_size(const char *path, long long *size)
{
    WIN32_FILE_ATTRIBUTE_DATA d;
    wchar_t *w = wpath(path);
    BOOL ok = GetFileAttributesExW(w, GetFileExInfoStandard, &d);
    free(w);
    if (!ok || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return -1;
    *size = (long long)d.nFileSizeHigh << 32 | d.nFileSizeLow;
    return 0;
}

static wchar_t *wjoin(const wchar_t *dir, const wchar_t *name)
{
    size_t n = wcslen(dir);
    wchar_t *w = malloc((n + wcslen(name) + 2) * sizeof *w);
    if (!w)
        abort();
    swprintf(w, n + wcslen(name) + 2, L"%ls%ls%ls", dir,
             n && dir[n - 1] != L'\\' && dir[n - 1] != L':' ? L"\\" : L"", name);
    return w;
}

char **ua_list_files(const char *dir)
{
    wchar_t *w = wpath(dir), *pattern = wjoin(w, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    free(pattern);
    free(w);
    if (h == INVALID_HANDLE_VALUE)
        return NULL;
    size_t n = 0, cap = 16;
    char **list = malloc(cap * sizeof *list);
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (n + 2 > cap)
            list = realloc(list, (cap *= 2) * sizeof *list);
        list[n++] = ua_narrow(fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
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
    wchar_t *w = wpath(path);
    size_t n = wcslen(w), i = 0;
    /* Skip what cannot be created: "E:\" or "\\server\share\". */
    if (n >= 2 && w[1] == L':') {
        i = 2;
    } else if (n >= 2 && w[0] == L'\\' && w[1] == L'\\') {
        int seps = 0;
        for (i = 2; i < n && seps < 2; i++)
            seps += w[i] == L'\\';
    }
    for (; i <= n; i++) {
        if (i < n && w[i] != L'\\')
            continue;
        if (!i || w[i - 1] == L'\\' || w[i - 1] == L':')
            continue;
        wchar_t c = w[i];
        w[i] = 0;
        if (!CreateDirectoryW(w, NULL)) {
            DWORD err = GetLastError();
            DWORD a = GetFileAttributesW(w);
            if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) {
                char *part = ua_narrow(w);
                win_error(err == ERROR_ALREADY_EXISTS ? ERROR_ALREADY_EXISTS : err, part, NULL);
                free(part);
                free(w);
                return -1;
            }
        }
        w[i] = c;
    }
    free(w);
    return 0;
}

int ua_replace(const char *from, const char *to)
{
    wchar_t *f = wpath(from), *t = wpath(to);
    BOOL ok = MoveFileExW(f, t, MOVEFILE_REPLACE_EXISTING);
    DWORD err = GetLastError();
    free(f);
    free(t);
    if (!ok) {
        win_error(err, from, to);
        return -1;
    }
    return 0;
}

int ua_remove(const char *path)
{
    wchar_t *w = wpath(path);
    BOOL ok = DeleteFileW(w);
    DWORD err = GetLastError();
    free(w);
    if (!ok) {
        win_error(err, path, NULL);
        return -1;
    }
    return 0;
}

char *ua_make_temp_dir(void)
{
    wchar_t base[MAX_PATH + 1], dir[MAX_PATH + 64];
    DWORD n = GetTempPathW(MAX_PATH + 1, base);
    if (!n || n > MAX_PATH) {
        win_error(GetLastError(), "%TEMP%", NULL);
        return NULL;
    }
    for (unsigned i = 0; i < 100; i++) {
        swprintf(dir, MAX_PATH + 64, L"%lsupdateAll-%lx-%lx", base,
                 (unsigned long)GetCurrentProcessId(), (unsigned long)(GetTickCount() + i));
        if (CreateDirectoryW(dir, NULL))
            return ua_narrow(dir);
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            break;
    }
    char *d = ua_narrow(dir);
    win_error(GetLastError(), d, NULL);
    free(d);
    return NULL;
}

static void remove_tree(const wchar_t *dir)
{
    wchar_t *pattern = wjoin(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    free(pattern);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
                continue;
            wchar_t *p = wjoin(dir, fd.cFileName);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                remove_tree(p);
            else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                RemoveDirectoryW(p);
            else
                DeleteFileW(p);
            free(p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

void ua_remove_tree(const char *path)
{
    wchar_t *w = wpath(path);
    remove_tree(w);
    free(w);
}

char *ua_abspath(const char *path)
{
    wchar_t *w = wpath(path);
    DWORD n = GetFullPathNameW(w, 0, NULL, NULL);
    wchar_t *full = malloc((n ? n : 1) * sizeof *full);
    if (!full)
        abort();
    if (!n || !GetFullPathNameW(w, n, full, NULL)) {
        free(full);
        free(w);
        return strdup(path);
    }
    free(w);
    char *s = ua_narrow(full);
    free(full);
    return s;
}

void ua_sleep_ms(int ms)
{
    Sleep((DWORD)ms);
}

static void http_error(char *err, size_t errlen)
{
    DWORD code = GetLastError();
    if (code >= 12000 && code <= 12200)   /* WINHTTP_ERROR_BASE .. WINHTTP_ERROR_LAST */
        error_text(code, GetModuleHandleW(L"winhttp.dll"), err, errlen);
    else
        error_text(code, NULL, err, errlen);
}

int ua_http_get(const char *url, ua_sink sink, void *ctx, int *status, char *err, size_t errlen)
{
    *status = 0;
    int result = -1;
    HINTERNET session = NULL, conn = NULL, req = NULL;
    wchar_t *wurl = ua_widen(url), *target = NULL;
    unsigned char *buf = malloc(65536);

    URL_COMPONENTS uc;
    memset(&uc, 0, sizeof uc);
    uc.dwStructSize = sizeof uc;
    uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = uc.dwSchemeLength = (DWORD)-1;
    if (!buf || !WinHttpCrackUrl(wurl, 0, 0, &uc) || !uc.dwHostNameLength) {
        snprintf(err, errlen, "unknown url type: '%s'", url);
        goto done;
    }
    wchar_t host[256];
    swprintf(host, 256, L"%.*ls", (int)uc.dwHostNameLength, uc.lpszHostName);
    target = malloc((uc.dwUrlPathLength + uc.dwExtraInfoLength + 2) * sizeof *target);
    swprintf(target, uc.dwUrlPathLength + uc.dwExtraInfoLength + 2, L"%.*ls%.*ls",
             (int)uc.dwUrlPathLength, uc.lpszUrlPath ? uc.lpszUrlPath : L"",
             (int)uc.dwExtraInfoLength, uc.lpszExtraInfo ? uc.lpszExtraInfo : L"");
    if (!*target)
        wcscpy(target, L"/");

    /* The automatic proxy needs Windows 8.1; before that, the configured one. */
    session = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                          WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session)
        session = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        http_error(err, errlen);
        goto done;
    }
    /* Windows 7 and 8 do not offer TLS 1.2 unless asked to. */
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    if (!WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1;
        WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
    WinHttpSetTimeouts(session, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS, TIMEOUT_MS);

    conn = WinHttpConnect(session, host, uc.nPort, 0);
    req = conn ? WinHttpOpenRequest(conn, L"GET", target, NULL, WINHTTP_NO_REFERER,
                                    WINHTTP_DEFAULT_ACCEPT_TYPES,
                                    uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
               : NULL;
    if (!req || !WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                                    0, 0, 0) ||
        !WinHttpReceiveResponse(req, NULL)) {
        http_error(err, errlen);
        goto done;
    }

    DWORD code = 0, size = sizeof code;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
    if (code < 200 || code > 299) {
        wchar_t reason[128] = L"";
        size = sizeof reason;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_TEXT, WINHTTP_HEADER_NAME_BY_INDEX, reason,
                            &size, WINHTTP_NO_HEADER_INDEX);
        char *r = ua_narrow(reason);
        snprintf(err, errlen, "HTTP %lu %s", (unsigned long)code, r);
        free(r);
        *status = (int)code;
        goto done;
    }

    long long total = -1;
    wchar_t length[32];
    size = sizeof length;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, length,
                            &size, WINHTTP_NO_HEADER_INDEX))
        total = _wtoi64(length);

    for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(req, buf, 65536, &got)) {
            http_error(err, errlen);
            goto done;
        }
        if (!got)
            break;
        if (sink(ctx, buf, got, total)) {
            snprintf(err, errlen, "cancelled");
            goto done;
        }
    }
    result = 0;

done:
    if (req)
        WinHttpCloseHandle(req);
    if (conn)
        WinHttpCloseHandle(conn);
    if (session)
        WinHttpCloseHandle(session);
    free(target);
    free(wurl);
    free(buf);
    return result;
}
