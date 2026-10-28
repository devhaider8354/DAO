/*
 * main.c - DAO user interface (Win32, dark theme, English / Arabic with RTL layout)
 */
#include "engine.h"
#include "i18n.h"
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#endif

/* ------------------------------------------------------------------ */
/*                               Colors                                */
/* ------------------------------------------------------------------ */
#define C_BG        RGB(16, 18, 24)
#define C_PANEL     RGB(23, 26, 34)
#define C_PANEL2    RGB(27, 31, 40)
#define C_FIELD     RGB(33, 37, 48)
#define C_BORDER    RGB(46, 52, 66)
#define C_TEXT      RGB(232, 235, 242)
#define C_MUTED     RGB(138, 146, 166)
#define C_ACCENT    RGB(59, 130, 246)
#define C_ACCENT_HI RGB(96, 156, 250)
#define C_GREEN     RGB(34, 197, 94)
#define C_ORANGE    RGB(245, 158, 11)
#define C_RED       RGB(239, 68, 68)
#define C_ROW_SEL   RGB(35, 50, 80)
#define C_BTN       RGB(36, 41, 53)
#define C_BTN_HI    RGB(48, 55, 71)

/* Segoe MDL2 Assets glyphs */
#define G_ADD      L"\xE710"
#define G_PLAY     L"\xE768"
#define G_PAUSE    L"\xE769"
#define G_DELETE   L"\xE74D"
#define G_FILE     L"\xE8E5"
#define G_FOLDER   L"\xE838"
#define G_DOWN     L"\xE896"
#define G_LINK     L"\xE71B"
#define G_SPEED    L"\xEC4A"
#define G_GLOBE    L"\xE774"
#define G_CHECK    L"\xE73E"
#define G_CANCEL   L"\xE711"
#define G_COPY     L"\xE8C8"

/* MessageBox / menu flags that follow the UI direction */
#define MB_DIR  (g_rtl ? (MB_RTLREADING | MB_RIGHT) : 0)
#define TPM_DIR (g_rtl ? (TPM_LAYOUTRTL | TPM_RIGHTALIGN) : TPM_LEFTALIGN)
#define WS_EX_DIR (g_rtl ? WS_EX_LAYOUTRTL : 0)

enum {
    IDC_URL = 101, IDC_CONN, IDC_FOLDER, IDC_ADD,
    IDC_RESUME = 110, IDC_PAUSE, IDC_DELETE, IDC_OPEN, IDC_OPENDIR, IDC_RESUMEALL, IDC_PAUSEALL, IDC_CHROME, IDC_LANG,
    IDC_LIST = 120, IDC_DETAILS, IDC_STATUS,
    IDM_RESUME = 200, IDM_PAUSE, IDM_OPEN, IDM_OPENDIR, IDM_COPYURL, IDM_REMOVE, IDM_REMOVEFILE
};

enum { COL_NAME, COL_SIZE, COL_PROGRESS, COL_SPEED, COL_ETA, COL_STATUS, COL_COUNT };

/* ------------------------------------------------------------------ */
/*                               Globals                               */
/* ------------------------------------------------------------------ */
static HINSTANCE g_inst;
static HWND      g_main, g_url, g_conn, g_list, g_details, g_status;
static HFONT     g_font, g_font_bold, g_font_small, g_font_title, g_font_icon, g_font_icon_big;
static HBRUSH    g_br_bg, g_br_field;
static int       g_dpi = 96;
static HIMAGELIST g_sysimg;

#define MAX_DL 1024
static Download *g_dl[MAX_DL];
static int       g_ndl;
static WCHAR     g_folder[MAX_PATH];
static int       g_nconn = DEFAULT_CONNS;
static WCHAR     g_cfg_dir[MAX_PATH];
static int       g_rtl;          /* Arabic UI: mirrored window layout */
static int       g_restart;      /* relaunch after closing (language change) */

typedef struct {
    int id;
    const WCHAR *glyph;
    const WCHAR *text;
    int style;          /* 0 = normal, 1 = primary (accent) */
    HWND hwnd;
    int hover;
} Button;

typedef struct { int id; const WCHAR *glyph; StrId text; int style; } ButtonDef;

static const ButtonDef g_btn_defs[] = {
    { IDC_FOLDER,    G_FOLDER,   S_BTN_FOLDER,    0 },
    { IDC_ADD,       G_ADD,      S_BTN_ADD,       1 },
    { IDC_RESUME,    G_PLAY,     S_BTN_RESUME,    0 },
    { IDC_PAUSE,     G_PAUSE,    S_BTN_PAUSE,     0 },
    { IDC_DELETE,    G_DELETE,   S_BTN_DELETE,    0 },
    { IDC_OPEN,      G_FILE,     S_BTN_OPEN,      0 },
    { IDC_OPENDIR,   G_FOLDER,   S_BTN_OPENDIR,   0 },
    { IDC_PAUSEALL,  G_PAUSE,    S_BTN_PAUSEALL,  0 },
    { IDC_RESUMEALL, G_PLAY,     S_BTN_RESUMEALL, 0 },
    { IDC_CHROME,    G_LINK,     S_BTN_CHROME,    0 },
    { IDC_LANG,      G_GLOBE,    S_LANG_SWITCH,   0 },
};
#define NBTN ((int)(sizeof(g_btn_defs) / sizeof(g_btn_defs[0])))
static Button g_btn[NBTN];

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static Button *find_btn(int id)
{
    for (int i = 0; i < NBTN; i++) if (g_btn[i].id == id) return &g_btn[i];
    return NULL;
}

/* ------------------------------------------------------------------ */
/*                           Drawing helpers                           */
/* ------------------------------------------------------------------ */
static void fill_rect(HDC dc, const RECT *r, COLORREF c)
{
    SetDCBrushColor(dc, c);
    FillRect(dc, r, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void round_rect(HDC dc, const RECT *r, int rad, COLORREF fill, COLORREF border)
{
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, fill);
    SetDCPenColor(dc, border);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
}

static void draw_text(HDC dc, const WCHAR *s, RECT *r, HFONT f, COLORREF c, UINT flags)
{
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, r, flags | DT_NOPREFIX);
    SelectObject(dc, of);
}

static int text_width(HDC dc, const WCHAR *s, HFONT f)
{
    SIZE sz;
    HGDIOBJ of = SelectObject(dc, f);
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    SelectObject(dc, of);
    return sz.cx;
}

static COLORREF status_color(int st)
{
    switch (st) {
    case ST_DOWNLOADING: return C_ACCENT;
    case ST_CONNECTING:  return C_ACCENT_HI;
    case ST_DONE:        return C_GREEN;
    case ST_PAUSED:
    case ST_PAUSING:     return C_ORANGE;
    case ST_ERROR:       return C_RED;
    default:             return C_MUTED;
    }
}

static const WCHAR *status_text(int st)
{
    switch (st) {
    case ST_QUEUED:      return T(S_ST_QUEUED);
    case ST_CONNECTING:  return T(S_ST_CONNECTING);
    case ST_DOWNLOADING: return T(S_ST_DOWNLOADING);
    case ST_PAUSING:     return T(S_ST_PAUSING);
    case ST_PAUSED:      return T(S_ST_PAUSED);
    case ST_DONE:        return T(S_ST_DONE);
    case ST_ERROR:       return T(S_ST_ERROR);
    }
    return L"";
}

static void error_text(const Download *d, WCHAR *out, size_t n)
{
    switch (d->err) {
    case ERR_BAD_URL:     wcsncpy(out, T(S_ERR_BAD_URL), n); break;
    case ERR_CONNECT:     swprintf(out, n, T(S_ERR_CONNECT), d->err_detail); break;
    case ERR_HTTP:        swprintf(out, n, T(S_ERR_HTTP), d->err_detail); break;
    case ERR_DISK_WRITE:  wcsncpy(out, T(S_ERR_DISK_WRITE), n); break;
    case ERR_RETRIES:     wcsncpy(out, T(S_ERR_RETRIES), n); break;
    case ERR_CREATE_FILE: wcsncpy(out, T(S_ERR_CREATE_FILE), n); break;
    case ERR_DISK_FULL:   wcsncpy(out, T(S_ERR_DISK_FULL), n); break;
    case ERR_RENAME:      wcsncpy(out, T(S_ERR_RENAME), n); break;
    default:              out[0] = 0; break;
    }
    out[n - 1] = 0;
}

/* Mirror a rect drawn in RTL-designed coordinates when the UI is LTR
   (used by the panels that paint with an unmirrored DC). */
static RECT vrect(int l, int t, int r, int b, int w)
{
    RECT rc = { l, t, r, b };
    if (!g_rtl) { rc.left = w - r; rc.right = w - l; }
    return rc;
}

#define AL_START (g_rtl ? (DT_RIGHT | DT_RTLREADING) : DT_LEFT)
#define AL_END   (g_rtl ? (DT_LEFT | DT_RTLREADING) : DT_RIGHT)

/* ------------------------------------------------------------------ */
/*                       Settings and download list                    */
/* ------------------------------------------------------------------ */
static void cfg_path(WCHAR *out, const WCHAR *file)
{
    swprintf(out, MAX_PATH, L"%ls\\%ls", g_cfg_dir, file);
}

static void load_settings(void)
{
    WCHAR ini[MAX_PATH], def[MAX_PATH];
    PWSTR dl = NULL;
    def[0] = 0;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Downloads, 0, NULL, &dl)) && dl) {
        wcsncpy(def, dl, MAX_PATH - 1);
        CoTaskMemFree(dl);
    } else {
        GetCurrentDirectoryW(MAX_PATH, def);
    }
    cfg_path(ini, L"dao.ini");
    GetPrivateProfileStringW(L"dao", L"folder", def, g_folder, MAX_PATH, ini);
    g_nconn = GetPrivateProfileIntW(L"dao", L"connections", DEFAULT_CONNS, ini);
    if (g_nconn < 1 || g_nconn > MAX_CONNECTIONS) g_nconn = DEFAULT_CONNS;
    int lang = GetPrivateProfileIntW(L"dao", L"language", -1, ini);
    g_lang = (lang == LANG_EN || lang == LANG_AR) ? (UiLang)lang : i18n_default_lang();
    g_rtl = (g_lang == LANG_AR);
}

static void save_settings(void)
{
    WCHAR ini[MAX_PATH], n[16];
    cfg_path(ini, L"dao.ini");
    swprintf(n, 16, L"%d", g_nconn);
    WritePrivateProfileStringW(L"dao", L"folder", g_folder, ini);
    WritePrivateProfileStringW(L"dao", L"connections", n, ini);
    swprintf(n, 16, L"%d", (int)g_lang);
    WritePrivateProfileStringW(L"dao", L"language", n, ini);
}

static void save_list(void)
{
    WCHAR p[MAX_PATH];
    cfg_path(p, L"downloads.txt");
    FILE *f = _wfopen(p, L"w, ccs=UTF-8");
    if (!f) return;
    for (int i = 0; i < g_ndl; i++) {
        Download *d = g_dl[i];
        if (d->remove_after) continue;
        fwprintf(f, L"%d\t%lld\t%d\t%ls\t%ls\t%ls\t%ls\t%d\t%ls\t%ls\t%ls\n",
                 d->status == ST_DONE ? 1 : 0, d->total, d->nconn,
                 d->url, d->folder, d->path[0] ? d->path : L"-", d->name, d->name_fixed,
                 d->cookie ? d->cookie : L"", d->referer ? d->referer : L"", d->ua ? d->ua : L"");
    }
    fclose(f);
}

static WCHAR *next_field(WCHAR **p)
{
    WCHAR *s = *p, *t;
    if (!s) return NULL;
    t = wcschr(s, L'\t');
    if (t) { *t = 0; *p = t + 1; } else { *p = NULL; }
    return s;
}

static void load_list(void)
{
    WCHAR p[MAX_PATH];
    static WCHAR line[65536];
    cfg_path(p, L"downloads.txt");
    FILE *f = _wfopen(p, L"r, ccs=UTF-8");
    if (!f) return;
    while (g_ndl < MAX_DL && fgetws(line, ARRAYSIZE(line), f)) {
        line[wcscspn(line, L"\r\n")] = 0;
        WCHAR *cur = line;
        WCHAR *done = next_field(&cur), *total = next_field(&cur), *nc = next_field(&cur);
        WCHAR *url = next_field(&cur), *folder = next_field(&cur), *path = next_field(&cur);
        WCHAR *name = next_field(&cur);
        WCHAR *fixed = next_field(&cur), *cookie = next_field(&cur);
        WCHAR *referer = next_field(&cur), *ua = next_field(&cur);
        if (!name || !url[0]) continue;

        Download *d = dl_create(url, folder, _wtoi(nc), g_main);
        if (!d) break;
        if (wcscmp(path, L"-") != 0) wcsncpy(d->path, path, ARRAYSIZE(d->path) - 1);
        wcsncpy(d->name, name, MAX_PATH - 1);
        d->total = _wtoi64(total);
        if (fixed) d->name_fixed = _wtoi(fixed);
        if (cookie || referer || ua) dl_set_headers(d, cookie, referer, ua);
        if (_wtoi(done)) {
            d->status = ST_DONE;
        } else {
            d->status = ST_PAUSED;
            if (d->path[0]) dl_load_state(d);
        }
        g_dl[g_ndl++] = d;
    }
    fclose(f);
}

/* ------------------------------------------------------------------ */
/*                               Commands                              */
/* ------------------------------------------------------------------ */
static Download *selected(int *index)
{
    int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (index) *index = i;
    return (i >= 0 && i < g_ndl) ? g_dl[i] : NULL;
}

static void refresh_all(void)
{
    InvalidateRect(g_list, NULL, FALSE);
    InvalidateRect(g_details, NULL, FALSE);
    InvalidateRect(g_status, NULL, FALSE);
}

static void update_buttons(void)
{
    Download *d = selected(NULL);
    int st = d ? (int)d->status : -1;
    EnableWindow(find_btn(IDC_RESUME)->hwnd, d && st != ST_DONE && !dl_is_running(d));
    EnableWindow(find_btn(IDC_PAUSE)->hwnd,  d && dl_is_running(d) && st != ST_PAUSING);
    EnableWindow(find_btn(IDC_DELETE)->hwnd, d != NULL);
    EnableWindow(find_btn(IDC_OPEN)->hwnd,   d && st == ST_DONE);
    EnableWindow(find_btn(IDC_OPENDIR)->hwnd, d != NULL);
}

static int read_conn(void)
{
    WCHAR b[16];
    GetWindowTextW(g_conn, b, 16);
    int n = _wtoi(b);
    if (n < 1) n = 1;
    if (n > MAX_CONNECTIONS) n = MAX_CONNECTIONS;
    return n;
}

static int is_url(const WCHAR *s)
{
    return !_wcsnicmp(s, L"http://", 7) || !_wcsnicmp(s, L"https://", 8);
}

static void trim(WCHAR *s)
{
    WCHAR *p = s;
    while (*p == L' ' || *p == L'\t' || *p == L'"' || *p == L'\r' || *p == L'\n') p++;
    memmove(s, p, (wcslen(p) + 1) * sizeof(WCHAR));
    size_t L = wcslen(s);
    while (L && (s[L - 1] == L' ' || s[L - 1] == L'"' || s[L - 1] == L'\r' || s[L - 1] == L'\n' || s[L - 1] == L'\t')) s[--L] = 0;
}

static void msg(const WCHAR *text, UINT icon)
{
    MessageBoxW(g_main, text, L"DAO", icon | MB_DIR);
}

static void add_download(const WCHAR *src)
{
    WCHAR url[4096];
    if (src) wcsncpy(url, src, ARRAYSIZE(url) - 1), url[ARRAYSIZE(url) - 1] = 0;
    else GetWindowTextW(g_url, url, ARRAYSIZE(url));
    trim(url);
    if (!url[0]) { SetFocus(g_url); return; }
    if (!is_url(url)) { msg(T(S_MSG_BAD_URL), MB_ICONWARNING); return; }
    if (g_ndl >= MAX_DL) { msg(T(S_MSG_LIST_FULL), MB_ICONWARNING); return; }

    for (int i = 0; i < g_ndl; i++) {
        if (!wcscmp(g_dl[i]->url, url) && g_dl[i]->status != ST_DONE) {
            ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED);
            ListView_SetItemState(g_list, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
            ListView_EnsureVisible(g_list, i, FALSE);
            if (!dl_is_running(g_dl[i])) dl_start(g_dl[i]);
            SetWindowTextW(g_url, L"");
            return;
        }
    }

    g_nconn = read_conn();
    save_settings();
    CreateDirectoryW(g_folder, NULL);
    Download *d = dl_create(url, g_folder, g_nconn, g_main);
    if (!d) return;
    g_dl[g_ndl++] = d;
    ListView_SetItemCountEx(g_list, g_ndl, LVSICF_NOSCROLL);
    ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(g_list, g_ndl - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(g_list, g_ndl - 1, FALSE);
    dl_start(d);
    SetWindowTextW(g_url, L"");
    save_list();
    update_buttons();
    refresh_all();
}

static void remove_index(int i)
{
    Download *d = g_dl[i];
    ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED);
    memmove(&g_dl[i], &g_dl[i + 1], (g_ndl - i - 1) * sizeof(Download *));
    g_ndl--;
    ListView_SetItemCountEx(g_list, g_ndl, LVSICF_NOSCROLL);
    dl_free(d);
    save_list();
    update_buttons();
    refresh_all();
}

static void delete_files(Download *d)
{
    WCHAR p[MAX_PATH * 2 + 8];
    if (!d->path[0]) return;
    dl_part_path(d, p, ARRAYSIZE(p));  DeleteFileW(p);
    dl_state_path(d, p, ARRAYSIZE(p)); DeleteFileW(p);
    if (d->status == ST_DONE) DeleteFileW(d->path);
}

static void delete_download(Download *d, int with_files)
{
    int idx = -1;
    for (int i = 0; i < g_ndl; i++) if (g_dl[i] == d) idx = i;
    if (idx < 0) return;
    if (dl_is_running(d)) {
        InterlockedExchange(&d->remove_after, with_files ? 2 : 1);
        dl_pause(d);          /* removal finishes when the stop notification arrives */
        save_list();
        return;
    }
    if (with_files) delete_files(d);
    remove_index(idx);
}

static void delete_selected(int ask)
{
    Download *sel[MAX_DL];
    int n = 0, with_files = 0;
    for (int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED); i >= 0;
         i = ListView_GetNextItem(g_list, i, LVNI_SELECTED))
        sel[n++] = g_dl[i];
    if (!n) return;
    if (ask == 1) {
        int r = MessageBoxW(g_main, T(S_DEL_ASK), T(S_DEL_TITLE),
                            MB_YESNOCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_DIR);
        if (r == IDCANCEL) return;
        with_files = (r == IDYES);
    } else {
        with_files = (ask == 2);
    }
    for (int i = 0; i < n; i++) delete_download(sel[i], with_files);
}

static void for_selected(int resume)
{
    for (int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED); i >= 0;
         i = ListView_GetNextItem(g_list, i, LVNI_SELECTED)) {
        if (resume) dl_start(g_dl[i]); else dl_pause(g_dl[i]);
    }
    update_buttons();
    refresh_all();
}

static void open_file(Download *d)
{
    if (d && d->status == ST_DONE) ShellExecuteW(g_main, L"open", d->path, NULL, NULL, SW_SHOWNORMAL);
}

static void open_folder(Download *d)
{
    if (!d) return;
    WCHAR args[MAX_PATH * 2 + 32];
    if (d->status == ST_DONE && GetFileAttributesW(d->path) != INVALID_FILE_ATTRIBUTES) {
        swprintf(args, ARRAYSIZE(args), L"/select,\"%ls\"", d->path);
        ShellExecuteW(g_main, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
    } else {
        ShellExecuteW(g_main, L"open", d->folder, NULL, NULL, SW_SHOWNORMAL);
    }
}

static void copy_text(const WCHAR *s)
{
    if (!OpenClipboard(g_main)) return;
    EmptyClipboard();
    size_t bytes = (wcslen(s) + 1) * sizeof(WCHAR);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        memcpy(GlobalLock(h), s, bytes);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

static int clipboard_url(WCHAR *out, size_t n)
{
    int ok = 0;
    out[0] = 0;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(g_main)) return 0;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const WCHAR *s = (const WCHAR *)GlobalLock(h);
        if (s) {
            wcsncpy(out, s, n - 1);
            out[n - 1] = 0;
            GlobalUnlock(h);
            trim(out);
            ok = is_url(out) && !wcschr(out, L'\n') && !wcschr(out, L' ');
        }
    }
    CloseClipboard();
    return ok;
}

static int pick_folder(HWND owner, WCHAR *out)
{
    BROWSEINFOW bi;
    int ok = 0;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = T(S_PICK_FOLDER);
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        ok = SHGetPathFromIDListW(pidl, out);
        CoTaskMemFree(pidl);
    }
    return ok;
}

static void choose_folder(void)
{
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = g_main;
    bi.lpszTitle = T(S_PICK_FOLDER);
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        WCHAR p[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, p)) {
            wcscpy(g_folder, p);
            save_settings();
            InvalidateRect(g_status, NULL, FALSE);
        }
        CoTaskMemFree(pidl);
    }
}

/* ------------------------------------------------------------------ */
/*                          Owner-drawn buttons                        */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK btn_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    Button *b = (Button *)ref;
    (void)id;
    switch (m) {
    case WM_MOUSEMOVE:
        if (!b->hover) {
            TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            b->hover = 1;
            InvalidateRect(h, NULL, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        b->hover = 0;
        InvalidateRect(h, NULL, FALSE);
        break;
    case WM_ERASEBKGND:
        return 1;
    }
    return DefSubclassProc(h, m, w, l);
}

static void draw_button(DRAWITEMSTRUCT *di)
{
    Button *b = (Button *)GetWindowLongPtrW(di->hwndItem, GWLP_USERDATA);
    if (!b) return;
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    int w = r.right - r.left, h = r.bottom - r.top;
    int disabled = (di->itemState & ODS_DISABLED) != 0;
    int pressed  = (di->itemState & ODS_SELECTED) != 0;

    SetLayout(dc, 0);      /* paint in plain visual coordinates */
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ ob = SelectObject(mem, bmp);
    RECT rr = { 0, 0, w, h };
    fill_rect(mem, &rr, C_BG);

    COLORREF fill, border, fg;
    if (b->style == 1) {
        fill = disabled ? C_BTN : (pressed ? C_ACCENT : (b->hover ? C_ACCENT_HI : C_ACCENT));
        border = fill;
        fg = disabled ? C_MUTED : RGB(255, 255, 255);
    } else {
        fill = disabled ? C_PANEL : (pressed ? C_FIELD : (b->hover ? C_BTN_HI : C_BTN));
        border = disabled ? C_PANEL2 : C_BORDER;
        fg = disabled ? RGB(80, 86, 102) : C_TEXT;
    }
    round_rect(mem, &rr, S(10), fill, border);

    /* icon goes on the leading side: right of the text in RTL, left in LTR */
    int tw = text_width(mem, b->text, g_font);
    int gw = text_width(mem, b->glyph, g_font_icon);
    int gap = S(8);
    int total = tw + gap + gw;
    int x = (w - total) / 2;
    int tx = g_rtl ? x : x + gw + gap;
    int gx = g_rtl ? x + tw + gap : x;
    RECT tr = { tx, 0, tx + tw + 2, h };
    draw_text(mem, b->text, &tr, g_font, fg, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    RECT gr = { gx, 0, gx + gw + 2, h };
    draw_text(mem, b->glyph, &gr, g_font_icon, b->style == 1 ? fg : (disabled ? fg : C_ACCENT_HI),
              DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
}

/* ------------------------------------------------------------------ */
/*                             Download list                           */
/* ------------------------------------------------------------------ */
static int col_rect(int item, int col, RECT *rc)
{
    if (col == 0) {
        RECT b;
        if (!ListView_GetItemRect(g_list, item, &b, LVIR_BOUNDS)) return 0;
        rc->left = b.left;
        rc->right = b.left + ListView_GetColumnWidth(g_list, 0);
        rc->top = b.top; rc->bottom = b.bottom;
        return 1;
    }
    return ListView_GetSubItemRect(g_list, item, col, LVIR_BOUNDS, rc);
}

static int file_icon(const WCHAR *name)
{
    SHFILEINFOW fi;
    memset(&fi, 0, sizeof(fi));
    if (SHGetFileInfoW(name, FILE_ATTRIBUTE_NORMAL, &fi, sizeof(fi),
                       SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON))
        return fi.iIcon;
    return 0;
}

static void draw_row(HDC dc, int i)
{
    Download *d = g_dl[i];
    RECT row, c;
    if (!ListView_GetItemRect(g_list, i, &row, LVIR_BOUNDS)) return;
    RECT client;
    GetClientRect(g_list, &client);
    row.right = max(row.right, client.right);

    int sel = ListView_GetItemState(g_list, i, LVIS_SELECTED) & LVIS_SELECTED;
    fill_rect(dc, &row, sel ? C_ROW_SEL : ((i & 1) ? C_PANEL2 : C_PANEL));
    if (sel) {
        RECT bar = { row.left, row.top, row.left + S(3), row.bottom };
        fill_rect(dc, &bar, C_ACCENT);
    }
    RECT line = { row.left, row.bottom - 1, row.right, row.bottom };
    fill_rect(dc, &line, C_BG);

    int st = d->status;
    LONGLONG done = dl_done_bytes(d), total = d->total;
    WCHAR buf[256], a[32], b[32];
    UINT tf = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_LEFT;
    int pad = S(10);

    /* icon + name */
    if (col_rect(i, COL_NAME, &c)) {
        int ih = GetSystemMetrics(SM_CYSMICON);
        if (g_sysimg)
            ImageList_Draw(g_sysimg, file_icon(d->name), dc, c.left + pad + S(2),
                           (c.top + c.bottom - ih) / 2, ILD_TRANSPARENT);
        RECT t = { c.left + pad + S(2) + ih + S(10), c.top, c.right - S(4), c.bottom };
        draw_text(dc, d->name, &t, g_font, C_TEXT, tf);
    }
    /* size */
    if (col_rect(i, COL_SIZE, &c)) {
        if (total > 0) { fmt_size(total, a, 32); swprintf(buf, 64, L"\x202A%ls\x202C", a); } else wcscpy(buf, L"—");
        RECT t = { c.left + pad, c.top, c.right - S(4), c.bottom };
        draw_text(dc, buf, &t, g_font, C_MUTED, tf);
    }
    /* progress bar */
    if (col_rect(i, COL_PROGRESS, &c)) {
        double pct = st == ST_DONE ? 100.0 : (total > 0 ? 100.0 * (double)done / (double)total : 0);
        int bh = S(18);
        RECT tr = { c.left + pad, (c.top + c.bottom - bh) / 2, c.right - pad, (c.top + c.bottom + bh) / 2 };
        round_rect(dc, &tr, bh, C_FIELD, C_FIELD);
        int fw = (int)((tr.right - tr.left) * pct / 100.0);
        if (fw > 0) {
            RECT fr = tr;
            fr.right = tr.left + max(fw, bh);
            if (fr.right > tr.right) fr.right = tr.right;
            round_rect(dc, &fr, bh, status_color(st), status_color(st));
        }
        if (total > 0 || st == ST_DONE) swprintf(buf, 64, L"\x202A%.1f%%\x202C", pct);
        else { fmt_size(done, a, 32); swprintf(buf, 64, L"\x202A%ls\x202C", a); }
        draw_text(dc, buf, &tr, g_font_small, RGB(255, 255, 255), DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }
    /* speed */
    if (col_rect(i, COL_SPEED, &c)) {
        buf[0] = 0;
        if (st == ST_DOWNLOADING) { fmt_size(d->speed, a, 32); swprintf(buf, 64, L"\x202A%ls/s\x202C", a); }
        RECT t = { c.left + pad, c.top, c.right - S(4), c.bottom };
        draw_text(dc, buf, &t, g_font, C_TEXT, tf);
    }
    /* time left */
    if (col_rect(i, COL_ETA, &c)) {
        buf[0] = 0;
        if (st == ST_DOWNLOADING && total > 0) { fmt_time(d->speed > 0 ? (total - done) / d->speed : -1, a, 32); swprintf(buf, 64, L"\x202A%ls\x202C", a); }
        RECT t = { c.left + pad, c.top, c.right - S(4), c.bottom };
        draw_text(dc, buf, &t, g_font, C_MUTED, tf);
    }
    /* status */
    if (col_rect(i, COL_STATUS, &c)) {
        int r = S(8);
        RECT dot = { c.left + pad, (c.top + c.bottom - r) / 2, c.left + pad + r, (c.top + c.bottom + r) / 2 };
        HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        SetDCBrushColor(dc, status_color(st));
        Ellipse(dc, dot.left, dot.top, dot.right + 1, dot.bottom + 1);
        SelectObject(dc, ob); SelectObject(dc, op);
        if (st == ST_DOWNLOADING) swprintf(buf, 64, L"%ls (%ld)", status_text(st), d->conns);
        else wcscpy(buf, status_text(st));
        RECT t = { dot.right + S(8), c.top, c.right - S(4), c.bottom };
        draw_text(dc, buf, &t, g_font, status_color(st) == C_MUTED ? C_MUTED : C_TEXT, tf);
    }
    (void)b;
}

static LRESULT list_custom_draw(NMLVCUSTOMDRAW *cd)
{
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
    case CDDS_ITEMPREPAINT:
        if ((int)cd->nmcd.dwItemSpec < g_ndl) draw_row(cd->nmcd.hdc, (int)cd->nmcd.dwItemSpec);
        return CDRF_SKIPDEFAULT;
    case CDDS_POSTPAINT: {
        /* paint the empty area below the last row (hides column divider lines) */
        RECT cr, last;
        GetClientRect(g_list, &cr);
        int top = 0;
        if (g_ndl > 0 && ListView_GetItemRect(g_list, g_ndl - 1, &last, LVIR_BOUNDS)) top = last.bottom;
        if (top < 0) top = 0;
        if (top < cr.bottom) { RECT e = { cr.left, top, cr.right, cr.bottom }; fill_rect(cd->nmcd.hdc, &e, C_PANEL); }
    }
        if (g_ndl == 0) {
            RECT r;
            GetClientRect(g_list, &r);
            r.top += S(40);
            RECT g = r; g.bottom = g.top + S(70);
            draw_text(cd->nmcd.hdc, G_DOWN, &g, g_font_icon_big, C_BORDER, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            RECT t = r; t.top = g.bottom + S(6); t.bottom = t.top + S(30);
            draw_text(cd->nmcd.hdc, T(S_EMPTY_TITLE), &t, g_font_bold, C_MUTED, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
            t.top = t.bottom; t.bottom = t.top + S(26);
            draw_text(cd->nmcd.hdc, T(S_EMPTY_HINT), &t, g_font, RGB(95, 102, 120), DT_SINGLELINE | DT_CENTER | DT_VCENTER);
        }
        return CDRF_DODEFAULT;
    }
    return CDRF_DODEFAULT;
}

/* Subclass the list view to custom-draw its header in the dark theme. */
static LRESULT CALLBACK list_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;
    if (m == WM_NOTIFY) {
        NMHDR *nh = (NMHDR *)l;
        if (nh->hwndFrom == ListView_GetHeader(h) && nh->code == NM_CUSTOMDRAW) {
            NMCUSTOMDRAW *cd = (NMCUSTOMDRAW *)l;
            if (cd->dwDrawStage == CDDS_PREPAINT) {
                RECT r;
                GetClientRect(nh->hwndFrom, &r);
                fill_rect(cd->hdc, &r, C_BG);
                return CDRF_NOTIFYITEMDRAW;
            }
            if (cd->dwDrawStage == CDDS_ITEMPREPAINT) {
                WCHAR txt[64];
                HDITEMW hi;
                memset(&hi, 0, sizeof(hi));
                hi.mask = HDI_TEXT; hi.pszText = txt; hi.cchTextMax = 64;
                Header_GetItem(nh->hwndFrom, (int)cd->dwItemSpec, &hi);
                RECT r = cd->rc;
                fill_rect(cd->hdc, &r, C_BG);
                RECT sep = { r.right - 1, r.top + S(8), r.right, r.bottom - S(8) };
                fill_rect(cd->hdc, &sep, C_BORDER);
                RECT bl = { r.left, r.bottom - 1, r.right, r.bottom };
                fill_rect(cd->hdc, &bl, C_BORDER);
                r.left += S(12); r.right -= S(6);
                draw_text(cd->hdc, txt, &r, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
                return CDRF_SKIPDEFAULT;
            }
        }
    }
    return DefSubclassProc(h, m, w, l);
}

static void size_columns(void)
{
    RECT r;
    GetClientRect(g_list, &r);
    int others = 0;
    for (int i = 1; i < COL_COUNT; i++) others += ListView_GetColumnWidth(g_list, i);
    int w = r.right - others;
    if (w < S(180)) w = S(180);
    ListView_SetColumnWidth(g_list, 0, w);
}

static void create_list(void)
{
    g_list = CreateWindowExW(0, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_OWNERDATA |
                             LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                             0, 0, 100, 100, g_main, (HMENU)IDC_LIST, g_inst, NULL);
    ListView_SetExtendedListViewStyleEx(g_list, 0xFFFFFFFF, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetWindowTheme(g_list, L"DarkMode_Explorer", NULL);
    SetWindowTheme(ListView_GetHeader(g_list), L"DarkMode_ItemsView", NULL);
    ListView_SetBkColor(g_list, C_PANEL);
    ListView_SetTextBkColor(g_list, C_PANEL);
    ListView_SetTextColor(g_list, C_TEXT);
    SendMessageW(g_list, WM_SETFONT, (WPARAM)g_font, TRUE);

    /* dummy image list: only used to set the row height */
    HIMAGELIST il = ImageList_Create(1, S(42), ILC_COLOR32, 1, 1);
    ListView_SetImageList(g_list, il, LVSIL_SMALL);

    SHFILEINFOW fi;
    g_sysimg = (HIMAGELIST)SHGetFileInfoW(L".txt", FILE_ATTRIBUTE_NORMAL, &fi, sizeof(fi),
                                          SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);

    const WCHAR *names[COL_COUNT] = { T(S_COL_NAME), T(S_COL_SIZE), T(S_COL_PROGRESS),
                                      T(S_COL_SPEED), T(S_COL_ETA), T(S_COL_STATUS) };
    int widths[COL_COUNT] = { 300, 95, 190, 105, 85, 165 };
    for (int i = 0; i < COL_COUNT; i++) {
        LVCOLUMNW c;
        memset(&c, 0, sizeof(c));
        c.mask = LVCF_TEXT | LVCF_WIDTH;
        c.pszText = (WCHAR *)names[i];
        c.cx = S(widths[i]);
        ListView_InsertColumn(g_list, i, &c);
    }
    HWND hdr = ListView_GetHeader(g_list);
    SendMessageW(hdr, WM_SETFONT, (WPARAM)g_font_small, TRUE);
    SetWindowSubclass(g_list, list_proc, 1, 0);
    ListView_SetItemCountEx(g_list, g_ndl, 0);
}

/* ------------------------------------------------------------------ */
/*                      Details panel and status bar                   */
/* ------------------------------------------------------------------ */
/* These panels paint with an unmirrored DC, so rects are laid out for RTL
   and flipped with vrect() when the UI is LTR. */

static void paint_details(HDC dc, int w, int h)
{
    RECT all = { 0, 0, w, h };
    fill_rect(dc, &all, C_BG);
    RECT card = { 0, 0, w, h };
    round_rect(dc, &card, S(14), C_PANEL, C_BORDER);

    Download *d = selected(NULL);
    int pad = S(18);
    UINT R = DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | AL_START;

    if (!d) {
        draw_text(dc, T(S_DET_NONE), &card, g_font, C_MUTED,
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER | (g_rtl ? DT_RTLREADING : 0));
        return;
    }

    Segment seg[MAX_SEGMENTS];
    int nseg;
    LONGLONG total, done = dl_done_bytes(d);
    int st = d->status, ranges;
    WCHAR err[256];
    EnterCriticalSection(&d->lock);
    nseg = d->nseg;
    memcpy(seg, d->seg, sizeof(Segment) * nseg);
    total = d->total;
    ranges = d->ranges;
    error_text(d, err, ARRAYSIZE(err));
    LeaveCriticalSection(&d->lock);

    WCHAR buf[1024], a[32], b[32], c[32];

    /* line 1: status badge + file name */
    int top = S(12), lh = S(28);
    int sw = text_width(dc, status_text(st), g_font_bold) + S(28);
    RECT rs = vrect(pad, top + S(3), pad + sw, top + lh - S(3), w);
    round_rect(dc, &rs, S(22), C_PANEL2, status_color(st));
    draw_text(dc, status_text(st), &rs, g_font_bold, status_color(st), DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    RECT rn = vrect(pad + sw + S(12), top, w - pad, top + lh, w);
    draw_text(dc, d->name, &rn, g_font_title, C_TEXT, R);

    /* line 2: URL or error */
    int y2 = top + lh + S(4);
    RECT r2 = { pad, y2, w - pad, y2 + S(20) };
    if (st == ST_ERROR && err[0]) {
        swprintf(buf, 1024, T(S_DET_ERROR), err);
        draw_text(dc, buf, &r2, g_font_small, C_RED, R);
    } else {
        swprintf(buf, 1024, T(S_DET_URL), d->url);
        draw_text(dc, buf, &r2, g_font_small, C_MUTED, R);
    }

    /* segment map: file offset 0 starts on the leading side */
    RECT map = { pad, r2.bottom + S(10), w - pad, r2.bottom + S(10) + S(22) };
    round_rect(dc, &map, S(8), C_FIELD, C_FIELD);
    int mw = map.right - map.left - S(4);
    int m0 = map.left + S(2);
    #define MAPX(off) (g_rtl ? (m0 + mw) - (int)((double)(off) / total * mw) : m0 + (int)((double)(off) / total * mw))
    if (st == ST_DONE) {
        RECT f = { map.left + S(2), map.top + S(2), map.right - S(2), map.bottom - S(2) };
        fill_rect(dc, &f, C_GREEN);
    } else if (total > 0 && ranges && nseg > 0) {
        /* everything outside the remaining ranges is already downloaded */
        RECT f = { map.left + S(2), map.top + S(2), map.right - S(2), map.bottom - S(2) };
        fill_rect(dc, &f, RGB(30, 90, 170));
        for (int i = 0; i < nseg; i++) {
            if (seg[i].pos >= seg[i].end) continue;
            int x1 = MAPX(seg[i].pos), x2 = MAPX(seg[i].end);
            RECT e = { min(x1, x2), map.top + S(2), max(x1, x2), map.bottom - S(2) };
            fill_rect(dc, &e, C_FIELD);
            if (seg[i].active && st == ST_DOWNLOADING) {          /* write head */
                RECT hd = { g_rtl ? x1 - S(3) : x1, map.top + S(2), g_rtl ? x1 : x1 + S(3), map.bottom - S(2) };
                fill_rect(dc, &hd, C_ACCENT_HI);
            }
        }
        for (int i = 0; i < nseg; i++) {                            /* segment borders */
            if (seg[i].start <= 0) continue;
            int x = MAPX(seg[i].start);
            RECT sl = { x, map.top + S(2), x + 1, map.bottom - S(2) };
            fill_rect(dc, &sl, C_BG);
        }
    } else if (total > 0) {
        int fw = (int)((double)done / total * mw);
        RECT f = g_rtl ? (RECT){ m0 + mw - fw, map.top + S(2), m0 + mw, map.bottom - S(2) }
                       : (RECT){ m0, map.top + S(2), m0 + fw, map.bottom - S(2) };
        fill_rect(dc, &f, C_ACCENT);
    }
    #undef MAPX

    /* line 4: statistics */
    RECT r4 = { pad, map.bottom + S(8), w - pad, map.bottom + S(8) + S(22) };
    fmt_size(done, a, 32);
    if (total > 0) fmt_size(total, b, 32); else wcscpy(b, T(S_QMARK));
    int parts_left = 0;
    for (int i = 0; i < nseg; i++) if (seg[i].pos < seg[i].end) parts_left++;
    const WCHAR *resume = ranges ? T(S_SUPPORTED)
                                 : (total < 0 && st != ST_DONE ? T(S_UNKNOWN) : T(S_NOT_SUPPORTED));
    if (st == ST_DOWNLOADING) {
        WCHAR eta[32];
        fmt_size(d->speed, c, 32);
        fmt_time(d->speed > 0 && total > 0 ? (total - done) / d->speed : -1, eta, 32);
        swprintf(buf, 1024, T(S_DET_STATS_ACTIVE), a, b, c, eta, d->conns, parts_left, resume);
    } else {
        swprintf(buf, 1024, T(S_DET_STATS_IDLE), a, b, parts_left, resume, d->path[0] ? d->path : d->folder);
    }
    draw_text(dc, buf, &r4, g_font_small, C_MUTED, R);
}

static void paint_status(HDC dc, int w, int h)
{
    RECT all = { 0, 0, w, h };
    fill_rect(dc, &all, C_BG);
    RECT line = { 0, 0, w, 1 };
    fill_rect(dc, &line, C_PANEL2);

    LONGLONG speed = 0;
    int active = 0, done = 0;
    for (int i = 0; i < g_ndl; i++) {
        if (g_dl[i]->status == ST_DOWNLOADING || g_dl[i]->status == ST_CONNECTING) { active++; speed += g_dl[i]->speed; }
        if (g_dl[i]->status == ST_DONE) done++;
    }
    WCHAR s[64], buf[512];
    int pad = S(16);

    /* leading side: speed and counters */
    fmt_size(speed, s, 64);
    swprintf(buf, 512, T(S_BAR_STATS), s, active, done, g_ndl);
    RECT g = vrect(w - pad - S(18), 0, w - pad, h, w);
    draw_text(dc, G_SPEED, &g, g_font_icon, C_ACCENT_HI, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    RECT t = vrect(w / 2, 0, w - pad - S(26), h, w);
    draw_text(dc, buf, &t, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | AL_START);

    /* trailing side: download folder */
    swprintf(buf, 512, T(S_BAR_FOLDER), g_folder);
    RECT f = vrect(pad + S(26), 0, w / 2 - S(10), h, w);
    draw_text(dc, buf, &f, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS | AL_END);
    RECT fg = vrect(pad, 0, pad + S(20), h, w);
    draw_text(dc, G_FOLDER, &fg, g_font_icon, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

static LRESULT CALLBACK panel_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        SetLayout(dc, 0);
        RECT r;
        GetClientRect(h, &r);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, r.right, r.bottom);
        HGDIOBJ ob = SelectObject(mem, bmp);
        if (GetDlgCtrlID(h) == IDC_DETAILS) paint_details(mem, r.right, r.bottom);
        else paint_status(mem, r.right, r.bottom);
        BitBlt(dc, 0, 0, r.right, r.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, ob);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(h, &ps);
        return 0;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------------------ */
/*                               URL field                             */
/* ------------------------------------------------------------------ */
static LRESULT CALLBACK edit_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    (void)id; (void)ref;
    if (m == WM_KEYDOWN && w == VK_RETURN) { add_download(NULL); return 0; }
    if (m == WM_CHAR && (w == VK_RETURN || w == 1 /* Ctrl+A */)) {
        if (w == 1) SendMessageW(h, EM_SETSEL, 0, -1);
        return 0;
    }
    return DefSubclassProc(h, m, w, l);
}

/* ------------------------------------------------------------------ */
/*                   Browser bridge (Native Messaging)                 */
/* ------------------------------------------------------------------ */
#define NATIVE_HOST_NAME L"com.flowpoint.dao"
#define EXTENSION_ID     L"eojnoldnafpphjnhlafncmodihfhciam"

/* Register dao-host.exe with Chrome / Edge / Brave so the extension can reach DAO. */
static void register_native_host(void)
{
    WCHAR exe[MAX_PATH], host[MAX_PATH], json[MAX_PATH], esc[MAX_PATH * 2];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    WCHAR *slash = wcsrchr(exe, L'\\');
    if (!slash) return;
    *slash = 0;
    swprintf(host, MAX_PATH, L"%ls\\dao-host.exe", exe);
    if (GetFileAttributesW(host) == INVALID_FILE_ATTRIBUTES) return;

    /* escape backslashes for JSON */
    size_t j = 0;
    for (size_t i = 0; host[i] && j < ARRAYSIZE(esc) - 2; i++) {
        if (host[i] == L'\\' || host[i] == L'"') esc[j++] = L'\\';
        esc[j++] = host[i];
    }
    esc[j] = 0;

    WCHAR content[4096];
    swprintf(content, ARRAYSIZE(content),
             L"{\n  \"name\": \"%ls\",\n  \"description\": \"DAO Download Manager\",\n"
             L"  \"path\": \"%ls\",\n  \"type\": \"stdio\",\n"
             L"  \"allowed_origins\": [\"chrome-extension://%ls/\"]\n}\n",
             NATIVE_HOST_NAME, esc, EXTENSION_ID);

    cfg_path(json, L"com.flowpoint.dao.json");
    char utf8[8192];
    int n = WideCharToMultiByte(CP_UTF8, 0, content, -1, utf8, sizeof(utf8), NULL, NULL);
    HANDLE f = CreateFileW(json, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD wr;
    WriteFile(f, utf8, (DWORD)(n > 0 ? n - 1 : 0), &wr, NULL);
    CloseHandle(f);

    const WCHAR *keys[] = {
        L"Software\\Google\\Chrome\\NativeMessagingHosts\\" NATIVE_HOST_NAME,
        L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\" NATIVE_HOST_NAME,
        L"Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts\\" NATIVE_HOST_NAME,
        L"Software\\Chromium\\NativeMessagingHosts\\" NATIVE_HOST_NAME,
    };
    for (int i = 0; i < 4; i++) {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, keys[i], 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(k, NULL, 0, REG_SZ, (const BYTE *)json, (DWORD)((wcslen(json) + 1) * sizeof(WCHAR)));
            RegCloseKey(k);
        }
    }
}

/* ------------------------------------------------------------------ */
/*                          Add to download list                       */
/* ------------------------------------------------------------------ */
static void list_append(Download *d, int start)
{
    if (g_ndl >= MAX_DL) { dl_free(d); return; }
    g_dl[g_ndl++] = d;
    ListView_SetItemCountEx(g_list, g_ndl, LVSICF_NOSCROLL);
    ListView_SetItemState(g_list, -1, 0, LVIS_SELECTED);
    ListView_SetItemState(g_list, g_ndl - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(g_list, g_ndl - 1, FALSE);
    if (start) dl_start(d);
    save_list();
    update_buttons();
    refresh_all();
}

/* ------------------------------------------------------------------ */
/*          "New download" dialog (shown for browser downloads)         */
/* ------------------------------------------------------------------ */
#define WM_INFO_READY (WM_APP + 5)

enum { IDD_NAME = 301, IDD_CONN, IDD_FOLDER, IDD_START, IDD_LATER, IDD_CANCEL };

typedef struct {
    HWND hwnd, name, conn;
    WCHAR url[4096];
    WCHAR *cookie, *referer, *ua;
    WCHAR folder[MAX_PATH];
    LONGLONG size;
    int ranges;             /* -1 = not known yet */
    int probing;
    int name_edited, setting_text, browser_name;
    WCHAR info_error[256];
    HICON icon;
    Button btn[4];
    RECT name_box, folder_box, conn_box;
} AddDlg;

typedef struct { HWND hwnd; Download *d; } ProbeJob;

static WCHAR *wdup(const WCHAR *s)
{
    if (!s) s = L"";
    size_t n = wcslen(s) + 1;
    WCHAR *p = (WCHAR *)malloc(n * sizeof(WCHAR));
    if (p) memcpy(p, s, n * sizeof(WCHAR));
    return p;
}

static DWORD WINAPI probe_thread(LPVOID p)
{
    ProbeJob *job = (ProbeJob *)p;
    int ok = dl_fetch_info(job->d);
    if (!PostMessageW(job->hwnd, WM_INFO_READY, ok, (LPARAM)job->d))
        dl_free(job->d);
    free(job);
    return 0;
}

static void dlg_set_icon(AddDlg *a)
{
    WCHAR name[MAX_PATH];
    SHFILEINFOW fi;
    GetWindowTextW(a->name, name, MAX_PATH);
    if (a->icon) DestroyIcon(a->icon);
    a->icon = NULL;
    memset(&fi, 0, sizeof(fi));
    if (SHGetFileInfoW(name[0] ? name : L"file.bin", FILE_ATTRIBUTE_NORMAL, &fi, sizeof(fi),
                       SHGFI_USEFILEATTRIBUTES | SHGFI_ICON | SHGFI_LARGEICON))
        a->icon = fi.hIcon;
}

static void clean_name(WCHAR *s)
{
    for (; *s; s++) if (wcschr(L"\\/:*?\"<>|", *s) || *s < 32) *s = L'_';
}

static void dlg_finish(AddDlg *a, int mode)   /* 0 = cancel, 1 = start, 2 = later */
{
    if (mode) {
        WCHAR name[MAX_PATH], n[8];
        GetWindowTextW(a->name, name, MAX_PATH);
        trim(name);
        clean_name(name);
        GetWindowTextW(a->conn, n, 8);
        int nc = _wtoi(n);
        if (nc < 1) nc = 1;
        if (nc > MAX_CONNECTIONS) nc = MAX_CONNECTIONS;

        CreateDirectoryW(a->folder, NULL);
        Download *d = dl_create(a->url, a->folder, nc, g_main);
        if (d) {
            dl_set_headers(d, a->cookie, a->referer, a->ua);
            if (name[0]) { wcsncpy(d->name, name, MAX_PATH - 1); d->name_fixed = 1; }
            if (a->size > 0) d->total = a->size;
            if (mode == 2) d->status = ST_PAUSED;
            list_append(d, mode == 1);
        }
    }
    DestroyWindow(a->hwnd);
}

static void dlg_layout(AddDlg *a)
{
    RECT r;
    GetClientRect(a->hwnd, &r);
    int W = r.right, H = r.bottom, m = S(22), gap = S(10);
    int fh = S(42), eh = S(22);

    a->name_box = (RECT){ m, S(112), W - m, S(112) + fh };
    MoveWindow(a->name, m + S(14), a->name_box.top + (fh - eh) / 2, W - 2 * m - S(28), eh, TRUE);

    int connw = S(150), chg = S(110);
    a->folder_box = (RECT){ m, S(214), W - m - connw - chg - 2 * gap, S(214) + fh };
    MoveWindow(a->btn[0].hwnd, a->folder_box.right + gap, S(214), chg, fh, TRUE);
    a->conn_box = (RECT){ W - m - connw, S(214), W - m, S(214) + fh };
    MoveWindow(a->conn, a->conn_box.right - S(44), a->conn_box.top + (fh - eh) / 2, S(30), eh, TRUE);

    int bh = S(44), by = H - m - bh;
    int bw1 = S(170), bw2 = S(120), bw3 = S(110);
    int x = W - m - bw1;
    MoveWindow(a->btn[1].hwnd, x, by, bw1, bh, TRUE);       /* start */
    x -= gap + bw2;
    MoveWindow(a->btn[2].hwnd, x, by, bw2, bh, TRUE);       /* later */
    MoveWindow(a->btn[3].hwnd, m, by, bw3, bh, TRUE);       /* cancel */
}

static void dlg_paint(AddDlg *a, HDC dc)
{
    RECT r;
    GetClientRect(a->hwnd, &r);
    int W = r.right, m = S(22);
    WCHAR buf[1024], s1[32];
    fill_rect(dc, &r, C_BG);

    /* header */
    RECT ic = { m, S(20), m + S(52), S(72) };
    round_rect(dc, &ic, S(14), C_PANEL2, C_BORDER);
    if (a->icon) DrawIconEx(dc, ic.left + S(10), ic.top + S(10), a->icon, S(32), S(32), 0, NULL, DI_NORMAL);
    RECT t = { ic.right + S(14), S(18), W - m, S(46) };
    draw_text(dc, T(S_DLG_HEADER), &t, g_font_title, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    RECT u = { ic.right + S(14), S(48), W - m, S(70) };
    swprintf(buf, 1024, L"\x200E%ls", a->url);
    draw_text(dc, buf, &u, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);

    /* file name */
    RECT l1 = { m, S(88), W - m, S(108) };
    draw_text(dc, T(S_DLG_FILENAME), &l1, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    round_rect(dc, &a->name_box, S(12), C_FIELD, GetFocus() == a->name ? C_ACCENT : C_BORDER);

    /* file info */
    RECT inf = { m, S(162), W - m, S(194) };
    if (a->probing && a->size <= 0) {
        draw_text(dc, T(S_DLG_FETCHING), &inf, g_font, C_ACCENT_HI, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    } else {
        if (a->size > 0) { fmt_size(a->size, s1, 32); } else wcscpy(s1, T(S_UNKNOWN));
        const WCHAR *rs = a->ranges == 1 ? T(S_DLG_SUPPORTED) : (a->ranges == 0 ? T(S_NOT_SUPPORTED) : L"...");
        swprintf(buf, 1024, T(S_DLG_INFO), s1, rs);
        draw_text(dc, buf, &inf, g_font_bold, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        if (a->info_error[0]) {
            RECT e = { m, S(188), W - m, S(208) };
            draw_text(dc, a->info_error, &e, g_font_small, C_ORANGE, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        }
    }

    /* download folder + connections */
    RECT l2 = { m, S(194), W - m, S(212) };
    if (!a->info_error[0]) draw_text(dc, T(S_DLG_SAVE_TO), &l2, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    round_rect(dc, &a->folder_box, S(12), C_FIELD, C_BORDER);
    RECT fg = { a->folder_box.left + S(12), a->folder_box.top, a->folder_box.left + S(34), a->folder_box.bottom };
    draw_text(dc, G_FOLDER, &fg, g_font_icon, C_ACCENT_HI, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    RECT fp = { fg.right + S(8), a->folder_box.top, a->folder_box.right - S(12), a->folder_box.bottom };
    swprintf(buf, 1024, L"\x200E%ls", a->folder);
    draw_text(dc, buf, &fp, g_font, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_PATH_ELLIPSIS);

    round_rect(dc, &a->conn_box, S(12), C_FIELD, GetFocus() == a->conn ? C_ACCENT : C_BORDER);
    RECT cl = { a->conn_box.left + S(12), a->conn_box.top, a->conn_box.right - S(46), a->conn_box.bottom };
    draw_text(dc, T(S_CONNECTIONS), &cl, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    /* separator above the buttons */
    RECT sep = { 0, r.bottom - S(88), W, r.bottom - S(87) };
    fill_rect(dc, &sep, C_PANEL2);
}

static LRESULT CALLBACK dlg_edit_proc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR ref)
{
    AddDlg *a = (AddDlg *)ref;
    (void)id;
    if (m == WM_KEYDOWN && w == VK_RETURN) { dlg_finish(a, 1); return 0; }
    if (m == WM_KEYDOWN && w == VK_ESCAPE) { dlg_finish(a, 0); return 0; }
    if (m == WM_CHAR && (w == VK_RETURN || w == VK_ESCAPE)) return 0;
    if (m == WM_CHAR && w == 1) { SendMessageW(h, EM_SETSEL, 0, -1); return 0; }
    return DefSubclassProc(h, m, w, l);
}

static LRESULT CALLBACK add_dlg_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    AddDlg *a = (AddDlg *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_NCCREATE: {
        CREATESTRUCTW *cs = (CREATESTRUCTW *)l;
        a = (AddDlg *)cs->lpCreateParams;
        a->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)a);
        break;
    }
    case WM_CREATE: {
        a->name = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                  0, 0, 10, 10, h, (HMENU)IDD_NAME, g_inst, NULL);
        SendMessageW(a->name, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
        SetWindowSubclass(a->name, dlg_edit_proc, 1, (DWORD_PTR)a);
        WCHAR n[16];
        swprintf(n, 16, L"%d", g_nconn);
        a->conn = CreateWindowExW(0, L"EDIT", n, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER,
                                  0, 0, 10, 10, h, (HMENU)IDD_CONN, g_inst, NULL);
        SendMessageW(a->conn, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
        SendMessageW(a->conn, EM_SETLIMITTEXT, 2, 0);
        SetWindowSubclass(a->conn, dlg_edit_proc, 1, (DWORD_PTR)a);

        Button defs[4] = {
            { IDD_FOLDER, G_FOLDER, T(S_DLG_CHANGE), 0, NULL, 0 },
            { IDD_START,  G_DOWN,   T(S_DLG_START),  1, NULL, 0 },
            { IDD_LATER,  G_PAUSE,  T(S_DLG_LATER),  0, NULL, 0 },
            { IDD_CANCEL, G_CANCEL, T(S_DLG_CANCEL), 0, NULL, 0 },
        };
        for (int i = 0; i < 4; i++) {
            a->btn[i] = defs[i];
            a->btn[i].hwnd = CreateWindowExW(0, L"BUTTON", defs[i].text,
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                             0, 0, 10, 10, h, (HMENU)(INT_PTR)defs[i].id, g_inst, NULL);
            SetWindowLongPtrW(a->btn[i].hwnd, GWLP_USERDATA, (LONG_PTR)&a->btn[i]);
            SetWindowSubclass(a->btn[i].hwnd, btn_proc, 1, (DWORD_PTR)&a->btn[i]);
        }
        return 0;
    }
    case WM_SIZE:
        if (a) dlg_layout(a);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        dlg_paint(a, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_FIELD);
        return (LRESULT)g_br_field;
    }
    case WM_DRAWITEM:
        draw_button((DRAWITEMSTRUCT *)l);
        return TRUE;
    case WM_INFO_READY: {
        Download *d = (Download *)l;
        a->probing = 0;
        if (w) {
            a->size = d->total;
            a->ranges = d->ranges;
            if (!a->name_edited && !a->browser_name && d->name[0]) {
                a->setting_text = 1;
                SetWindowTextW(a->name, d->name);
                a->setting_text = 0;
                dlg_set_icon(a);
            }
        } else {
            WCHAR e[200];
            error_text(d, e, ARRAYSIZE(e));
            swprintf(a->info_error, 256, T(S_DLG_PROBE_FAILED), e);
        }
        dl_free(d);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDD_NAME:
            if (HIWORD(w) == EN_CHANGE && !a->setting_text) { a->name_edited = 1; dlg_set_icon(a); InvalidateRect(h, NULL, FALSE); }
            if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) InvalidateRect(h, &a->name_box, FALSE);
            break;
        case IDD_CONN:
            if (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS) InvalidateRect(h, &a->conn_box, FALSE);
            break;
        case IDD_FOLDER: {
            WCHAR p[MAX_PATH];
            if (pick_folder(h, p)) { wcscpy(a->folder, p); InvalidateRect(h, NULL, FALSE); }
            break;
        }
        case IDOK:
        case IDD_START:  dlg_finish(a, 1); break;
        case IDD_LATER:  dlg_finish(a, 2); break;
        case IDCANCEL:
        case IDD_CANCEL: dlg_finish(a, 0); break;
        }
        return 0;
    case WM_CLOSE:
        dlg_finish(a, 0);
        return 0;
    case WM_NCDESTROY:
        if (a) {
            if (a->icon) DestroyIcon(a->icon);
            free(a->cookie); free(a->referer); free(a->ua);
            free(a);
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
        }
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

static void show_add_dialog(const WCHAR *url, const WCHAR *filename, const WCHAR *referer,
                            const WCHAR *cookie, const WCHAR *ua, LONGLONG size)
{
    AddDlg *a = (AddDlg *)calloc(1, sizeof(AddDlg));
    if (!a) return;
    wcsncpy(a->url, url, ARRAYSIZE(a->url) - 1);
    a->cookie  = wdup(cookie);
    a->referer = wdup(referer);
    a->ua      = wdup(ua);
    wcsncpy(a->folder, g_folder, MAX_PATH - 1);
    a->size = size;
    a->ranges = -1;

    RECT rc = { 0, 0, S(640), S(372) };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    DWORD ex = WS_EX_DIR | WS_EX_TOPMOST | WS_EX_APPWINDOW;
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;

    /* center on the monitor under the mouse */
    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY), &mi);
    int x = (mi.rcWork.left + mi.rcWork.right - ww) / 2;
    int y = (mi.rcWork.top + mi.rcWork.bottom - wh) / 2;

    HWND h = CreateWindowExW(ex, L"DaoAddDialog", T(S_DLG_WINDOW), style,
                             x, y, ww, wh, NULL, NULL, g_inst, a);
    if (!h) { free(a->cookie); free(a->referer); free(a->ua); free(a); return; }

    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20, &dark, sizeof(dark));
    DwmSetWindowAttribute(h, 19, &dark, sizeof(dark));
    COLORREF cap = C_BG;
    DwmSetWindowAttribute(h, 35, &cap, sizeof(cap));

    /* initial name: from the browser, otherwise from the URL */
    WCHAR name[MAX_PATH];
    if (filename && filename[0]) { wcsncpy(name, filename, MAX_PATH - 1); name[MAX_PATH - 1] = 0; a->browser_name = 1; }
    else name_from_url_str(url, name, MAX_PATH);
    a->setting_text = 1;
    SetWindowTextW(a->name, name);
    a->setting_text = 0;
    dlg_set_icon(a);

    /* probe in the background for the real name, size and range support */
    ProbeJob *job = (ProbeJob *)malloc(sizeof(ProbeJob));
    Download *pd = dl_create(url, a->folder, 1, NULL);
    if (job && pd) {
        dl_set_headers(pd, cookie, referer, ua);
        job->hwnd = h;
        job->d = pd;
        a->probing = 1;
        HANDLE t = CreateThread(NULL, 0, probe_thread, job, 0, NULL);
        if (t) CloseHandle(t); else { a->probing = 0; dl_free(pd); free(job); }
    } else {
        free(job);
        if (pd) dl_free(pd);
    }

    ShowWindow(h, SW_SHOW);
    SetForegroundWindow(h);
    BringWindowToTop(h);
    SetFocus(a->btn[1].hwnd);
    MessageBeep(MB_ICONASTERISK);
}

/* ------------------------------------------------------------------ */
/*                   Chrome setup guide + connection state             */
/* ------------------------------------------------------------------ */
#define USER_KEY L"Software\\FlowPoint\\DAO"

static HWND g_guide;
static int  g_ext_connected;

static DWORD reg_get_dword(const WCHAR *name, DWORD def)
{
    DWORD v = def, sz = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, USER_KEY, name, RRF_RT_REG_DWORD, NULL, &v, &sz) != ERROR_SUCCESS) return def;
    return v;
}

static void reg_set_dword(const WCHAR *name, DWORD v)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, USER_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&v, sizeof v);
        RegCloseKey(k);
    }
}

static void extension_dir(WCHAR *out)
{
    GetModuleFileNameW(NULL, out, MAX_PATH);
    WCHAR *s = wcsrchr(out, L'\\');
    if (s) wcscpy(s + 1, L"chrome-extension");
}

static void update_chrome_button(void)
{
    Button *b = find_btn(IDC_CHROME);
    if (!b) return;
    b->text  = g_ext_connected ? T(S_BTN_CHROME_OK) : T(S_BTN_CHROME);
    b->glyph = g_ext_connected ? G_CHECK : G_LINK;
    InvalidateRect(b->hwnd, NULL, FALSE);
}

/* Open chrome://extensions in the user's Chrome. */
static int open_chrome_extensions(void)
{
    WCHAR chrome[MAX_PATH] = L"";
    DWORD sz = sizeof chrome;
    const WCHAR *key = L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\chrome.exe";
    if (RegGetValueW(HKEY_CURRENT_USER, key, NULL, RRF_RT_REG_SZ, NULL, chrome, &sz) != ERROR_SUCCESS) {
        sz = sizeof chrome;
        if (RegGetValueW(HKEY_LOCAL_MACHINE, key, NULL, RRF_RT_REG_SZ, NULL, chrome, &sz) != ERROR_SUCCESS)
            chrome[0] = 0;
    }
    if (!chrome[0]) return 0;
    return (INT_PTR)ShellExecuteW(g_main, L"open", chrome, L"chrome://extensions/", NULL, SW_SHOWNORMAL) > 32;
}

typedef struct {
    Button btn[4];
    int connected;
    DWORD anim;
} Guide;

enum { IDG_OPEN = 401, IDG_COPY, IDG_LATER, IDG_DONE };

static void guide_paint(HWND h, HDC dc)
{
    Guide *g = (Guide *)GetWindowLongPtrW(h, GWLP_USERDATA);
    RECT r;
    GetClientRect(h, &r);
    int W = r.right, m = S(24);
    fill_rect(dc, &r, C_BG);

    RECT t = { m, S(18), W - m, S(50) };
    draw_text(dc, T(S_GUIDE_TITLE), &t, g_font_title, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    RECT st = { m, S(50), W - m, S(72) };
    draw_text(dc, T(S_GUIDE_SUBTITLE), &st, g_font_small, C_MUTED,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    const WCHAR *titles[3] = {
        T(S_GUIDE_STEP1),
        T(S_GUIDE_STEP2),
        T(S_GUIDE_STEP3),
    };
    const WCHAR *subs[3] = {
        T(S_GUIDE_STEP1_HINT),
        T(S_GUIDE_STEP2_HINT),
        L"",
    };
    WCHAR ext[MAX_PATH], sub3[MAX_PATH + 32];
    extension_dir(ext);
    swprintf(sub3, ARRAYSIZE(sub3), L"\x200E%ls", ext);
    subs[2] = sub3;

    int y = S(88);
    for (int i = 0; i < 3; i++) {
        RECT card = { m, y, W - m, y + S(64) };
        round_rect(dc, &card, S(14), C_PANEL, C_BORDER);
        RECT num = { card.left + S(14), card.top + S(16), card.left + S(46), card.top + S(48) };
        round_rect(dc, &num, S(32), g->connected ? C_GREEN : C_ACCENT, g->connected ? C_GREEN : C_ACCENT);
        WCHAR n[4];
        swprintf(n, 4, L"%d", i + 1);
        draw_text(dc, g->connected ? L"\xE73E" : n, &num, g->connected ? g_font_icon : g_font_bold, RGB(255, 255, 255),
                  DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        int right = card.right - S(14) - ((i == 0 || i == 2) ? S(140) : 0);
        RECT tt = { num.right + S(14), card.top + S(8), right, card.top + S(34) };
        draw_text(dc, titles[i], &tt, g_font_bold, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
        RECT ss = { num.right + S(14), card.top + S(34), right, card.bottom - S(8) };
        draw_text(dc, subs[i], &ss, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_PATH_ELLIPSIS);
        y += S(74);
    }

    /* connection state */
    RECT sb = { m, y + S(4), W - m, y + S(48) };
    round_rect(dc, &sb, S(12), g->connected ? RGB(20, 52, 34) : C_PANEL2, g->connected ? C_GREEN : C_BORDER);
    RECT dot = { sb.left + S(16), (sb.top + sb.bottom) / 2 - S(5), sb.left + S(26), (sb.top + sb.bottom) / 2 + S(5) };
    int blink = (GetTickCount() / 600) & 1;
    COLORREF dc_ = g->connected ? C_GREEN : (blink ? C_ORANGE : RGB(120, 80, 20));
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    SetDCBrushColor(dc, dc_);
    Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
    SelectObject(dc, ob); SelectObject(dc, op);
    RECT stx = { dot.right + S(12), sb.top, sb.right - S(12), sb.bottom };
    draw_text(dc, g->connected ? T(S_GUIDE_CONNECTED) : T(S_GUIDE_WAITING),
              &stx, g_font_bold, g->connected ? C_GREEN : C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
}

static void guide_layout(HWND h)
{
    Guide *g = (Guide *)GetWindowLongPtrW(h, GWLP_USERDATA);
    RECT r;
    GetClientRect(h, &r);
    int W = r.right, H = r.bottom, m = S(24);
    MoveWindow(g->btn[0].hwnd, W - m - S(14) - S(130), S(88) + S(12), S(130), S(40), TRUE);
    MoveWindow(g->btn[1].hwnd, W - m - S(14) - S(130), S(88) + 2 * S(74) + S(12), S(130), S(40), TRUE);
    MoveWindow(g->btn[2].hwnd, m, H - m - S(42), S(110), S(42), TRUE);
    MoveWindow(g->btn[3].hwnd, W - m - S(140), H - m - S(42), S(140), S(42), TRUE);
}

static LRESULT CALLBACK guide_proc(HWND h, UINT msg, WPARAM w, LPARAM l)
{
    Guide *g = (Guide *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CREATE: {
        g = (Guide *)calloc(1, sizeof(Guide));
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)g);
        g->connected = g_ext_connected;
        Button defs[4] = {
            { IDG_OPEN,  G_GLOBE,  T(S_GUIDE_OPEN),  1, NULL, 0 },
            { IDG_COPY,  G_COPY,   T(S_GUIDE_COPY),  0, NULL, 0 },
            { IDG_LATER, G_CANCEL, T(S_DLG_LATER),   0, NULL, 0 },
            { IDG_DONE,  G_CHECK,  T(S_GUIDE_DONE),  1, NULL, 0 },
        };
        for (int i = 0; i < 4; i++) {
            g->btn[i] = defs[i];
            g->btn[i].hwnd = CreateWindowExW(0, L"BUTTON", defs[i].text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                             0, 0, 10, 10, h, (HMENU)(INT_PTR)defs[i].id, g_inst, NULL);
            SetWindowLongPtrW(g->btn[i].hwnd, GWLP_USERDATA, (LONG_PTR)&g->btn[i]);
            SetWindowSubclass(g->btn[i].hwnd, btn_proc, 1, (DWORD_PTR)&g->btn[i]);
        }
        ShowWindow(g->btn[3].hwnd, g->connected ? SW_SHOW : SW_HIDE);
        SetTimer(h, 1, 600, NULL);
        return 0;
    }
    case WM_SIZE:
        guide_layout(h);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT r;
        GetClientRect(h, &r);
        (void)r;
        guide_paint(h, dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_TIMER: {
        if (g_ext_connected && !g->connected) {
            g->connected = 1;
            ShowWindow(g->btn[3].hwnd, SW_SHOW);
            ShowWindow(g->btn[2].hwnd, SW_HIDE);
            MessageBeep(MB_OK);
            SetForegroundWindow(h);
        }
        RECT r;
        GetClientRect(h, &r);
        r.top = r.bottom - S(140);
        InvalidateRect(h, &r, FALSE);
        return 0;
    }
    case WM_DRAWITEM:
        draw_button((DRAWITEMSTRUCT *)l);
        return TRUE;
    case WM_COMMAND: {
        WCHAR ext[MAX_PATH];
        extension_dir(ext);
        switch (LOWORD(w)) {
        case IDG_OPEN:
            copy_text(ext);
            if (!open_chrome_extensions())
                MessageBoxW(h, T(S_GUIDE_NO_CHROME), L"DAO", MB_ICONINFORMATION | MB_DIR);
            break;
        case IDG_COPY:
            copy_text(ext);
            break;
        case IDG_LATER:
        case IDG_DONE:
            DestroyWindow(h);
            break;
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        g_guide = NULL;
        return 0;
    case WM_NCDESTROY:
        free(g);
        break;
    }
    return DefWindowProcW(h, msg, w, l);
}

static void show_chrome_guide(void)
{
    if (g_guide) { SetForegroundWindow(g_guide); return; }
    RECT rc = { 0, 0, S(640), S(470) };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    DWORD ex = WS_EX_DIR | WS_EX_DLGMODALFRAME;
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    RECT mr;
    GetWindowRect(g_main, &mr);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    int x = (mr.left + mr.right - ww) / 2, y = (mr.top + mr.bottom - wh) / 2;
    g_guide = CreateWindowExW(ex, L"DaoGuide", T(S_GUIDE_WINDOW), style, x, y, ww, wh, g_main, NULL, g_inst, NULL);
    if (!g_guide) return;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_guide, 20, &dark, sizeof dark);
    DwmSetWindowAttribute(g_guide, 19, &dark, sizeof dark);
    COLORREF cap = C_BG;
    DwmSetWindowAttribute(g_guide, 35, &cap, sizeof cap);
    ShowWindow(g_guide, SW_SHOW);
}

/* The extension pinged us through dao-host. */
static void on_extension_connected(void)
{
    if (!g_ext_connected) {
        g_ext_connected = 1;
        reg_set_dword(L"ExtensionConnected", 1);
        update_chrome_button();
    }
}

/* ------------------------------------------------------------------ */
/*                              Main window                            */
/* ------------------------------------------------------------------ */
typedef struct { RECT url_box, conn_box, header; } Layout;
static Layout g_lay;

static void layout(void)
{
    RECT r;
    GetClientRect(g_main, &r);
    int W = r.right, H = r.bottom;
    int m = S(18);
    int y = S(14);

    g_lay.header = (RECT){ m, y, W - m, y + S(44) };
    y += S(56);

    /* input row: [url ..............][connections][folder][add] */
    int rowh = S(44);
    int addw = S(150), folw = S(110), connw = S(130), gap = S(10);
    int x = m;
    int urlw = W - 2 * m - addw - folw - connw - 3 * gap;
    if (urlw < S(200)) urlw = S(200);
    g_lay.url_box = (RECT){ x, y, x + urlw, y + rowh };
    int eh = S(22);
    MoveWindow(g_url, x + S(44), y + (rowh - eh) / 2, urlw - S(56), eh, TRUE);
    x += urlw + gap;
    g_lay.conn_box = (RECT){ x, y, x + connw, y + rowh };
    MoveWindow(g_conn, x + connw - S(42), y + (rowh - eh) / 2, S(30), eh, TRUE);
    x += connw + gap;
    MoveWindow(find_btn(IDC_FOLDER)->hwnd, x, y, folw, rowh, TRUE);
    x += folw + gap;
    MoveWindow(find_btn(IDC_ADD)->hwnd, x, y, addw, rowh, TRUE);
    y += rowh + S(14);

    /* toolbar */
    int bh = S(38);
    int ids[] = { IDC_RESUME, IDC_PAUSE, IDC_DELETE, IDC_OPEN, IDC_OPENDIR };
    int bw[]  = { 110, 100, 90, 120, 125 };
    x = m;
    for (int i = 0; i < 5; i++) {
        MoveWindow(find_btn(ids[i])->hwnd, x, y, S(bw[i]), bh, TRUE);
        x += S(bw[i]) + S(8);
    }
    int rx = W - m;
    rx -= S(135); MoveWindow(find_btn(IDC_RESUMEALL)->hwnd, rx, y, S(135), bh, TRUE);
    rx -= S(8) + S(120); MoveWindow(find_btn(IDC_PAUSEALL)->hwnd, rx, y, S(120), bh, TRUE);
    MoveWindow(find_btn(IDC_CHROME)->hwnd, W - m - S(160), g_lay.header.top + S(4), S(160), S(36), TRUE);
    MoveWindow(find_btn(IDC_LANG)->hwnd, W - m - S(160) - S(8) - S(110), g_lay.header.top + S(4), S(110), S(36), TRUE);
    y += bh + S(14);

    int sth = S(34), deth = S(132);
    int lh = H - y - deth - sth - S(14) * 2 + S(6);
    if (lh < S(80)) lh = S(80);
    MoveWindow(g_list, m, y, W - 2 * m, lh, TRUE);
    y += lh + S(14);
    MoveWindow(g_details, m, y, W - 2 * m, deth, TRUE);
    y += deth + S(8);
    MoveWindow(g_status, 0, H - sth, W, sth, TRUE);
    size_columns();
    InvalidateRect(g_main, NULL, TRUE);
}

static void paint_main(HDC dc)
{
    RECT r;
    GetClientRect(g_main, &r);
    fill_rect(dc, &r, C_BG);

    /* header (logical coordinates; mirrored automatically in RTL) */
    RECT h = g_lay.header;
    RECT logo = { h.left, h.top + S(2), h.left + S(40), h.top + S(42) };
    round_rect(dc, &logo, S(12), C_ACCENT, C_ACCENT);
    draw_text(dc, G_DOWN, &logo, g_font_icon, RGB(255, 255, 255), DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    RECT t1 = { logo.right + S(12), h.top - S(2), h.right, h.top + S(26) };
    draw_text(dc, L"DAO", &t1, g_font_title, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    RECT t2 = { logo.right + S(12), h.top + S(24), h.right, h.top + S(44) };
    draw_text(dc, T(S_APP_TAGLINE), &t2, g_font_small, C_MUTED,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    /* URL field */
    int focused = GetFocus() == g_url;
    round_rect(dc, &g_lay.url_box, S(12), C_FIELD, focused ? C_ACCENT : C_BORDER);
    RECT gi = { g_lay.url_box.left + S(14), g_lay.url_box.top, g_lay.url_box.left + S(38), g_lay.url_box.bottom };
    draw_text(dc, G_LINK, &gi, g_font_icon, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_CENTER);

    /* connections field */
    round_rect(dc, &g_lay.conn_box, S(12), C_FIELD, GetFocus() == g_conn ? C_ACCENT : C_BORDER);
    RECT cl = { g_lay.conn_box.left + S(12), g_lay.conn_box.top, g_lay.conn_box.right - S(46), g_lay.conn_box.bottom };
    draw_text(dc, T(S_CONNECTIONS), &cl, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
}

static void on_timer(void)
{
    DWORD now = GetTickCount();
    for (int i = 0; i < g_ndl; i++) {
        Download *d = g_dl[i];
        if (d->status == ST_DOWNLOADING) {
            DWORD dt = now - d->ui_last_tick;
            if (dt >= 400) {
                LONGLONG cur = d->session_bytes;
                LONGLONG inst = (cur - d->ui_last_bytes) * 1000 / (LONGLONG)dt;
                if (inst < 0) inst = 0;
                d->speed = d->speed ? (d->speed * 6 + inst * 4) / 10 : inst;
                d->ui_last_bytes = cur;
                d->ui_last_tick = now;
            }
        } else {
            d->speed = 0;
            d->ui_last_bytes = d->session_bytes;
            d->ui_last_tick = now;
        }
    }
    refresh_all();
}

static void context_menu(int x, int y)
{
    Download *d = selected(NULL);
    if (!d) return;
    HMENU m = CreatePopupMenu();
    int run = dl_is_running(d);
    AppendMenuW(m, MF_STRING | ((run || d->status == ST_DONE) ? MF_GRAYED : 0), IDM_RESUME, T(S_MENU_RESUME));
    AppendMenuW(m, MF_STRING | (run ? 0 : MF_GRAYED), IDM_PAUSE, T(S_MENU_PAUSE));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (d->status == ST_DONE ? 0 : MF_GRAYED), IDM_OPEN, T(S_MENU_OPEN));
    AppendMenuW(m, MF_STRING, IDM_OPENDIR, T(S_MENU_OPENDIR));
    AppendMenuW(m, MF_STRING, IDM_COPYURL, T(S_MENU_COPYURL));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, IDM_REMOVE, T(S_MENU_REMOVE));
    AppendMenuW(m, MF_STRING, IDM_REMOVEFILE, T(S_MENU_REMOVEFILE));
    if (x == -1 && y == -1) {
        POINT p = { S(60), S(60) };
        ClientToScreen(g_list, &p);
        x = p.x; y = p.y;
    }
    TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_DIR, x, y, 0, g_main, NULL);
    DestroyMenu(m);
}

static HFONT make_font(int px, int weight, const WCHAR *face)
{
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

static void create_controls(HWND h)
{
    g_font       = make_font(14, FW_NORMAL, L"Segoe UI");
    g_font_bold  = make_font(14, FW_SEMIBOLD, L"Segoe UI");
    g_font_small = make_font(12, FW_NORMAL, L"Segoe UI");
    g_font_title = make_font(18, FW_BOLD, L"Segoe UI");
    g_font_icon  = make_font(16, FW_NORMAL, L"Segoe MDL2 Assets");
    g_font_icon_big = make_font(48, FW_NORMAL, L"Segoe MDL2 Assets");
    g_br_bg    = CreateSolidBrush(C_BG);
    g_br_field = CreateSolidBrush(C_FIELD);

    g_url = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                            0, 0, 10, 10, h, (HMENU)IDC_URL, g_inst, NULL);
    SendMessageW(g_url, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_url, EM_SETCUEBANNER, TRUE, (LPARAM)T(S_URL_CUE));
    SendMessageW(g_url, EM_SETLIMITTEXT, 4000, 0);
    SetWindowSubclass(g_url, edit_proc, 1, 0);

    WCHAR n[16];
    swprintf(n, 16, L"%d", g_nconn);
    g_conn = CreateWindowExW(0, L"EDIT", n, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER,
                             0, 0, 10, 10, h, (HMENU)IDC_CONN, g_inst, NULL);
    SendMessageW(g_conn, WM_SETFONT, (WPARAM)g_font_bold, TRUE);
    SendMessageW(g_conn, EM_SETLIMITTEXT, 2, 0);

    for (int i = 0; i < NBTN; i++) {
        g_btn[i].id    = g_btn_defs[i].id;
        g_btn[i].glyph = g_btn_defs[i].glyph;
        g_btn[i].text  = T(g_btn_defs[i].text);
        g_btn[i].style = g_btn_defs[i].style;
        g_btn[i].hwnd = CreateWindowExW(0, L"BUTTON", g_btn[i].text,
                                        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                        0, 0, 10, 10, h, (HMENU)(INT_PTR)g_btn[i].id, g_inst, NULL);
        SetWindowLongPtrW(g_btn[i].hwnd, GWLP_USERDATA, (LONG_PTR)&g_btn[i]);
        SetWindowSubclass(g_btn[i].hwnd, btn_proc, 1, (DWORD_PTR)&g_btn[i]);
    }

    create_list();
    g_details = CreateWindowExW(0, L"DaoPanel", L"", WS_CHILD | WS_VISIBLE,
                                0, 0, 10, 10, h, (HMENU)IDC_DETAILS, g_inst, NULL);
    g_status  = CreateWindowExW(0, L"DaoPanel", L"", WS_CHILD | WS_VISIBLE,
                                0, 0, 10, 10, h, (HMENU)IDC_STATUS, g_inst, NULL);
}

/* Ask before stopping running downloads. Returns 1 to proceed. */
static int confirm_quit(const WCHAR *question, const WCHAR *title)
{
    int running = 0;
    for (int i = 0; i < g_ndl; i++) if (dl_is_running(g_dl[i])) running++;
    if (!running) return 1;
    return MessageBoxW(g_main, question, title, MB_YESNO | MB_ICONQUESTION | MB_DIR) == IDYES;
}

static void shutdown_main(void)
{
    KillTimer(g_main, 1);
    RemoveClipboardFormatListener(g_main);
    for (int i = 0; i < g_ndl; i++) dl_pause(g_dl[i]);
    for (int i = 0; i < g_ndl; i++) dl_wait(g_dl[i], 5000);
    for (int i = 0; i < g_ndl; i++) if (g_dl[i]->status != ST_DONE) g_dl[i]->status = ST_PAUSED;
    save_list();
    if (g_guide) DestroyWindow(g_guide);
    DestroyWindow(g_main);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        g_main = h;
        create_controls(h);
        load_list();
        ListView_SetItemCountEx(g_list, g_ndl, 0);
        if (g_ndl) ListView_SetItemState(g_list, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        update_buttons();
        SetTimer(h, 1, 500, NULL);
        AddClipboardFormatListener(h);
        g_ext_connected = reg_get_dword(L"ExtensionConnected", 0) == 1;
        update_chrome_button();
        if (reg_get_dword(L"ShowChromeSetup", 0) == 1 || (!g_ext_connected && !reg_get_dword(L"GuideShown", 0))) {
            reg_set_dword(L"ShowChromeSetup", 0);
            reg_set_dword(L"GuideShown", 1);
            PostMessageW(h, WM_APP + 9, 0, 0);
        }
        {
            WCHAR u[4096];
            if (clipboard_url(u, ARRAYSIZE(u))) SetWindowTextW(g_url, u);
        }
        return 0;

    case WM_SIZE:
        layout();
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mm = (MINMAXINFO *)l;
        mm->ptMinTrackSize.x = S(980);
        mm->ptMinTrackSize.y = S(600);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint_main(dc);
        EndPaint(h, &ps);
        return 0;
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)w;
        SetTextColor(dc, C_TEXT);
        SetBkColor(dc, C_FIELD);
        return (LRESULT)g_br_field;
    }

    case WM_DRAWITEM:
        draw_button((DRAWITEMSTRUCT *)l);
        return TRUE;

    case WM_TIMER:
        on_timer();
        return 0;

    case WM_CLIPBOARDUPDATE: {
        WCHAR u[4096], cur[16];
        GetWindowTextW(g_url, cur, 16);
        if (!cur[0] && clipboard_url(u, ARRAYSIZE(u))) {
            int exists = 0;
            for (int i = 0; i < g_ndl; i++) if (!wcscmp(g_dl[i]->url, u)) exists = 1;
            if (!exists) {
                SetWindowTextW(g_url, u);
                InvalidateRect(h, &g_lay.url_box, FALSE);
                FlashWindow(h, FALSE);
            }
        }
        return 0;
    }

    case WM_APP + 9:
        show_chrome_guide();
        return 0;

    case WM_COPYDATA: {
        COPYDATASTRUCT *cds = (COPYDATASTRUCT *)l;
        if (cds->dwData == 3) { on_extension_connected(); return TRUE; }   /* extension ping */
        if (cds->dwData == 2) on_extension_connected();
        if (cds->dwData == 2 && cds->cbData >= sizeof(WCHAR) && cds->cbData < 512 * 1024) {
            /* from the browser: url \x1F filename \x1F referer \x1F cookies \x1F ua \x1F size */
            size_t n = cds->cbData / sizeof(WCHAR);
            WCHAR *buf = (WCHAR *)calloc(n + 1, sizeof(WCHAR));
            if (buf) {
                memcpy(buf, cds->lpData, n * sizeof(WCHAR));
                WCHAR *f[6] = { 0 };
                WCHAR *p = buf;
                for (int i = 0; i < 6 && p; i++) {
                    f[i] = p;
                    WCHAR *sep = wcschr(p, 0x1F);
                    if (sep) { *sep = 0; p = sep + 1; } else p = NULL;
                }
                if (f[0] && is_url(f[0]))
                    show_add_dialog(f[0], f[1], f[2], f[3], f[4], f[5] ? _wtoi64(f[5]) : -1);
                free(buf);
            }
            return TRUE;
        }
        if (cds->dwData == 1 && cds->cbData >= sizeof(WCHAR) && cds->cbData < 8000) {
            WCHAR u[4096];
            size_t n = cds->cbData / sizeof(WCHAR);
            if (n >= ARRAYSIZE(u)) n = ARRAYSIZE(u) - 1;
            memcpy(u, cds->lpData, n * sizeof(WCHAR));
            u[n] = 0;
            trim(u);
            if (is_url(u)) add_download(u);
        }
        return TRUE;
    }

    case WM_DL_CHANGED: {
        Download *d = (Download *)l;
        int idx = -1;
        for (int i = 0; i < g_ndl; i++) if (g_dl[i] == d) idx = i;
        if (idx < 0) return 0;
        int st = d->status;
        int final = (st == ST_PAUSED || st == ST_DONE || st == ST_ERROR);
        if (final && d->remove_after) {
            dl_wait(d, 5000);
            if (d->remove_after == 2) delete_files(d);
            remove_index(idx);
            return 0;
        }
        if (final) {
            dl_wait(d, 2000);
            save_list();
            if (st == ST_DONE) MessageBeep(MB_OK);
        }
        update_buttons();
        refresh_all();
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(w);
        if ((id == IDC_URL || id == IDC_CONN) && (HIWORD(w) == EN_SETFOCUS || HIWORD(w) == EN_KILLFOCUS)) {
            InvalidateRect(h, &g_lay.url_box, FALSE);
            InvalidateRect(h, &g_lay.conn_box, FALSE);
            return 0;
        }
        Download *d = selected(NULL);
        switch (id) {
        case IDOK:          if (GetFocus() == g_url || GetFocus() == g_conn) add_download(NULL); break;
        case IDC_ADD:       add_download(NULL); break;
        case IDC_FOLDER:    choose_folder(); break;
        case IDC_RESUME:
        case IDM_RESUME:    for_selected(1); break;
        case IDC_PAUSE:
        case IDM_PAUSE:     for_selected(0); break;
        case IDC_DELETE:    delete_selected(1); break;
        case IDM_REMOVE:    delete_selected(0); break;
        case IDM_REMOVEFILE:delete_selected(2); break;
        case IDC_OPEN:
        case IDM_OPEN:      open_file(d); break;
        case IDC_OPENDIR:
        case IDM_OPENDIR:   open_folder(d); break;
        case IDM_COPYURL:   if (d) copy_text(d->url); break;
        case IDC_RESUMEALL: for (int i = 0; i < g_ndl; i++) dl_start(g_dl[i]); update_buttons(); break;
        case IDC_CHROME:    show_chrome_guide(); break;
        case IDC_LANG:
            if (confirm_quit(T(S_LANG_RESTART_ASK), L"DAO")) {
                g_lang = (g_lang == LANG_AR) ? LANG_EN : LANG_AR;
                save_settings();
                g_restart = 1;
                shutdown_main();
            }
            break;
        case IDC_PAUSEALL:  for (int i = 0; i < g_ndl; i++) dl_pause(g_dl[i]); update_buttons(); break;
        }
        return 0;
    }

    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR *)l;
        if (nh->idFrom == IDC_LIST) {
            switch (nh->code) {
            case NM_CUSTOMDRAW:
                return list_custom_draw((NMLVCUSTOMDRAW *)l);
            case LVN_GETDISPINFOW: {
                NMLVDISPINFOW *di = (NMLVDISPINFOW *)l;
                if ((di->item.mask & LVIF_TEXT) && di->item.iItem < g_ndl && di->item.iSubItem == 0)
                    wcsncpy(di->item.pszText, g_dl[di->item.iItem]->name, di->item.cchTextMax - 1);
                return 0;
            }
            case LVN_ITEMCHANGED:
            case LVN_ODSTATECHANGED:
                update_buttons();
                InvalidateRect(g_details, NULL, FALSE);
                return 0;
            case NM_DBLCLK: {
                Download *d = selected(NULL);
                if (d) {
                    if (d->status == ST_DONE) open_file(d);
                    else if (!dl_is_running(d)) dl_start(d);
                    update_buttons();
                }
                return 0;
            }
            case NM_RCLICK: {
                POINT p;
                GetCursorPos(&p);
                context_menu(p.x, p.y);
                return 0;
            }
            case LVN_KEYDOWN: {
                NMLVKEYDOWN *k = (NMLVKEYDOWN *)l;
                if (k->wVKey == VK_DELETE) delete_selected(GetKeyState(VK_SHIFT) < 0 ? 2 : 1);
                else if (k->wVKey == VK_RETURN) { Download *d = selected(NULL); if (d) open_file(d); }
                else if (k->wVKey == VK_SPACE) {
                    Download *d = selected(NULL);
                    if (d) for_selected(!dl_is_running(d));
                }
                else if (k->wVKey == 'A' && GetKeyState(VK_CONTROL) < 0) {
                    ListView_SetItemState(g_list, -1, LVIS_SELECTED, LVIS_SELECTED);
                }
                else if (k->wVKey == 'V' && GetKeyState(VK_CONTROL) < 0) {
                    WCHAR u[4096];
                    if (clipboard_url(u, ARRAYSIZE(u))) { SetWindowTextW(g_url, u); SetFocus(g_url); }
                }
                return 0;
            }
            }
        }
        break;
    }

    case WM_CLOSE:
        if (confirm_quit(T(S_EXIT_ASK), T(S_EXIT_TITLE))) shutdown_main();
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

/* ------------------------------------------------------------------ */
int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)prev;
    g_inst = inst;

    /* single instance: forward the URL to the running copy */
    HANDLE mtx = CreateMutexW(NULL, TRUE, L"DAO_DownloadManager_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND other = FindWindowW(L"DaoMainWindow", NULL);
        if (other) {
            if (cmd && cmd[0]) {
                COPYDATASTRUCT cds = { 1, (DWORD)((wcslen(cmd) + 1) * sizeof(WCHAR)), cmd };
                SendMessageW(other, WM_COPYDATA, 0, (LPARAM)&cds);
            }
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        return 0;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    HDC sdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(sdc, LOGPIXELSY);
    ReleaseDC(NULL, sdc);

    /* settings live in %APPDATA%\DAO */
    PWSTR appdata = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_RoamingAppData, 0, NULL, &appdata)) && appdata) {
        swprintf(g_cfg_dir, MAX_PATH, L"%ls\\DAO", appdata);
        CoTaskMemFree(appdata);
    } else {
        GetCurrentDirectoryW(MAX_PATH, g_cfg_dir);
    }
    CreateDirectoryW(g_cfg_dir, NULL);
    load_settings();
    engine_init();
    register_native_host();

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = panel_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"DaoPanel";
    RegisterClassExW(&wc);

    wc.lpfnWndProc = add_dlg_proc;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"DaoAddDialog";
    RegisterClassExW(&wc);

    wc.lpfnWndProc = guide_proc;
    wc.lpszClassName = L"DaoGuide";
    RegisterClassExW(&wc);

    wc.lpfnWndProc = wnd_proc;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, 0);
    wc.lpszClassName = L"DaoMainWindow";
    RegisterClassExW(&wc);

    HWND h = CreateWindowExW(WS_EX_DIR | WS_EX_APPWINDOW, L"DaoMainWindow",
                             T(S_APP_TITLE),
                             WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, S(1180), S(760), NULL, NULL, inst, NULL);
    if (!h) return 1;

    BOOL dark = TRUE;
    DwmSetWindowAttribute(h, 20, &dark, sizeof(dark));   /* DWMWA_USE_IMMERSIVE_DARK_MODE */
    DwmSetWindowAttribute(h, 19, &dark, sizeof(dark));   /* pre-20H1 Windows 10 */
    COLORREF cap = C_BG;
    DwmSetWindowAttribute(h, 35, &cap, sizeof(cap));     /* DWMWA_CAPTION_COLOR (Windows 11) */

    ShowWindow(h, show);
    UpdateWindow(h);

    if (cmd && cmd[0]) {
        WCHAR u[4096];
        wcsncpy(u, cmd, ARRAYSIZE(u) - 1);
        u[ARRAYSIZE(u) - 1] = 0;
        trim(u);
        if (is_url(u)) add_download(u);
    }

    MSG msgq;
    while (GetMessageW(&msgq, NULL, 0, 0)) {
        HWND root = msgq.hwnd ? GetAncestor(msgq.hwnd, GA_ROOT) : NULL;
        if (root && root != h) {
            WCHAR cls[32];
            GetClassNameW(root, cls, 32);
            if (!wcscmp(cls, L"DaoAddDialog") && IsDialogMessageW(root, &msgq)) continue;
        }
        if (root == h && msgq.message == WM_KEYDOWN && msgq.wParam == 'V' && GetKeyState(VK_CONTROL) < 0 &&
            GetFocus() != g_url && GetFocus() != g_conn) {
            WCHAR u[4096];
            if (clipboard_url(u, ARRAYSIZE(u))) { SetWindowTextW(g_url, u); SetFocus(g_url); continue; }
        }
        if (!IsDialogMessageW(h, &msgq)) {
            TranslateMessage(&msgq);
            DispatchMessageW(&msgq);
        }
    }

    for (int i = 0; i < g_ndl; i++) dl_free(g_dl[i]);
    engine_shutdown();
    CoUninitialize();
    ReleaseMutex(mtx);
    CloseHandle(mtx);

    if (g_restart) {
        WCHAR self[MAX_PATH];
        STARTUPINFOW si;
        memset(&si, 0, sizeof(si));
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi;
        GetModuleFileNameW(NULL, self, MAX_PATH);
        if (CreateProcessW(self, NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }
    return 0;
}
