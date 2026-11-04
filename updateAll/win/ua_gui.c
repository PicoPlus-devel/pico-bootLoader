/*
 * updateAll.exe: the window around the updateAll core. The core runs on a
 * worker thread and reports through posted messages; the window only
 * collects the choices, lists the files of the run with their progress, and
 * keeps the log the scripts would print. Updating this program itself is only
 * done here, as the scripts are replaced with the SD-card archive.
 */
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <dbt.h>
#include <process.h>
#include <shobjidl.h>
#include <uxtheme.h>
#include <vssym32.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "resource.h"
#include "ua_core.h"
#include "ua_plat.h"

#ifndef UA_VERSION
#define UA_VERSION "dev"
#endif
#define WIDEN_(s) L##s
#define WIDEN(s) WIDEN_(s)
#define UA_VERSION_W WIDEN(UA_VERSION)

#define WM_APP_LOG      (WM_APP + 1)   /* lParam: malloc'd text */
#define WM_APP_SET      (WM_APP + 2)   /* wParam: set, lParam: number of sets */
#define WM_APP_STATUS   (WM_APP + 3)   /* wParam: row, lParam: files current, -1 for no card */
#define WM_APP_DONE     (WM_APP + 4)   /* wParam: job kind, lParam: exit status */
#define WM_APP_CONFIG   (WM_APP + 5)   /* lParam: malloc'd fetched */
#define WM_APP_BYTES    (WM_APP + 6)   /* wParam: done, lParam: total, -1 when unknown */
#define WM_APP_FILE     (WM_APP + 7)   /* lParam: malloc'd file_msg */
#define WM_APP_RELEASE  (WM_APP + 8)   /* lParam: malloc'd release_msg */
#define WM_APP_CARD     (WM_APP + 9)   /* lParam: malloc'd card_msg */
#define WM_APP_OFFER    (WM_APP + 10)  /* a good moment to ask about a newer program */
#define WM_APP_SELF_FAILED (WM_APP + 11)   /* app.self_error says why */
#define WM_APP_SELF_START (WM_APP + 12)    /* the download is done */
#define TIMER_CHECK     1

enum { JOB_NONE, JOB_CHECK, JOB_INSTALL, JOB_CONFIG, JOB_SELF };

typedef struct {
    int kind;
    char *sd, *zips;
    char **ids;              /* NULL-terminated, or NULL for every set */
    int no_download, dry_run;
    int sdcard;              /* ua_options.sdcard */
    const ua_release *release;   /* the release if already read; stays valid */
    const ua_release *program;   /* JOB_SELF: the release to update to; stays valid */
    DWORD last_bytes;        /* when on_bytes last reached the window */
    char *config_url, *config_dir;
} job;

/* What a JOB_CONFIG read from the configuration's config_url. */
typedef struct {
    int ok;
    ua_config cfg;
    char err[1024];
} fetched;

/* What the release check found. */
typedef struct {
    int ok;
    ua_release rel;
    ua_release prog;         /* the newest release of this program; may be empty */
    char err[1024];
} release_msg;

/* What a JOB_CHECK found out about the card, apart from its sets. */
typedef struct {
    int has_emu;
    char *tag;               /* the release it holds, or NULL when not known */
} card_msg;

/* What the core says about a file of the run. */
typedef struct {
    int state;
    wchar_t *set, *file, *detail;
} file_msg;

/* A row of the list of files. */
typedef struct {
    wchar_t *file;
    int state;
    int permille;            /* how far its bar is */
    int counted;             /* the bar shows a known part of a total */
} file_row;

enum { COL_FILE, COL_SET, COL_PROGRESS, COL_STATUS };

/* How each control follows the window when it is resized. */
enum { MOVE_X = 1, MOVE_Y = 2, GROW_X = 4, GROW_Y = 8 };
static const struct {
    int id, how;
} anchors[] = {
    { IDC_SD, GROW_X },          { IDC_SD_BROWSE, MOVE_X },  { IDC_ZIPS, GROW_X },
    { IDC_ZIPS_BROWSE, MOVE_X }, { IDC_SETS, GROW_X },       { IDC_LOG, GROW_X | GROW_Y },
    { IDC_FILES, GROW_X | GROW_Y }, { IDC_SHOWLOG, MOVE_X }, { IDC_STATUS, MOVE_Y | GROW_X },
    { IDC_PROGRESS, MOVE_Y | GROW_X }, { IDOK, MOVE_X | MOVE_Y }, { IDCANCEL, MOVE_X | MOVE_Y },
    { IDC_NOTICE, GROW_X },
};
#define NANCHORS ((int)(sizeof anchors / sizeof anchors[0]))

/* What cannot be changed during a run, or without a configuration. */
static const int inputs[] = { IDC_SD,         IDC_SD_BROWSE, IDC_ZIPS,     IDC_ZIPS_BROWSE,
                              IDC_SELECT_ALL, IDC_SETS,      IDC_DOWNLOAD, IDC_DRYRUN };

static struct {
    HWND dlg;
    int ready;               /* the dialog is set up */
    ua_config cfg;
    int cfg_ok;
    wchar_t source[MAX_PATH + 32];   /* the local or built-in configuration */
    int running;             /* an install was asked for and has not finished */
    int job;                 /* the job the worker thread runs, JOB_NONE if idle */
    job *queued;             /* an install waiting for a card check to stop */
    int check_again;         /* the card changed while a job ran */
    int closing;
    volatile LONG cancel;
    RECT rects[NANCHORS];
    SIZE client0;
    POINT min_track;
    file_row *rows;          /* the list of files */
    int nrows, rows_cap;
    int set_first;           /* the first row of the set being processed */
    int active;              /* the row that on_bytes reports on, or -1 */
    int set_n, nsets;        /* the set being processed; nsets is 0 until there is one */
    int overall;             /* where the bar at the bottom is */
    wchar_t *last_line;      /* the last line the core reported */
    HTHEME bar_theme;        /* NULL without visual styles */
    ua_release *release;     /* the newest release; set once, never replaced */
    int release_failed;      /* it could not be read */
    wchar_t card_path[MAX_PATH * 2];   /* the card that the next three are about */
    int card_known;          /* a check of that card has reported */
    int card_emu;            /* it has an emu folder */
    char *card_tag;          /* the release it holds, or NULL when not known */
    int archive_touched;     /* the user set the archive's tick for this card */
    int setting_tick;        /* the program sets ticks, not the user */
    ua_release *program;     /* the newest release of this program; set once */
    int offered;             /* the user was asked about it, or will not be */
    int in_dialog;           /* the folder dialog or the question before installing is open */
    wchar_t *self_error;     /* why this program could not be updated */
} app;

/* ------------------------------------------------------------------------- */
/* Rows of the list                                                          */
/* ------------------------------------------------------------------------- */

/* The list has a row for each set, except that the BIOS files, which come
   last in the configuration, share one: the row of the set "bios". The SD-card
   archive, when configured, has the first row; it stands for no set of the
   configuration. */
static int archive_rows(void)
{
    return app.cfg.sd_asset != NULL;
}

static int is_archive_row(int row)
{
    return row == 0 && archive_rows();
}

static int bios_first(void)
{
    int i = app.cfg.ncores;
    while (i > 0 && app.cfg.cores[i - 1].bios)
        i--;
    return i;
}

static int row_count(void)
{
    int b = bios_first();
    return archive_rows() + b + (b < app.cfg.ncores);
}

/* The sets a row stands for: cores first .. first + n - 1. */
static void row_sets(int row, int *first, int *n)
{
    int b = bios_first();
    if (is_archive_row(row)) {
        *first = *n = 0;
        return;
    }
    row -= archive_rows();
    *first = row < b ? row : b;
    *n = row < b ? 1 : app.cfg.ncores - b;
}

/* The id the row is selected by, as with --core. */
static const char *row_id(int row)
{
    if (is_archive_row(row))
        return "sdcard";
    row -= archive_rows();
    return row < bios_first() ? app.cfg.cores[row].id : "bios";
}

/* ------------------------------------------------------------------------- */
/* Worker thread                                                             */
/* ------------------------------------------------------------------------- */

static void hook_log(void *ctx, int to_stderr, const char *line)
{
    (void)ctx, (void)to_stderr;
    size_t n = strlen(line);
    char *s = malloc(n + 3);
    memcpy(s, line, n);
    memcpy(s + n, "\r\n", 3);
    wchar_t *w = ua_widen(s);
    free(s);
    if (!PostMessageW(app.dlg, WM_APP_LOG, 0, (LPARAM)w))
        free(w);
}

static void hook_set(void *ctx, int n, int nsets)
{
    (void)ctx;
    PostMessageW(app.dlg, WM_APP_SET, n, nsets);
}

static void hook_bytes(void *ctx, long long done, long long total)
{
    job *j = ctx;
    DWORD now = GetTickCount();
    if (done != total && now - j->last_bytes < 100)
        return;
    j->last_bytes = now;
    PostMessageW(app.dlg, WM_APP_BYTES, (WPARAM)done, (LPARAM)total);
}

static void file_msg_free(file_msg *m)
{
    free(m->set);
    free(m->file);
    free(m->detail);
    free(m);
}

static void hook_file(void *ctx, const char *set, const char *file, int state, const char *detail)
{
    job *j = ctx;
    j->last_bytes = 0;       /* the next on_bytes is about this file */
    file_msg *m = malloc(sizeof *m);
    m->state = state;
    m->set = ua_widen(set);
    m->file = ua_widen(file);
    m->detail = ua_widen(detail);
    if (!PostMessageW(app.dlg, WM_APP_FILE, 0, (LPARAM)m))
        file_msg_free(m);
}

static int hook_cancelled(void *ctx)
{
    (void)ctx;
    return app.cancel != 0;
}

/* This program's path, malloc'd; NULL when it is too long to add a suffix to,
   as the paths here are limited to MAX_PATH. */
static wchar_t *own_path(void)
{
    wchar_t *exe = malloc(MAX_PATH * sizeof *exe);
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n + 9 >= MAX_PATH) {
        free(exe);
        return NULL;
    }
    return exe;
}

static wchar_t *with_suffix(const wchar_t *path, const wchar_t *suffix)
{
    size_t n = wcslen(path) + wcslen(suffix) + 1;
    wchar_t *s = malloc(n * sizeof *s);
    swprintf(s, n, L"%ls%ls", path, suffix);
    return s;
}

/* Starts the copy at exe, and waits until it responds. 0 when it runs, 1 when it
   cannot be started, which can pass as antivirus reads a new file, and -1 when
   it exited at once. */
static int start_copy(const wchar_t *exe)
{
    wchar_t cmd[MAX_PATH + 4];
    swprintf(cmd, MAX_PATH + 4, L"\"%ls\"", exe);
    STARTUPINFOW si = { .cb = sizeof si };
    PROCESS_INFORMATION pi;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
        return 1;
    WaitForInputIdle(pi.hProcess, 10000);
    DWORD gone = WaitForSingleObject(pi.hProcess, 500);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return gone == WAIT_OBJECT_0 ? -1 : 0;
}

static void self_failed(const char *why)
{
    free(app.self_error);
    app.self_error = ua_widen(why);
}

/* JOB_SELF: downloads the new version next to this program, moves it into place,
   and starts it. 0 when the new version runs, 1 when cancelled, and -1 with
   app.self_error set otherwise; the program as it was is then still in place. */
static int self_install(job *j)
{
    ua_hooks hooks = { .log = hook_log, .on_bytes = hook_bytes, .on_file = hook_file,
                       .cancelled = hook_cancelled, .ctx = j };
    wchar_t *exe = own_path();
    if (!exe) {
        self_failed("the path of this program is too long");
        return -1;
    }
    /* Two running copies must not write the same file. */
    HANDLE lock = CreateMutexW(NULL, FALSE, L"updateAll.selfupdate");
    DWORD got = lock ? WaitForSingleObject(lock, 0) : WAIT_FAILED;
    if (got != WAIT_OBJECT_0 && got != WAIT_ABANDONED) {
        self_failed("another copy of updateAll is updating itself");
        if (lock)
            CloseHandle(lock);
        free(exe);
        return -1;
    }
    wchar_t *wnew = with_suffix(exe, L".new"), *wold = with_suffix(exe, L".old");
    char *exe_u = ua_narrow(exe), *new_u = ua_narrow(wnew), *name = ua_narrow(wcsrchr(exe, L'\\') + 1);
    char *why = NULL;
    int rc = -1;
    if (ua_program_download(j->program, name, new_u, &hooks, &why)) {
        if (!strcmp(why, "cancelled"))
            rc = 1;
        else
            self_failed(why);
    } else if (app.cancel) {
        ua_remove(new_u);
        rc = 1;
    } else {
        /* Antivirus can take seconds over a new program, here and in starting it. */
        PostMessageW(app.dlg, WM_APP_SELF_START, 0, 0);
        int failed = 1;
        for (int i = 0; failed && i < 5; i++) {
            if (i)
                Sleep(200);
            failed = ua_replace(new_u, exe_u);
        }
        if (failed) {
            self_failed(ua_last_error());
            /* The new version is all there is if the old one could not be put back. */
            if (GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES)
                ua_remove(new_u);
        } else {
            int started = 1;
            for (int i = 0; started > 0 && i < 5; i++) {
                if (i)
                    Sleep(200);
                started = start_copy(exe);
            }
            if (!started) {
                rc = 0;
            } else {
                /* Back to how it was: this copy's file can be renamed while it runs. */
                MoveFileExW(exe, wnew, MOVEFILE_REPLACE_EXISTING);
                if (MoveFileExW(wold, exe, 0)) {
                    DeleteFileW(wnew);
                    self_failed("the new version did not start, so this version was put back");
                } else {
                    char *old_u = ua_narrow(wold), *text = malloc(strlen(old_u) * 2 + strlen(exe_u) + 96);
                    sprintf(text, "the new version did not start, and this version could not be "
                                  "put back: rename '%s' to '%s'", old_u, exe_u);
                    self_failed(text);
                    free(text);
                    free(old_u);
                }
            }
        }
    }
    free(why);
    free(name);
    free(new_u);
    free(exe_u);
    free(wold);
    free(wnew);
    free(exe);
    ReleaseMutex(lock);
    CloseHandle(lock);
    return rc;
}

static void job_free(job *j)
{
    if (!j)
        return;
    free(j->sd);
    free(j->zips);
    free(j->config_url);
    free(j->config_dir);
    if (j->ids)
        for (char **p = j->ids; *p; p++)
            free(*p);
    free(j->ids);
    free(j);
}

static unsigned __stdcall worker(void *arg)
{
    job *j = arg;
    int rc = 0;
    if (j->kind == JOB_CHECK) {
        int card = ua_is_dir(j->sd);
        card_msg *c = calloc(1, sizeof *c);
        c->has_emu = card && ua_card_has_emu(j->sd);
        c->tag = card ? ua_card_release(j->sd) : NULL;
        if (!PostMessageW(app.dlg, WM_APP_CARD, 0, (LPARAM)c)) {
            free(c->tag);
            free(c);
        }
        for (int r = archive_rows(), rows = row_count(); r < rows && !app.cancel; r++) {
            int first, n, current = 0;
            row_sets(r, &first, &n);
            for (int i = first; i < first + n && card && !app.cancel; i++)
                current += ua_count_current(&app.cfg.cores[i], j->sd);
            PostMessageW(app.dlg, WM_APP_STATUS, r, card ? current : -1);
        }
    } else if (j->kind == JOB_CONFIG) {
        fetched *f = calloc(1, sizeof *f);
        f->ok = !ua_config_fetch(j->config_url, j->config_dir, &f->cfg, f->err, sizeof f->err);
        if (!PostMessageW(app.dlg, WM_APP_CONFIG, 0, (LPARAM)f)) {
            ua_config_free(&f->cfg);
            free(f);
        }
    } else if (j->kind == JOB_SELF) {
        rc = self_install(j);
    } else {
        ua_hooks hooks = { .log = hook_log, .on_set = hook_set, .on_bytes = hook_bytes,
                           .on_file = hook_file, .cancelled = hook_cancelled, .ctx = j };
        ua_options opt = { j->sd, j->zips, (const char **)j->ids, NULL, j->no_download,
                           j->dry_run, j->sdcard, j->release };
        rc = ua_run(&app.cfg, &opt, &hooks);
    }
    PostMessageW(app.dlg, WM_APP_DONE, j->kind, rc);
    job_free(j);
    return 0;
}

/* Asks GitHub for the newest release, once the configuration is final. On its
   own thread: a job would hold up an install for as long as the answer takes. */
static unsigned __stdcall release_worker(void *arg)
{
    (void)arg;
    release_msg *m = calloc(1, sizeof *m);
    m->ok = !ua_release_fetch(&app.cfg, &m->rel, &m->prog, m->err, sizeof m->err);
    if (!PostMessageW(app.dlg, WM_APP_RELEASE, 0, (LPARAM)m)) {
        ua_release_free(&m->rel);
        ua_release_free(&m->prog);
        free(m);
    }
    return 0;
}

static void start_job(job *j)
{
    app.cancel = 0;
    app.job = j->kind;
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, worker, j, 0, NULL);
    if (h) {
        CloseHandle(h);
    } else {
        app.job = JOB_NONE;
        PostMessageW(app.dlg, WM_APP_DONE, j->kind, 1);
        job_free(j);
    }
}

/* ------------------------------------------------------------------------- */
/* Controls                                                                  */
/* ------------------------------------------------------------------------- */

static HWND item(int id)
{
    return GetDlgItem(app.dlg, id);
}

static void log_text(const wchar_t *s)
{
    HWND e = item(IDC_LOG);
    int len = GetWindowTextLengthW(e);
    SendMessageW(e, EM_SETSEL, len, len);
    SendMessageW(e, EM_REPLACESEL, FALSE, (LPARAM)s);
}

/* The status line says this at the end of a run: the core's last line is its
   summary, "Cancelled", or the reason it could not start. */
static void remember_line(const wchar_t *s)
{
    size_t n = wcslen(s);
    while (n && (s[n - 1] == L'\r' || s[n - 1] == L'\n'))
        n--;
    if (!n)
        return;
    free(app.last_line);
    app.last_line = malloc((n + 1) * sizeof *app.last_line);
    wmemcpy(app.last_line, s, n);
    app.last_line[n] = 0;
}

/* The list of files and the log share one place in the window. */
static void show_view(int log)
{
    HWND show = item(log ? IDC_LOG : IDC_FILES), hide = item(log ? IDC_FILES : IDC_LOG);
    ShowWindow(show, SW_SHOW);
    if (GetFocus() == hide)
        SetFocus(show);
    ShowWindow(hide, SW_HIDE);
    if (log)
        SendMessageW(show, EM_SCROLLCARET, 0, 0);
    SetDlgItemTextW(app.dlg, IDC_VIEW_LABEL, log ? L"&Log:" : L"&Files:");
    CheckDlgButton(app.dlg, IDC_SHOWLOG, log ? BST_CHECKED : BST_UNCHECKED);
}

static int is_final(int state)
{
    return state != UA_FILE_DOWNLOADING && state != UA_FILE_BUSY;
}

static void redraw_bar(int row)
{
    HWND lv = item(IDC_FILES);
    RECT r;
    ListView_GetSubItemRect(lv, row, COL_PROGRESS, LVIR_BOUNDS, &r);
    InvalidateRect(lv, &r, FALSE);
}

static void clear_files(void)
{
    ListView_DeleteAllItems(item(IDC_FILES));
    for (int r = 0; r < app.nrows; r++)
        free(app.rows[r].file);
    app.nrows = app.set_first = app.set_n = app.nsets = app.overall = 0;
    app.active = -1;
}

/* The bar at the bottom: each set has an equal share, and the file being worked
   on moves it through that share. It never goes back, though a file's progress
   does when it starts again or when the next stage of the file begins. */
static void raise_overall(int permille)
{
    if (!app.nsets)
        return;
    int pos = (app.set_n * 1000 + permille * 9 / 10) / app.nsets;
    if (pos > app.overall) {
        app.overall = pos;
        SendMessageW(item(IDC_PROGRESS), PBM_SETPOS, pos, 0);
    }
}

static void set_started(int n, int nsets)
{
    if (app.cancel)              /* the core reports the set before it sees the cancel */
        return;
    app.set_first = app.nrows;
    app.set_n = n;
    app.nsets = nsets;
    app.active = -1;
    wchar_t text[64];
    swprintf(text, 64, L"Set %d of %d", n + 1, nsets);
    SetDlgItemTextW(app.dlg, IDC_STATUS, text);
    raise_overall(0);
}

/* A file is one row while its set is processed; a later set that uses the same
   zip gets a row of its own, since the core reports its result for that set. */
static void file_changed(file_msg *m)
{
    HWND lv = item(IDC_FILES);
    int row = app.set_first;
    while (row < app.nrows && _wcsicmp(app.rows[row].file, m->file))
        row++;
    if (row == app.nrows) {
        if (app.nrows == app.rows_cap) {
            app.rows_cap = app.rows_cap ? app.rows_cap * 2 : 64;
            app.rows = realloc(app.rows, (size_t)app.rows_cap * sizeof *app.rows);
        }
        app.rows[row] = (file_row){ .file = _wcsdup(m->file), .state = UA_FILE_BUSY };
        app.nrows++;
        LVITEMW it = { .mask = LVIF_TEXT, .iItem = row, .pszText = m->file };
        ListView_InsertItem(lv, &it);
        ListView_SetItemText(lv, row, COL_SET, m->set);
        ListView_EnsureVisible(lv, row, FALSE);
    }
    file_row *f = &app.rows[row];
    if (is_final(f->state))
        return;
    f->state = m->state;
    f->counted = m->state == UA_FILE_DONE;
    if (m->state == UA_FILE_DONE)
        f->permille = 1000;
    else if (m->state != UA_FILE_FAILED && m->state != UA_FILE_CANCELLED)
        f->permille = 0;
    if (!is_final(m->state))
        app.active = row;
    else if (app.active == row)
        app.active = -1;
    ListView_SetItemText(lv, row, COL_STATUS, m->detail);
    redraw_bar(row);
}

static void size_text(wchar_t *out, size_t n, long long bytes)
{
    if (bytes < 1024 * 1024)
        swprintf(out, n, L"%d KB", (int)((bytes + 1023) / 1024));
    else
        swprintf(out, n, L"%.1f MB", (double)bytes / (1024 * 1024));
}

static void bytes_changed(long long done, long long total)
{
    int permille = total > 0 ? (int)((done < total ? done : total) * 1000 / total) : 0;
    raise_overall(permille);
    if (app.active < 0)
        return;
    file_row *f = &app.rows[app.active];
    if (f->state == UA_FILE_DOWNLOADING) {
        wchar_t got[32], all[32], text[96];
        size_text(got, 32, done);
        if (total > 0) {
            size_text(all, 32, total);
            swprintf(text, 96, L"Downloading, %ls of %ls", got, all);
        } else {
            swprintf(text, 96, L"Downloading, %ls", got);
        }
        ListView_SetItemText(item(IDC_FILES), app.active, COL_STATUS, text);
    }
    if (permille != f->permille || f->counted != (total > 0)) {
        f->permille = permille;
        f->counted = total > 0;
        redraw_bar(app.active);
    }
}

/* The run is over, so nothing is under way any more, even if a message about a
   file could not be posted. */
static void files_done(int cancelled)
{
    for (int r = 0; r < app.nrows; r++) {
        if (is_final(app.rows[r].state))
            continue;
        app.rows[r].state = cancelled ? UA_FILE_CANCELLED : UA_FILE_NOTE;
        app.rows[r].counted = 0;
        ListView_SetItemText(item(IDC_FILES), r, COL_STATUS, cancelled ? L"Cancelled" : L"See the log");
        redraw_bar(r);
    }
    app.active = -1;
}

static void draw_bar(HWND lv, HDC dc, int row)
{
    const file_row *f = &app.rows[row];
    RECT cell, bar, inner, fill;
    /* Not the draw item's rectangle, which is not reliable for a subitem. */
    ListView_GetSubItemRect(lv, row, COL_PROGRESS, LVIR_BOUNDS, &cell);
    FillRect(dc, &cell, GetSysColorBrush(COLOR_WINDOW));
    bar = cell;
    InflateRect(&bar, -3, -2);
    if (app.bar_theme) {
        DrawThemeBackground(app.bar_theme, dc, PP_BAR, 0, &bar, NULL);
        GetThemeBackgroundContentRect(app.bar_theme, dc, PP_BAR, 0, &bar, &inner);
    } else {
        FrameRect(dc, &bar, GetSysColorBrush(COLOR_BTNSHADOW));
        inner = bar;
        InflateRect(&inner, -1, -1);
    }
    int permille = f->state == UA_FILE_FAILED ? 1000 : f->permille;
    fill = inner;
    fill.right = inner.left + (inner.right - inner.left) * permille / 1000;
    if (permille > 0 && app.bar_theme) {
        int look = f->state == UA_FILE_FAILED      ? PBFS_ERROR
                   : f->state == UA_FILE_CANCELLED ? PBFS_PAUSED
                                                   : PBFS_NORMAL;
        DrawThemeBackground(app.bar_theme, dc, PP_FILL, look, &fill, NULL);
    } else if (permille > 0) {
        FillRect(dc, &fill, GetSysColorBrush(COLOR_HIGHLIGHT));
    }
    if (f->counted) {
        /* Light over the filled part of the bar, dark over the rest. */
        wchar_t text[8];
        swprintf(text, 8, L"%d%%", f->permille / 10);
        for (int filled = 0; filled < 2; filled++) {
            int saved = SaveDC(dc);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GetSysColor(filled ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
            IntersectClipRect(dc, filled ? bar.left : fill.right, bar.top,
                              filled ? fill.right : bar.right, bar.bottom);
            DrawTextW(dc, text, -1, &bar, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RestoreDC(dc, saved);
        }
    }
}

static LRESULT draw_files(NMLVCUSTOMDRAW *cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;
    case CDDS_ITEMPREPAINT:
        return CDRF_NOTIFYSUBITEMDRAW;
    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
        if (cd->iSubItem == COL_PROGRESS && (int)cd->nmcd.dwItemSpec < app.nrows) {
            draw_bar(cd->nmcd.hdr.hwndFrom, cd->nmcd.hdc, (int)cd->nmcd.dwItemSpec);
            return CDRF_SKIPDEFAULT;
        }
        break;
    }
    return CDRF_DODEFAULT;
}

/* A path typed or pasted into a field, without surrounding blanks or the
   quotes Explorer's "Copy as path" adds. */
static void field_path(int id, wchar_t *out, int size)
{
    wchar_t text[MAX_PATH * 2];
    GetDlgItemTextW(app.dlg, id, text, MAX_PATH * 2);
    wchar_t *s = text;
    while (*s == L' ' || *s == L'"')
        s++;
    size_t n = wcslen(s);
    while (n && (s[n - 1] == L' ' || s[n - 1] == L'"'))
        s[--n] = 0;
    out[0] = 0;
    if (*s && !GetFullPathNameW(s, (DWORD)size, out, NULL))
        swprintf(out, (size_t)size, L"%ls", s);
}

/* The SD card root: a drive picked from the list, or whatever was typed. */
static void sd_path(wchar_t *out, int size)
{
    HWND combo = item(IDC_SD);
    wchar_t text[MAX_PATH * 2], entry[MAX_PATH * 2];
    GetWindowTextW(combo, text, MAX_PATH * 2);
    int n = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        if (SendMessageW(combo, CB_GETLBTEXTLEN, i, 0) >= MAX_PATH * 2)
            continue;
        SendMessageW(combo, CB_GETLBTEXT, i, (LPARAM)entry);
        if (!wcscmp(entry, text)) {
            swprintf(out, (size_t)size, L"%c:\\", (wchar_t)SendMessageW(combo, CB_GETITEMDATA, i, 0));
            return;
        }
    }
    field_path(IDC_SD, out, size);
}

static int has_emu_folder(wchar_t letter)
{
    wchar_t path[8];
    swprintf(path, 8, L"%c:\\emu", letter);
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

/* Lists the removable drives that hold a medium. With choose set, or when the
   field is empty, it also picks one: the drive this program was started from
   if it is a pico-bootLoader card, else the first removable drive that is
   one, else the first removable drive. */
static void fill_drives(int choose)
{
    HWND combo = item(IDC_SD);
    wchar_t keep[MAX_PATH * 2];
    GetWindowTextW(combo, keep, MAX_PATH * 2);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);

    int first = -1, first_card = -1;
    DWORD mask = GetLogicalDrives();
    for (int d = 2; d < 26; d++) {           /* not A: and B: */
        wchar_t root[4] = { (wchar_t)(L'A' + d), L':', L'\\', 0 }, label[MAX_PATH + 1] = L"";
        if (!(mask & (1u << d)) || GetDriveTypeW(root) != DRIVE_REMOVABLE ||
            !GetVolumeInformationW(root, label, MAX_PATH + 1, NULL, NULL, NULL, NULL, 0))
            continue;
        wchar_t text[MAX_PATH + 16];
        if (*label)
            swprintf(text, MAX_PATH + 16, L"%ls  (%ls)", root, label);
        else
            swprintf(text, MAX_PATH + 16, L"%ls", root);
        int i = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text);
        SendMessageW(combo, CB_SETITEMDATA, i, root[0]);
        if (first < 0)
            first = d;
        if (first_card < 0 && has_emu_folder(root[0]))
            first_card = d;
    }

    if (!choose && *keep) {
        SetWindowTextW(combo, keep);
        return;
    }
    wchar_t exe[MAX_PATH];
    int pick = -1;
    if (GetModuleFileNameW(NULL, exe, MAX_PATH) && exe[1] == L':' && has_emu_folder(exe[0]))
        pick = (exe[0] & ~0x20) - L'A';
    else if (first_card >= 0)
        pick = first_card;
    else
        pick = first;
    if (pick < 0)
        return;
    int n = (int)SendMessageW(combo, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++) {
        if ((int)SendMessageW(combo, CB_GETITEMDATA, i, 0) == L'A' + pick) {
            SendMessageW(combo, CB_SETCURSEL, i, 0);
            return;
        }
    }
    wchar_t root[4] = { (wchar_t)(L'A' + pick), L':', L'\\', 0 };   /* not removable, but the card */
    SetWindowTextW(combo, root);
}

static int checked_sets(void)
{
    int n = 0;
    for (int r = 0; r < row_count(); r++)
        n += ListView_GetCheckState(item(IDC_SETS), r) != 0;
    return n;
}

/* Select all is about the sets: the archive's row has its own default. */
static int in_select_all(int row)
{
    return !is_archive_row(row);
}

static void select_all_counts(int *ticked, int *all)
{
    *ticked = *all = 0;
    for (int r = 0; r < row_count(); r++) {
        if (in_select_all(r)) {
            (*all)++;
            *ticked += ListView_GetCheckState(item(IDC_SETS), r) != 0;
        }
    }
}

static void set_archive_tick(int tick)
{
    if (!archive_rows())
        return;
    app.setting_tick = 1;
    ListView_SetCheckState(item(IDC_SETS), 0, tick);
    app.setting_tick = 0;
}

/* There is something to download from: a database for the ROM sets, or game
   data or the SD-card archive, which have a URL of their own. */
static int can_download(void)
{
    int any_url = 0;
    for (int i = 0; i < app.cfg.ncores; i++)
        any_url |= app.cfg.cores[i].url != NULL;
    return app.cfg.ndatabases > 0 || any_url || archive_rows();
}

static int may_download(void)
{
    return app.cfg_ok && can_download() && IsDlgButtonChecked(app.dlg, IDC_DOWNLOAD);
}

static void update_buttons(void)
{
    int ticked = app.cfg_ok ? checked_sets() : 0;
    int ready = app.cfg_ok && !app.running && app.job != JOB_CONFIG &&
                GetWindowTextLengthW(item(IDC_SD)) > 0 &&
                ticked > 0 &&
                (GetWindowTextLengthW(item(IDC_ZIPS)) > 0 || may_download());
    EnableWindow(item(IDOK), ready);
    int selected = 0, all = 0;
    if (app.cfg_ok)
        select_all_counts(&selected, &all);
    CheckDlgButton(app.dlg, IDC_SELECT_ALL, !selected        ? BST_UNCHECKED
                                            : selected < all ? BST_INDETERMINATE
                                                             : BST_CHECKED);
}

static void set_running(int running)
{
    app.running = running;
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++)
        EnableWindow(item(inputs[i]), !running && (inputs[i] != IDC_DOWNLOAD || can_download()));
    SetDlgItemTextW(app.dlg, IDCANCEL, running ? L"Cancel" : L"Close");
    EnableWindow(item(IDCANCEL), TRUE);
    update_buttons();
    if (!running)
        SetFocus(item(IDCANCEL));
}

/* The notice above the list and the archive's status follow what is known about
   the release and the card; they are worked out afresh when either changes. */
static void update_notice(void)
{
    wchar_t notice[128] = L"", status[64] = L"";
    ua_release *rel = app.release;
    if (rel && app.card_known) {
        ua_release_compare(rel, app.card_tag);
        wchar_t *tag = ua_widen(rel->tag), *card = ua_widen(rel->card ? rel->card : "unknown");
        if (rel->newer) {
            swprintf(status, 64, L"%ls available", tag);
            if (app.card_emu)
                swprintf(notice, 128, L"New SD card archive: %ls (card: %ls).%ls", tag, card,
                         rel->firmware ? L" Flash the board too." : L"");
        } else {
            swprintf(status, 64, L"Up to date (%ls)", card);
        }
        free(tag);
        free(card);
    } else if (app.release_failed) {
        wcscpy(status, L"Not checked");
    }
    SetDlgItemTextW(app.dlg, IDC_NOTICE, notice);
    if (archive_rows())
        ListView_SetItemText(item(IDC_SETS), 0, 3, status);
}

static void release_read(release_msg *m)
{
    if (m->ok) {
        app.release = malloc(sizeof *app.release);
        *app.release = m->rel;
    } else {
        app.release_failed = 1;
        /* A run reports this itself, as it asks again. */
        if (!app.running) {
            wchar_t *why = ua_widen(m->err);
            log_text(L"New archive:  could not check: ");
            log_text(why);
            log_text(L"\r\n");
            free(why);
        }
    }
    /* Independent of the archive, which a release can lack. */
    if (m->prog.tag) {
        app.program = malloc(sizeof *app.program);
        *app.program = m->prog;
        PostMessageW(app.dlg, WM_APP_OFFER, 0, 0);
    }
    free(m);
    update_notice();
}

/* A check of the card reports. */
static void card_checked(card_msg *c)
{
    free(app.card_tag);
    app.card_tag = c->tag;
    app.card_emu = c->has_emu;
    app.card_known = 1;
    free(c);
    update_notice();
    update_buttons();
}

static void check_release(void)
{
    if (!app.cfg_ok || !archive_rows())
        return;
    HANDLE h = (HANDLE)_beginthreadex(NULL, 0, release_worker, NULL, 0, NULL);
    if (h) {
        CloseHandle(h);
    } else {
        app.release_failed = 1;
        update_notice();
    }
}

static void set_status(int row, int current)
{
    wchar_t text[64] = L"";
    int first, n, total = 0;
    if (is_archive_row(row))
        return;
    row_sets(row, &first, &n);
    for (int i = first; i < first + n; i++)
        total += app.cfg.cores[i].nfiles;
    if (current == total)
        wcscpy(text, L"Up to date");
    else if (current == 0)
        wcscpy(text, L"Not installed");
    else if (current > 0)
        swprintf(text, 64, L"%d of %d files missing", total - current, total);
    ListView_SetItemText(item(IDC_SETS), row, 3, text);
}

/* Checks which sets are complete on the card, for the status column. */
static void check_card(void)
{
    if (!app.cfg_ok || app.running)
        return;
    wchar_t sd[MAX_PATH * 2];
    sd_path(sd, MAX_PATH * 2);
    if (wcscmp(sd, app.card_path)) {
        /* Nothing is known about this card yet, and a check of a card that is
           slow to read can take a while: no notice about the last one meanwhile. */
        wcscpy(app.card_path, sd);
        app.card_known = app.card_emu = app.archive_touched = 0;
        free(app.card_tag);
        app.card_tag = NULL;
        set_archive_tick(1);     /* whatever the user chose for the last card */
        update_notice();
        update_buttons();
    }
    if (app.job == JOB_CONFIG) {
        app.check_again = 1;     /* once the sets are known */
        return;
    }
    if (app.job == JOB_CHECK) {
        app.cancel = 1;          /* restart it for the new card */
        app.check_again = 1;
        return;
    }
    if (!*sd) {
        for (int r = 0; r < row_count(); r++)
            set_status(r, -1);
        return;
    }
    job *j = calloc(1, sizeof *j);
    j->kind = JOB_CHECK;
    j->sd = ua_narrow(sd);
    start_job(j);
}

/* What every run starts with; the job waits for a card check to stop. */
static void begin_run(job *j)
{
    SetDlgItemTextW(app.dlg, IDC_STATUS, L"");
    free(app.last_line);
    app.last_line = NULL;
    clear_files();
    show_view(0);
    SendMessageW(item(IDC_PROGRESS), PBM_SETSTATE, PBST_NORMAL, 0);
    SendMessageW(item(IDC_PROGRESS), PBM_SETPOS, 0, 0);
    set_running(1);
    if (app.job == JOB_CHECK) {
        app.cancel = 1;          /* started once the card check has stopped */
        app.queued = j;
    } else {
        start_job(j);
    }
}

/* A warning before anything happens, with No as the default answer, as the user
   may press Enter without reading. A question about a new version of this program
   waits until it is answered. */
static int warn_first(const wchar_t *text)
{
    app.in_dialog = 1;
    int yes = MessageBoxW(app.dlg, text, L"updateAll", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
    app.in_dialog = 0;
    PostMessageW(app.dlg, WM_APP_OFFER, 0, 0);
    return yes;
}

/* Whether the archive is newer than what this card holds. Without a release yet,
   the run looks for it itself. */
static int archive_newer_for(const char *sd_u)
{
    if (!app.release)
        return 1;
    ua_release rel = *app.release;
    char *card = ua_card_release(sd_u);
    rel.card = NULL;
    ua_release_compare(&rel, card);
    int newer = rel.newer;
    free(rel.card);
    free(card);
    return newer;
}

/* Whether a folder holds anything of the user's. What Windows and macOS leave on a
   new card does not count: System Volume Information and .fseventsd, for example. */
static int holds_files(const wchar_t *sd)
{
    size_t n = wcslen(sd);
    wchar_t pattern[MAX_PATH * 2 + 4];
    swprintf(pattern, MAX_PATH * 2 + 4, L"%ls%ls*", sd, n && sd[n - 1] == L'\\' ? L"" : L"\\");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    int found = 0;
    do {
        found = fd.cFileName[0] != L'.' &&
                !(fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM));
    } while (!found && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

/* The archive's row starts ticked, so this asks before it overwrites files on a card
   in use, or adds its folders to a drive that the program chose but is no such card. */
static int confirm_archive(const wchar_t *sd)
{
    if (!archive_rows() || !ListView_GetCheckState(item(IDC_SETS), 0) ||
        IsDlgButtonChecked(app.dlg, IDC_DRYRUN) == BST_CHECKED)
        return 1;
    char *sd_u = ua_narrow(sd);
    int ok = 1;
    if (ua_card_has_emu(sd_u)) {
        if (archive_newer_for(sd_u))
            ok = warn_first(L"Installing the SD card files replaces files on this card.\r\n\r\n"
                            L"Files that the archive also holds are overwritten, including custom artwork "
                            L"under the same name. A list in /emu that you edited is first saved as .bak. "
                            L"Files that are not in the archive, such as your games and saves, are not "
                            L"changed.\r\n\r\n"
                            L"Continue? To leave the card as it is, select No and untick SD card files.");
    } else if (holds_files(sd)) {
        ok = warn_first(L"The selected SD card does not look like a pico-bootLoader card: it holds "
                        L"files, but no emu folder.\r\n\r\n"
                        L"Installing the SD card files adds the folders of the archive to it, such as "
                        L"emu and updateAll, and overwrites files there under the same names. Other "
                        L"files are not changed.\r\n\r\n"
                        L"Continue? If this is the wrong drive, select No and choose another one.");
    }
    free(sd_u);
    return ok;
}

/* Whether a BIOS file or a ROM set is ticked. Game data is shareware or a demo. */
static int copyright_ticked(void)
{
    for (int r = archive_rows(); r < row_count(); r++) {
        int first, n;
        row_sets(r, &first, &n);
        const ua_core *c = &app.cfg.cores[first];
        if (ListView_GetCheckState(item(IDC_SETS), r) && (c->bios || !c->url))
            return 1;
    }
    return 0;
}

/* Once per run rather than per row, as there can be many; no run, no warning. */
static int confirm_rights(void)
{
    return !copyright_ticked() || IsDlgButtonChecked(app.dlg, IDC_DRYRUN) == BST_CHECKED ||
           warn_first(L"BIOS files or arcade ROM sets are selected.\r\n\r\nThe ROM sets and BIOS files are copyright of their manufacturers and are not part of this project. Holding the rights to them is your responsibility.\r\n\r\n"
                      L"Continue? To leave them out, select No and untick them.");
}

static void install(void)
{
    wchar_t sd[MAX_PATH * 2], zips[MAX_PATH * 2];
    sd_path(sd, MAX_PATH * 2);
    field_path(IDC_ZIPS, zips, MAX_PATH * 2);
    if (!confirm_archive(sd) || !confirm_rights())
        return;

    job *j = calloc(1, sizeof *j);
    j->kind = JOB_INSTALL;
    j->sd = ua_narrow(sd);
    j->zips = *zips ? ua_narrow(zips) : NULL;
    j->no_download = !may_download();
    j->dry_run = IsDlgButtonChecked(app.dlg, IDC_DRYRUN) == BST_CHECKED;
    /* Unticked sets work like --core with the ticked ones. The archive's row
       goes apart: a set that is skipped is no failure just because the archive
       was unticked. */
    int archive = archive_rows() && ListView_GetCheckState(item(IDC_SETS), 0);
    if (archive_rows())
        j->sdcard = !archive ? -1 : may_download() ? 1 : 0;
    j->release = app.release;
    if (checked_sets() - archive < row_count() - archive_rows()) {
        j->ids = calloc((size_t)row_count() + 1, sizeof *j->ids);
        for (int r = archive_rows(), n = 0; r < row_count(); r++)
            if (ListView_GetCheckState(item(IDC_SETS), r))
                j->ids[n++] = _strdup(row_id(r));
        /* Only the archive is ticked: no set at all, rather than every set. */
        if (!j->ids[0] && archive)
            j->ids[0] = _strdup("sdcard");
    }

    SetDlgItemTextW(app.dlg, IDC_LOG, L"");
    begin_run(j);
}

/* A program should only ever come over a connection that can be trusted; the
   test fixtures are on this computer. */
static int program_url_allowed(const char *url)
{
    static const char local[] = "http://127.0.0.1";
    size_t n = sizeof local - 1;
    return !_strnicmp(url, "https://", 8) ||
           (!_strnicmp(url, local, n) && (url[n] == ':' || url[n] == '/'));
}

/* Asks once whether to update to a newer version of this program, if there is
   one and it can be done. Posted, and posted again once whatever holds it back is
   over: this is a message box, which must not nest in the steps of a job. */
static void offer_update(void)
{
    if (!app.program || app.offered || app.running || app.in_dialog || app.closing)
        return;
    app.offered = 1;
    /* Without main's configuration, removing the key there is not seen. */
    if (app.cfg.fetch_error || !app.cfg.sd_program ||
        !ua_version_older(UA_VERSION, app.program->tag) || !program_url_allowed(app.program->url))
        return;
    wchar_t *exe = own_path();
    if (!exe)
        return;
    wchar_t *old = with_suffix(exe, L".old"), *probe = with_suffix(exe, L".new.part");
    wchar_t *version = ua_widen(app.program->tag), *page = ua_widen(app.program->page);
    /* The SD-card archive replaced this program during this run, or an update
       still has to be cleared up: the new copy will ask, if there is anything newer. */
    int replaced = GetFileAttributesW(old) != INVALID_FILE_ATTRIBUTES;
    HANDLE h = replaced ? INVALID_HANDLE_VALUE
                        : CreateFileW(probe, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    int writable = h != INVALID_HANDLE_VALUE;
    if (writable)
        CloseHandle(h);
    if (!replaced && !writable) {
        /* Nothing the user could agree to. */
        size_t n = wcslen(version) + wcslen(page) + 96;
        wchar_t *line = malloc(n * sizeof *line);
        swprintf(line, n, L"updateAll %ls is available, but this folder cannot be written to: %ls\r\n",
                 version, page);
        log_text(line);
        free(line);
    } else if (!replaced) {
        wchar_t text[512];
        swprintf(text, 512, L"A new version of updateAll is available: %ls (this is %ls). Update now?"
                            L"\r\n\r\nupdateAll closes and starts the new version.",
                 version, UA_VERSION_W);
        /* The window is not on the screen yet when the answer comes quickly. */
        if (!IsWindowVisible(app.dlg))
            ShowWindow(app.dlg, SW_SHOWNORMAL);
        /* No by default, as the user may be typing when this appears. */
        if (MessageBoxW(app.dlg, text, L"updateAll", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
            job *j = calloc(1, sizeof *j);
            j->kind = JOB_SELF;
            j->program = app.program;
            begin_run(j);
            app.nsets = 1;       /* the bar at the bottom shows the download */
        }
    }
    free(page);
    free(version);
    free(probe);
    free(old);
    free(exe);
}

static void show_self_failure(void)
{
    wchar_t *page = ua_widen(app.program->page);
    const wchar_t *why = app.self_error ? app.self_error : L"unknown reason";
    size_t n = wcslen(why) + wcslen(page) + 128;
    wchar_t *text = malloc(n * sizeof *text);
    swprintf(text, n, L"updateAll was not updated: %ls\r\n", why);
    log_text(text);
    swprintf(text, n, L"updateAll was not updated: %ls.\r\n\r\nThe new version can be downloaded from %ls",
             why, page);
    MessageBoxW(app.dlg, text, L"updateAll", MB_OK | MB_ICONWARNING);
    free(text);
    free(page);
}

static void job_done(int kind, int rc)
{
    app.job = JOB_NONE;
    if (kind == JOB_CONFIG) {
        update_buttons();
        check_release();
    }
    if (kind == JOB_INSTALL) {
        int cancelled = app.cancel != 0;
        files_done(cancelled);
        if (app.nsets && !cancelled)
            SendMessageW(item(IDC_PROGRESS), PBM_SETPOS, 1000, 0);
        SendMessageW(item(IDC_PROGRESS), PBM_SETSTATE,
                     cancelled ? PBST_PAUSED : rc ? PBST_ERROR : PBST_NORMAL, 0);
        SetDlgItemTextW(app.dlg, IDC_STATUS, app.last_line ? app.last_line : L"");
        MessageBeep(rc ? MB_ICONWARNING : MB_OK);
        if (app.closing) {
            EndDialog(app.dlg, 0);
            return;
        }
        set_running(0);
        app.check_again = 1;
        PostMessageW(app.dlg, WM_APP_OFFER, 0, 0);
    }
    if (kind == JOB_SELF) {
        /* The new version runs, so this one goes. */
        if (!rc || app.closing) {
            EndDialog(app.dlg, 0);
            return;
        }
        files_done(rc > 0);
        SendMessageW(item(IDC_PROGRESS), PBM_SETSTATE, rc > 0 ? PBST_PAUSED : PBST_ERROR, 0);
        SetDlgItemTextW(app.dlg, IDC_STATUS, rc > 0 ? L"Cancelled" : L"updateAll was not updated");
        set_running(0);
        app.check_again = 1;
        if (rc < 0)
            PostMessageW(app.dlg, WM_APP_SELF_FAILED, 0, 0);
    }
    if (app.queued) {
        job *j = app.queued;
        app.queued = NULL;
        start_job(j);
    } else if (app.check_again) {
        app.check_again = 0;
        check_card();
    }
}

static void close_or_cancel(void)
{
    if (!app.running) {
        EndDialog(app.dlg, 0);     /* a card check may still run; it only reads */
        return;
    }
    if (app.queued) {              /* the install has not started yet */
        job_free(app.queued);
        app.queued = NULL;
        SetDlgItemTextW(app.dlg, IDC_STATUS, L"Cancelled");
        set_running(0);
        return;
    }
    app.cancel = 1;
    SetDlgItemTextW(app.dlg, IDCANCEL, L"Cancelling...");
    EnableWindow(item(IDCANCEL), FALSE);
}

static void browse(int field, const wchar_t *title)
{
    IFileOpenDialog *d;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IFileOpenDialog, (void **)&d)))
        return;
    DWORD opts;
    IFileOpenDialog_GetOptions(d, &opts);
    IFileOpenDialog_SetOptions(d, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    IFileOpenDialog_SetTitle(d, title);
    wchar_t current[MAX_PATH * 2];
    if (field == IDC_SD)
        sd_path(current, MAX_PATH * 2);
    else
        field_path(field, current, MAX_PATH * 2);
    IShellItem *folder;
    if (*current &&
        SUCCEEDED(SHCreateItemFromParsingName(current, NULL, &IID_IShellItem, (void **)&folder))) {
        IFileOpenDialog_SetFolder(d, folder);
        IShellItem_Release(folder);
    }
    IShellItem *result;
    /* A message box closing now would re-enable the window under this dialog. */
    app.in_dialog = 1;
    HRESULT shown = IFileOpenDialog_Show(d, app.dlg);
    app.in_dialog = 0;
    PostMessageW(app.dlg, WM_APP_OFFER, 0, 0);
    if (SUCCEEDED(shown) && SUCCEEDED(IFileOpenDialog_GetResult(d, &result))) {
        wchar_t *path;
        if (SUCCEEDED(IShellItem_GetDisplayName(result, SIGDN_FILESYSPATH, &path))) {
            SetDlgItemTextW(app.dlg, field, path);
            CoTaskMemFree(path);
            if (field == IDC_SD)
                SetTimer(app.dlg, TIMER_CHECK, 1, NULL);
            update_buttons();
        }
        IShellItem_Release(result);
    }
    IFileOpenDialog_Release(d);
}

/* ------------------------------------------------------------------------- */
/* Set-up and layout                                                         */
/* ------------------------------------------------------------------------- */

/* updateAll.json next to the program wins over the built-in configuration,
   so that sets can be added without a new program. */
static void load_config(void)
{
    wchar_t *source = app.source;
    size_t size = sizeof app.source / sizeof app.source[0];
    wchar_t exe[MAX_PATH], dir[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wcscpy(dir, exe);
    wchar_t *slash = wcsrchr(dir, L'\\');
    if (slash)
        *slash = 0;
    wchar_t path[MAX_PATH + 32];
    swprintf(path, MAX_PATH + 32, L"%ls\\updateAll.json", dir);

    char err[1024] = "";
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        char *p = ua_narrow(path);
        app.cfg_ok = !ua_config_load(p, &app.cfg, err, sizeof err);
        free(p);
        swprintf(source, size, L"%ls", path);
    } else {
        HRSRC res = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_CONFIG), (LPCWSTR)RT_RCDATA);
        HGLOBAL mem = res ? LoadResource(NULL, res) : NULL;
        const char *text = mem ? LockResource(mem) : NULL;
        char *d = ua_narrow(dir);
        app.cfg_ok = text && !ua_config_parse(text, SizeofResource(NULL, res),
                                               "built-in configuration", d, &app.cfg, err,
                                               sizeof err);
        free(d);
        swprintf(source, size, L"built-in");
    }
    if (!app.cfg_ok) {
        wchar_t *w = ua_widen(err);
        log_text(L"ERROR: ");
        log_text(w);
        log_text(L"\r\n\r\nCorrect or remove the configuration file and start the program again.\r\n");
        free(w);
    }
}

static void init_sets(void)
{
    HWND lv = item(IDC_SETS);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT |
                                              LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    static const wchar_t *const titles[] = { L"Set", L"Zip", L"SD card folder", L"Status" };
    for (int c = 0; c < 4; c++) {
        LVCOLUMNW col = { .mask = LVCF_TEXT | LVCF_WIDTH, .cx = 100, .pszText = (LPWSTR)titles[c] };
        ListView_InsertColumn(lv, c, &col);
    }
}

static void init_files(void)
{
    HWND lv = item(IDC_FILES);
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                              LVS_EX_LABELTIP);
    static const wchar_t *const titles[] = { L"File", L"Set", L"Progress", L"Status" };
    for (int c = 0; c < 4; c++) {
        LVCOLUMNW col = { .mask = LVCF_TEXT | LVCF_WIDTH, .cx = 100, .pszText = (LPWSTR)titles[c] };
        ListView_InsertColumn(lv, c, &col);
    }
    app.bar_theme = OpenThemeData(lv, VSCLASS_PROGRESS);
    app.active = -1;
}

/* Shares of the window's width, not of the client area, so that the columns stay
   as they are when a scroll bar appears. */
static void size_file_columns(void)
{
    static const int percent[] = { 26, 18, 16 };
    HWND lv = item(IDC_FILES);
    RECT r;
    GetWindowRect(lv, &r);
    int width = r.right - r.left - 2 * GetSystemMetrics(SM_CXEDGE) - GetSystemMetrics(SM_CXVSCROLL);
    int used = 0;
    for (int c = 0; c < 3; c++) {
        ListView_SetColumnWidth(lv, c, width * percent[c] / 100);
        used += width * percent[c] / 100;
    }
    ListView_SetColumnWidth(lv, COL_STATUS, width - used);
}

/* The texts of a row: the set's name, its zips and its folder; for the BIOS
   row, the number of downloads and the folders they go to. */
static void row_texts(int row, char **name, char **zips, char **dest)
{
    int first, n;
    if (is_archive_row(row)) {
        *name = _strdup(app.cfg.sd_name);
        *zips = _strdup(app.cfg.sd_asset);
        *dest = _strdup("/");
        return;
    }
    row_sets(row, &first, &n);
    const ua_core *c = &app.cfg.cores[first];
    if (!c->bios) {
        size_t len = 1;
        for (int z = 0; z < c->nzips; z++)
            len += strlen(c->zips[z]) + 2;
        *zips = calloc(1, len);
        for (int z = 0; z < c->nzips; z++) {
            if (z)
                strcat(*zips, ", ");
            strcat(*zips, c->zips[z]);
        }
        *name = _strdup(c->name);
        *dest = _strdup(c->dest_shown);
        return;
    }
    int downloads = 0;
    size_t len = 1;
    for (int i = first; i < first + n; i++) {
        downloads += app.cfg.cores[i].nzips;
        len += strlen(app.cfg.cores[i].dest_shown) + 2;
    }
    *zips = malloc(32);
    snprintf(*zips, 32, "%d download%s", downloads, downloads == 1 ? "" : "s");
    *dest = calloc(1, len);
    for (int i = first; i < first + n; i++) {
        int seen = 0;
        for (int k = first; k < i && !seen; k++)
            seen = !_stricmp(app.cfg.cores[k].dest_shown, app.cfg.cores[i].dest_shown);
        if (seen)
            continue;
        if (**dest)
            strcat(*dest, ", ");
        strcat(*dest, app.cfg.cores[i].dest_shown);
    }
    *name = _strdup("BIOS");
}

/* One row per set, and one for all BIOS files. Nothing is installed unless the
   user ticks it, and no choice is stored, so a set starts unticked unless its id
   is in ticked. The archive's row starts ticked. */
static void fill_sets(char **ticked)
{
    HWND lv = item(IDC_SETS);
    app.setting_tick = 1;
    ListView_DeleteAllItems(lv);
    for (int r = 0; r < row_count(); r++) {
        char *name, *zips, *dest;
        row_texts(r, &name, &zips, &dest);
        wchar_t *wname = ua_widen(name), *wzips = ua_widen(zips), *wdest = ua_widen(dest);
        LVITEMW it = { .mask = LVIF_TEXT, .iItem = r, .pszText = wname };
        ListView_InsertItem(lv, &it);
        ListView_SetItemText(lv, r, 1, wzips);
        ListView_SetItemText(lv, r, 2, wdest);
        int tick = 0;
        for (char **t = ticked; t && *t && !tick; t++)
            tick = !_stricmp(*t, row_id(r));
        if (is_archive_row(r) && !app.archive_touched)
            tick = 1;
        ListView_SetCheckState(lv, r, tick);
        free(name);
        free(zips);
        free(dest);
        free(wname);
        free(wzips);
        free(wdest);
    }
    app.setting_tick = 0;
    for (int c = 0; c < 3; c++)
        ListView_SetColumnWidth(lv, c, LVSCW_AUTOSIZE_USEHEADER);
}

static void size_status_column(void)
{
    HWND lv = item(IDC_SETS);
    RECT r;
    GetClientRect(lv, &r);
    int used = 0;
    for (int c = 0; c < 3; c++)
        used += ListView_GetColumnWidth(lv, c);
    ListView_SetColumnWidth(lv, 3, r.right - used > 80 ? r.right - used : 80);
}

/* The download box and the hint in the zip folder field follow whether there
   is anything to download from; had is whether there was before the
   configuration changed. */
static void show_databases(int had)
{
    SendMessageW(item(IDC_ZIPS), EM_SETCUEBANNER, TRUE,
                 (LPARAM)(can_download() ? L"Optional: folder with MAME zips"
                                         : L"Folder with MAME zips"));
    if (can_download()) {
        SetDlgItemTextW(app.dlg, IDC_DOWNLOAD, L"&Download missing files");
        EnableWindow(item(IDC_DOWNLOAD), !app.running);
        if (!had)
            CheckDlgButton(app.dlg, IDC_DOWNLOAD, BST_CHECKED);
    } else {
        SetDlgItemTextW(app.dlg, IDC_DOWNLOAD, L"Download missing files (nothing to download from)");
        CheckDlgButton(app.dlg, IDC_DOWNLOAD, BST_UNCHECKED);
        EnableWindow(item(IDC_DOWNLOAD), FALSE);
    }
}

static void log_config(void)
{
    const ua_config *c = &app.cfg;
    wchar_t *from = c->config_url && !c->fetch_error ? ua_widen(c->source) : _wcsdup(app.source);
    wchar_t *why = c->fetch_error ? ua_widen(c->fetch_error) : NULL;
    size_t n = wcslen(from) + (why ? wcslen(why) : 0) + 160;
    wchar_t *line = malloc(n * sizeof *line);
    int sets = row_count() - archive_rows();
    swprintf(line, n, L"Configuration: %ls, %d set%ls, %d download database%ls%ls%ls%ls\r\n", from,
             sets, sets == 1 ? L"" : L"s", c->ndatabases, c->ndatabases == 1 ? L"" : L"s",
             why ? L" (" : L"", why ? why : L"", why ? L")" : L"");
    log_text(line);
    log_text(can_download()
                 ? L"Choose the SD card, tick what to install, and choose the folder with your "
                   L"MAME zips if you have one. Files that are not in it are downloaded.\r\n"
                 : L"Choose the SD card, tick what to install, and choose the folder with your "
                   L"MAME zips.\r\n");
    free(line);
    free(why);
    free(from);
}

/* The end of JOB_CONFIG: the latest configuration replaces the one shown, or
   the reason it could not be read is kept, as ua_config_update does. */
static void config_fetched(fetched *f)
{
    if (f->ok) {
        /* Sets ticked in the meantime stay ticked. */
        char **ticked = calloc((size_t)row_count() + 1, sizeof *ticked);
        for (int r = 0, n = 0; r < row_count(); r++)
            if (ListView_GetCheckState(item(IDC_SETS), r))
                ticked[n++] = _strdup(row_id(r));
        int had = can_download();
        ua_config_free(&app.cfg);
        app.cfg = f->cfg;
        fill_sets(ticked);
        size_status_column();
        show_databases(had);
        update_notice();
        for (char **t = ticked; *t; t++)
            free(*t);
        free(ticked);
    } else {
        app.cfg.fetch_error = _strdup(f->err);
    }
    free(f);
    log_config();
}

static void layout(void)
{
    RECT rc;
    GetClientRect(app.dlg, &rc);
    int dx = rc.right - app.client0.cx, dy = rc.bottom - app.client0.cy;
    HDWP dwp = BeginDeferWindowPos(NANCHORS);
    for (int i = 0; i < NANCHORS && dwp; i++) {
        RECT r = app.rects[i];
        int x = r.left, y = r.top, w = r.right - r.left, h = r.bottom - r.top;
        if (anchors[i].how & MOVE_X)
            x += dx;
        if (anchors[i].how & MOVE_Y)
            y += dy;
        if (anchors[i].how & GROW_X)
            w += dx;
        if (anchors[i].how & GROW_Y)
            h += dy;
        if (anchors[i].id == IDC_SD)
            h += 200;            /* a combo box's height includes its list */
        dwp = DeferWindowPos(dwp, item(anchors[i].id), NULL, x, y, w, h,
                             SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (dwp)
        EndDeferWindowPos(dwp);
    size_status_column();
    size_file_columns();
}

static void init_dialog(HWND dlg)
{
    app.dlg = dlg;
    /* ua_program_download() looks for this text in a downloaded copy, to see which
       version it is: change it only together with is_program_of() in ua_core.c. */
    SetWindowTextW(dlg, L"updateAll " UA_VERSION_W);
    HINSTANCE inst = GetModuleHandleW(NULL);
    SendMessageW(dlg, WM_SETICON, ICON_BIG,
                 (LPARAM)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    SendMessageW(dlg, WM_SETICON, ICON_SMALL,
                 (LPARAM)LoadImageW(inst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON,
                                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    RECT rc;
    GetClientRect(dlg, &rc);
    app.client0.cx = rc.right;
    app.client0.cy = rc.bottom;
    for (int i = 0; i < NANCHORS; i++) {
        GetWindowRect(item(anchors[i].id), &app.rects[i]);
        MapWindowPoints(NULL, dlg, (POINT *)&app.rects[i], 2);
    }
    GetWindowRect(dlg, &rc);
    app.min_track.x = rc.right - rc.left;
    app.min_track.y = rc.bottom - rc.top;

    SendMessageW(item(IDC_LOG), EM_SETLIMITTEXT, 0, 0);
    SendMessageW(item(IDC_PROGRESS), PBM_SETRANGE32, 0, 1000);
    init_files();
    size_file_columns();
    show_view(1);

    log_text(L"updateAll installs arcade ROM sets, the BIOS files of the emulators, and the "
             L"shareware or demo data of the ported games, on the SD card. The ROM sets and BIOS "
             L"files are copyright of their manufacturers and are not included with this "
             L"program.\r\n\r\n");
    load_config();
    if (app.cfg_ok) {
        init_sets();
        fill_sets(NULL);
    }

    SendMessageW(item(IDC_SD), CB_SETCUEBANNER, 0, (LPARAM)L"Root of the SD card");
    show_databases(0);
    if (app.cfg_ok && app.cfg.config_url) {
        /* The sets above are shown until the latest configuration is in. */
        job *j = calloc(1, sizeof *j);
        j->kind = JOB_CONFIG;
        j->config_url = _strdup(app.cfg.config_url);
        j->config_dir = _strdup(app.cfg.config_dir);
        start_job(j);
    } else if (app.cfg_ok) {
        log_config();
        check_release();
    }
    if (!app.cfg_ok) {
        for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++)
            EnableWindow(item(inputs[i]), FALSE);
    }

    fill_drives(1);
    size_status_column();
    update_buttons();
    check_card();
    app.ready = 1;
}

static INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        init_dialog(dlg);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
            if (IsWindowEnabled(item(IDOK)))
                install();
            return TRUE;
        case IDCANCEL:
            close_or_cancel();
            return TRUE;
        case IDC_SD_BROWSE:
            browse(IDC_SD, L"Choose the root of the SD card");
            return TRUE;
        case IDC_ZIPS_BROWSE:
            browse(IDC_ZIPS, L"Choose the folder with MAME zips");
            return TRUE;
        case IDC_SD:
            if (HIWORD(wp) == CBN_EDITCHANGE || HIWORD(wp) == CBN_SELCHANGE)
                SetTimer(dlg, TIMER_CHECK, 400, NULL);   /* after the edit field is updated */
            return TRUE;
        case IDC_ZIPS:
            if (HIWORD(wp) == EN_CHANGE)
                update_buttons();
            return TRUE;
        case IDC_DOWNLOAD:
            update_buttons();
            return TRUE;
        case IDC_SELECT_ALL:
            if (HIWORD(wp) == BN_CLICKED) {
                int selected, all;
                select_all_counts(&selected, &all);
                for (int r = 0; r < row_count(); r++)
                    if (in_select_all(r))
                        ListView_SetCheckState(item(IDC_SETS), r, selected < all);
            }
            return TRUE;
        case IDC_SHOWLOG:
            show_view(IsDlgButtonChecked(dlg, IDC_SHOWLOG) == BST_CHECKED);
            return TRUE;
        }
        break;

    case WM_NOTIFY: {
        const NMLISTVIEW *nm = (const NMLISTVIEW *)lp;
        if (nm->hdr.idFrom == IDC_FILES && nm->hdr.code == NM_CUSTOMDRAW) {
            /* A dialog procedure answers a notification through DWLP_MSGRESULT. */
            LRESULT answer = draw_files((NMLVCUSTOMDRAW *)lp);
            SetWindowLongPtrW(dlg, DWLP_MSGRESULT, answer);
            return TRUE;
        }
        if (nm->hdr.idFrom == IDC_SETS && nm->hdr.code == LVN_ITEMCHANGED &&
            (nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK)) {
            if (is_archive_row(nm->iItem) && !app.setting_tick)
                app.archive_touched = 1;
            update_buttons();
        }
        break;
    }

    case WM_TIMER:
        if (wp == TIMER_CHECK) {
            KillTimer(dlg, TIMER_CHECK);
            update_buttons();
            check_card();
        }
        return TRUE;

    case WM_DEVICECHANGE:
        if ((wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) && !app.running) {
            fill_drives(0);
            SetTimer(dlg, TIMER_CHECK, 400, NULL);
        }
        return TRUE;

    case WM_APP_LOG:
        log_text((const wchar_t *)lp);
        remember_line((const wchar_t *)lp);
        free((void *)lp);
        return TRUE;

    case WM_APP_SET:
        set_started((int)wp, (int)lp);
        return TRUE;

    case WM_APP_BYTES:
        bytes_changed((long long)wp, (long long)lp);
        return TRUE;

    case WM_APP_FILE:
        file_changed((file_msg *)lp);
        file_msg_free((file_msg *)lp);
        return TRUE;

    case WM_THEMECHANGED:
        if (app.bar_theme)
            CloseThemeData(app.bar_theme);
        app.bar_theme = OpenThemeData(item(IDC_FILES), VSCLASS_PROGRESS);
        return FALSE;

    case WM_APP_STATUS:
        if (!app.running && app.job == JOB_CHECK && !app.cancel)
            set_status((int)wp, (int)lp);
        return TRUE;

    case WM_APP_CONFIG:
        config_fetched((fetched *)lp);
        return TRUE;

    case WM_APP_RELEASE:
        release_read((release_msg *)lp);
        return TRUE;

    case WM_APP_OFFER:
        offer_update();
        return TRUE;

    case WM_APP_SELF_FAILED:
        show_self_failure();
        return TRUE;

    case WM_APP_SELF_START:
        SetDlgItemTextW(dlg, IDC_STATUS, L"Starting the new version...");
        return TRUE;

    case WM_APP_CARD:
        if (!app.running && app.job == JOB_CHECK && !app.cancel) {
            card_checked((card_msg *)lp);
        } else {
            free(((card_msg *)lp)->tag);
            free((void *)lp);
        }
        return TRUE;

    case WM_APP_DONE:
        job_done((int)wp, (int)lp);
        return TRUE;

    case WM_CLOSE:
        /* The window's close button: a running install is cancelled first. */
        if (app.running && !app.queued) {
            app.closing = 1;
            close_or_cancel();
        } else {
            EndDialog(dlg, 0);
        }
        return TRUE;

    case WM_SIZE:
        if (app.ready)
            layout();
        return TRUE;

    case WM_GETMINMAXINFO:
        if (app.ready)
            ((MINMAXINFO *)lp)->ptMinTrackSize = app.min_track;
        return TRUE;
    }
    return FALSE;
}

/* What is left of the copy of this program that an update replaced. It may still
   be exiting when this copy starts, so this keeps trying for a while. */
static unsigned __stdcall remove_old_copy(void *arg)
{
    wchar_t *old = arg;
    for (int i = 0; i < 50; i++) {
        if (i)
            Sleep(200);
        SetFileAttributesW(old, FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(old) || GetLastError() == ERROR_FILE_NOT_FOUND)
            break;
    }
    free(old);
    return 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev, (void)cmd, (void)show;
    /* No "insert a disk" boxes for card readers without a card. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    wchar_t *exe = own_path();
    if (exe) {
        wchar_t *old = with_suffix(exe, L".old");
        HANDLE h = GetFileAttributesW(old) == INVALID_FILE_ATTRIBUTES
                       ? NULL
                       : (HANDLE)_beginthreadex(NULL, 0, remove_old_copy, old, 0, NULL);
        if (h)
            CloseHandle(h);
        else
            free(old);
        free(exe);
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS |
                                                 ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_MAIN), NULL, dlg_proc, 0);
    CoUninitialize();
    /* ExitProcess also ends a card check that may still be reading. */
    ExitProcess(0);
}
