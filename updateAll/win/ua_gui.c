/*
 * updateAll.exe: the window around the updateAll core. The core runs on a
 * worker thread and reports through posted messages; the window only
 * collects the choices and shows the log the scripts would print.
 */
#define _WIN32_WINNT 0x0601
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <commctrl.h>
#include <dbt.h>
#include <process.h>
#include <shobjidl.h>

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
#define WM_APP_PROGRESS (WM_APP + 2)   /* lParam: 0..1000 */
#define WM_APP_STATUS   (WM_APP + 3)   /* wParam: set, lParam: files current, -1 for no card */
#define WM_APP_DONE     (WM_APP + 4)   /* wParam: job kind, lParam: exit status */
#define TIMER_CHECK     1

enum { JOB_NONE, JOB_CHECK, JOB_INSTALL };

typedef struct {
    int kind;
    char *sd, *zips;
    char **ids;              /* NULL-terminated, or NULL for every set */
    int no_download, dry_run;
    int last_permille;
} job;

/* How each control follows the window when it is resized. */
enum { MOVE_X = 1, MOVE_Y = 2, GROW_X = 4, GROW_Y = 8 };
static const struct {
    int id, how;
} anchors[] = {
    { IDC_SD, GROW_X },          { IDC_SD_BROWSE, MOVE_X },  { IDC_ZIPS, GROW_X },
    { IDC_ZIPS_BROWSE, MOVE_X }, { IDC_SETS, GROW_X },       { IDC_LOG, GROW_X | GROW_Y },
    { IDC_PROGRESS, MOVE_Y | GROW_X }, { IDOK, MOVE_X | MOVE_Y }, { IDCANCEL, MOVE_X | MOVE_Y },
};
#define NANCHORS ((int)(sizeof anchors / sizeof anchors[0]))

static struct {
    HWND dlg;
    int ready;               /* the dialog is set up */
    ua_config cfg;
    int cfg_ok;
    int running;             /* an install was asked for and has not finished */
    int job;                 /* the job the worker thread runs, JOB_NONE if idle */
    job *queued;             /* an install waiting for a card check to stop */
    int check_again;         /* the card changed while a job ran */
    int closing;
    volatile LONG cancel;
    RECT rects[NANCHORS];
    SIZE client0;
    POINT min_track;
} app;

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
    PostMessageW(app.dlg, WM_APP_PROGRESS, 0, nsets ? n * 1000 / nsets : 0);
}

static void hook_bytes(void *ctx, long long done, long long total)
{
    job *j = ctx;
    if (total <= 0)
        return;
    int permille = (int)(done * 1000 / total);
    if (permille != j->last_permille) {
        j->last_permille = permille;
        PostMessageW(app.dlg, WM_APP_PROGRESS, 0, permille);
    }
}

static int hook_cancelled(void *ctx)
{
    (void)ctx;
    return app.cancel != 0;
}

static void job_free(job *j)
{
    if (!j)
        return;
    free(j->sd);
    free(j->zips);
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
        for (int i = 0; i < app.cfg.ncores && !app.cancel; i++)
            PostMessageW(app.dlg, WM_APP_STATUS, i,
                         card ? ua_count_current(&app.cfg.cores[i], j->sd) : -1);
    } else {
        ua_hooks hooks = { hook_log, hook_set, hook_bytes, hook_cancelled, j };
        ua_options opt = { j->sd, j->zips, (const char **)j->ids, NULL, j->no_download,
                           j->dry_run };
        rc = ua_run(&app.cfg, &opt, &hooks);
    }
    PostMessageW(app.dlg, WM_APP_DONE, j->kind, rc);
    job_free(j);
    return 0;
}

static void start_job(job *j)
{
    app.cancel = 0;
    app.job = j->kind;
    j->last_permille = -1;
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
    for (int i = 0; i < app.cfg.ncores; i++)
        n += ListView_GetCheckState(item(IDC_SETS), i) != 0;
    return n;
}

static int may_download(void)
{
    return app.cfg_ok && app.cfg.ndatabases > 0 && IsDlgButtonChecked(app.dlg, IDC_DOWNLOAD);
}

static void update_buttons(void)
{
    int ready = app.cfg_ok && !app.running && GetWindowTextLengthW(item(IDC_SD)) > 0 &&
                checked_sets() > 0 &&
                (GetWindowTextLengthW(item(IDC_ZIPS)) > 0 || may_download());
    EnableWindow(item(IDOK), ready);
}

static void set_running(int running)
{
    static const int inputs[] = { IDC_SD,   IDC_SD_BROWSE, IDC_ZIPS, IDC_ZIPS_BROWSE,
                                  IDC_SETS, IDC_DOWNLOAD,  IDC_DRYRUN };
    app.running = running;
    for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++)
        EnableWindow(item(inputs[i]), !running && (inputs[i] != IDC_DOWNLOAD || app.cfg.ndatabases));
    SetDlgItemTextW(app.dlg, IDCANCEL, running ? L"Cancel" : L"Close");
    EnableWindow(item(IDCANCEL), TRUE);
    update_buttons();
    if (!running)
        SetFocus(item(IDCANCEL));
}

static void set_status(int set, int current)
{
    wchar_t text[64] = L"";
    int total = app.cfg.cores[set].nfiles;
    if (current == total)
        wcscpy(text, L"Up to date");
    else if (current == 0)
        wcscpy(text, L"Not installed");
    else if (current > 0)
        swprintf(text, 64, L"%d of %d files missing", total - current, total);
    ListView_SetItemText(item(IDC_SETS), set, 3, text);
}

/* Checks which sets are complete on the card, for the status column. */
static void check_card(void)
{
    if (!app.cfg_ok || app.running)
        return;
    if (app.job == JOB_CHECK) {
        app.cancel = 1;          /* restart it for the new card */
        app.check_again = 1;
        return;
    }
    wchar_t sd[MAX_PATH * 2];
    sd_path(sd, MAX_PATH * 2);
    if (!*sd) {
        for (int i = 0; i < app.cfg.ncores; i++)
            set_status(i, -1);
        return;
    }
    job *j = calloc(1, sizeof *j);
    j->kind = JOB_CHECK;
    j->sd = ua_narrow(sd);
    start_job(j);
}

static void install(void)
{
    wchar_t sd[MAX_PATH * 2], zips[MAX_PATH * 2];
    sd_path(sd, MAX_PATH * 2);
    field_path(IDC_ZIPS, zips, MAX_PATH * 2);

    job *j = calloc(1, sizeof *j);
    j->kind = JOB_INSTALL;
    j->sd = ua_narrow(sd);
    j->zips = *zips ? ua_narrow(zips) : NULL;
    j->no_download = !may_download();
    j->dry_run = IsDlgButtonChecked(app.dlg, IDC_DRYRUN) == BST_CHECKED;
    /* Unticked sets work like --core with the ticked ones. */
    if (checked_sets() < app.cfg.ncores) {
        j->ids = calloc((size_t)app.cfg.ncores + 1, sizeof *j->ids);
        for (int i = 0, n = 0; i < app.cfg.ncores; i++)
            if (ListView_GetCheckState(item(IDC_SETS), i))
                j->ids[n++] = _strdup(app.cfg.cores[i].id);
    }

    SetDlgItemTextW(app.dlg, IDC_LOG, L"");
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

static void job_done(int kind, int rc)
{
    app.job = JOB_NONE;
    if (kind == JOB_INSTALL) {
        SendMessageW(item(IDC_PROGRESS), PBM_SETPOS, 1000, 0);
        SendMessageW(item(IDC_PROGRESS), PBM_SETSTATE, rc ? PBST_ERROR : PBST_NORMAL, 0);
        MessageBeep(rc ? MB_ICONWARNING : MB_OK);
        if (app.closing) {
            EndDialog(app.dlg, 0);
            return;
        }
        set_running(0);
        app.check_again = 1;
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
    if (SUCCEEDED(IFileOpenDialog_Show(d, app.dlg)) &&
        SUCCEEDED(IFileOpenDialog_GetResult(d, &result))) {
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
static void load_config(wchar_t *source, size_t size)
{
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
    for (int i = 0; i < app.cfg.ncores; i++) {
        const ua_core *c = &app.cfg.cores[i];
        wchar_t *name = ua_widen(c->name), *dest = ua_widen(c->dest_shown);
        size_t len = 1;
        for (int z = 0; z < c->nzips; z++)
            len += strlen(c->zips[z]) + 2;
        char *zips = calloc(1, len);
        for (int z = 0; z < c->nzips; z++) {
            if (z)
                strcat(zips, ", ");
            strcat(zips, c->zips[z]);
        }
        wchar_t *wzips = ua_widen(zips);
        LVITEMW it = { .mask = LVIF_TEXT, .iItem = i, .pszText = name };
        ListView_InsertItem(lv, &it);
        ListView_SetItemText(lv, i, 1, wzips);
        ListView_SetItemText(lv, i, 2, dest);
        ListView_SetCheckState(lv, i, TRUE);
        free(name);
        free(dest);
        free(zips);
        free(wzips);
    }
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
}

static void init_dialog(HWND dlg)
{
    app.dlg = dlg;
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

    wchar_t source[MAX_PATH + 32];
    log_text(L"updateAll installs arcade ROM sets on the SD card. The ROM sets are copyright "
             L"of their manufacturers and are not included with this program.\r\n\r\n");
    load_config(source, MAX_PATH + 32);
    if (app.cfg_ok) {
        init_sets();
        wchar_t line[MAX_PATH + 160];
        swprintf(line, MAX_PATH + 160, L"Configuration: %ls, %d set%ls, %d download database%ls\r\n",
                 source, app.cfg.ncores, app.cfg.ncores == 1 ? L"" : L"s", app.cfg.ndatabases,
                 app.cfg.ndatabases == 1 ? L"" : L"s");
        log_text(line);
        log_text(app.cfg.ndatabases
                     ? L"Choose the SD card, and the folder with your MAME zips if you have one. "
                       L"Zips that are not in it are downloaded.\r\n"
                     : L"Choose the SD card and the folder with your MAME zips.\r\n");
    }

    SendMessageW(item(IDC_SD), CB_SETCUEBANNER, 0, (LPARAM)L"Root of the SD card");
    SendMessageW(item(IDC_ZIPS), EM_SETCUEBANNER, TRUE,
                 (LPARAM)(app.cfg.ndatabases ? L"Optional: folder with MAME zips"
                                             : L"Folder with MAME zips"));
    if (app.cfg.ndatabases) {
        CheckDlgButton(dlg, IDC_DOWNLOAD, BST_CHECKED);
    } else {
        SetDlgItemTextW(dlg, IDC_DOWNLOAD, L"Download missing zips (no database configured)");
        EnableWindow(item(IDC_DOWNLOAD), FALSE);
    }
    if (!app.cfg_ok) {
        static const int inputs[] = { IDC_SD,   IDC_SD_BROWSE, IDC_ZIPS, IDC_ZIPS_BROWSE,
                                      IDC_SETS, IDC_DOWNLOAD,  IDC_DRYRUN };
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
        }
        break;

    case WM_NOTIFY: {
        const NMLISTVIEW *nm = (const NMLISTVIEW *)lp;
        if (nm->hdr.idFrom == IDC_SETS && nm->hdr.code == LVN_ITEMCHANGED &&
            (nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK))
            update_buttons();
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
        free((void *)lp);
        return TRUE;

    case WM_APP_PROGRESS:
        SendMessageW(item(IDC_PROGRESS), PBM_SETPOS, (WPARAM)lp, 0);
        return TRUE;

    case WM_APP_STATUS:
        if (!app.running && app.job == JOB_CHECK && !app.cancel)
            set_status((int)wp, (int)lp);
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

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev, (void)cmd, (void)show;
    /* No "insert a disk" boxes for card readers without a card. */
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS |
                                                 ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    DialogBoxParamW(inst, MAKEINTRESOURCEW(IDD_MAIN), NULL, dlg_proc, 0);
    CoUninitialize();
    /* ExitProcess also ends a card check that may still be reading. */
    ExitProcess(0);
}
