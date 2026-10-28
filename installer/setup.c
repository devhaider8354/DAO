/*
 * setup.c - DAO installer (DAO-Setup.exe)
 *
 *   - installs into Program Files\DAO (all files are embedded as resources)
 *   - Start menu and desktop shortcuts
 *   - registers the native messaging host for Chrome / Edge / Brave
 *   - if STORE_EXTENSION_ID is set, registers the Chrome Web Store extension
 *     so Chrome offers to enable it on next start
 *   - adds DAO to "Installed apps" with a full uninstaller
 *
 *   DAO-Setup.exe               install
 *   uninstall.exe /uninstall    uninstall
 *
 * The UI follows the Windows display language (English or Arabic).
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <objbase.h>
#include <stdio.h>
#include <wchar.h>
#include "payload.h"

#define APP_VERSION      L"2.3"
#define APP_NAME         L"DAO"
#define PUBLISHER        L"FLOW POINT"
#define HOST_NAME        L"com.flowpoint.dao"
#define DEV_EXTENSION_ID L"eojnoldnafpphjnhlafncmodihfhciam"
/* Chrome Web Store ID of the extension; when set, Chrome offers to enable it after install. */
#ifndef STORE_EXTENSION_ID
#define STORE_EXTENSION_ID L""
#endif
#define UNINSTALL_KEY    L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\DAO"
#define USER_KEY         L"Software\\FlowPoint\\DAO"

#define C_BG      RGB(16, 18, 24)
#define C_PANEL   RGB(23, 26, 34)
#define C_PANEL2  RGB(27, 31, 40)
#define C_FIELD   RGB(33, 37, 48)
#define C_BORDER  RGB(46, 52, 66)
#define C_TEXT    RGB(232, 235, 242)
#define C_MUTED   RGB(138, 146, 166)
#define C_ACCENT  RGB(59, 130, 246)
#define C_ACCENT2 RGB(96, 156, 250)
#define C_GREEN   RGB(34, 197, 94)
#define C_RED     RGB(239, 68, 68)
#define C_BTN     RGB(36, 41, 53)
#define C_BTN_HI  RGB(48, 55, 71)

enum { PAGE_WELCOME, PAGE_INSTALLING, PAGE_DONE, PAGE_ERROR };
enum { ID_INSTALL = 100, ID_CANCEL, ID_FINISH, ID_CHK_DESKTOP, ID_CHK_RUN, ID_CHANGE };
#define WM_PROGRESS (WM_APP + 1)
#define WM_DONE     (WM_APP + 2)

static HINSTANCE g_inst;
static HWND g_wnd;
static int  g_dpi = 96, g_page = PAGE_WELCOME, g_progress = 0;
static int  g_desktop = 1, g_run = 1;
static WCHAR g_dir[MAX_PATH], g_status[256], g_error[512];
static HFONT g_font, g_font_bold, g_font_small, g_font_title, g_font_icon;
static HICON g_icon;
static int   g_ar;            /* Arabic UI (RTL) */

static const WCHAR *tr(const WCHAR *en, const WCHAR *ar) { return g_ar ? ar : en; }
#define MB_DIR (g_ar ? (MB_RTLREADING | MB_RIGHT) : 0)

typedef struct { int id; const WCHAR *en, *ar; int style; const WCHAR *text; HWND hwnd; int hover; } Btn;
static Btn g_btn[] = {
    { ID_INSTALL,     L"Install",                  L"تثبيت",                   1 },
    { ID_CANCEL,      L"Cancel",                   L"إلغاء",                   0 },
    { ID_FINISH,      L"Finish",                   L"إنهاء",                   1 },
    { ID_CHK_DESKTOP, L"Create a desktop shortcut", L"اختصار على سطح المكتب",  2 },
    { ID_CHK_RUN,     L"Launch DAO when finished", L"تشغيل DAO بعد التثبيت",   2 },
    { ID_CHANGE,      L"Change",                   L"تغيير",                   0 },
};
#define NBTN ((int)(sizeof(g_btn) / sizeof(g_btn[0])))

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static Btn *btn(int id) { for (int i = 0; i < NBTN; i++) if (g_btn[i].id == id) return &g_btn[i]; return NULL; }

/* ------------------------------------------------------------------ */
/*                               Drawing                               */
/* ------------------------------------------------------------------ */
static void fill(HDC dc, const RECT *r, COLORREF c)
{
    SetDCBrushColor(dc, c);
    FillRect(dc, r, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void rrect(HDC dc, const RECT *r, int rad, COLORREF f, COLORREF b)
{
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ op = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCBrushColor(dc, f);
    SetDCPenColor(dc, b);
    RoundRect(dc, r->left, r->top, r->right, r->bottom, rad, rad);
    SelectObject(dc, ob);
    SelectObject(dc, op);
}

static void text(HDC dc, const WCHAR *s, RECT *r, HFONT f, COLORREF c, UINT fl)
{
    HGDIOBJ of = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, r, fl | DT_NOPREFIX);
    SelectObject(dc, of);
}

static void paint(HDC dc)
{
    RECT r;
    GetClientRect(g_wnd, &r);
    int W = r.right, H = r.bottom, m = S(28);
    fill(dc, &r, C_BG);

    /* top banner */
    RECT top = { 0, 0, W, S(120) };
    fill(dc, &top, C_PANEL);
    RECT line = { 0, S(120), W, S(121) };
    fill(dc, &line, C_BORDER);
    if (g_icon) DrawIconEx(dc, m, S(28), g_icon, S(64), S(64), 0, NULL, DI_NORMAL);
    RECT t1 = { m + S(82), S(30), W - m, S(64) };
    text(dc, tr(L"DAO Download Manager", L"DAO  مدير التحميل"), &t1, g_font_title, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
    RECT t2 = { m + S(82), S(64), W - m, S(90) };
    text(dc, tr(L"Version " APP_VERSION L"  \x2022  " PUBLISHER, L"الإصدار " APP_VERSION L"  \x2022  " PUBLISHER), &t2, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT);

    if (g_page == PAGE_WELCOME) {
        RECT a = { m, S(140), W - m, S(166) };
        text(dc, tr(L"Faster multi-connection downloads, resume support and Chrome integration.",
                    L"تحميل أسرع بعدة اتصالات، استكمال التحميل، وربط مباشر مع كروم."), &a, g_font, C_TEXT,
             DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        RECT l = { m, S(180), W - m, S(200) };
        text(dc, tr(L"Install location", L"مكان التثبيت"), &l, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        RECT box = { m, S(204), W - m - S(110), S(244) };
        rrect(dc, &box, S(12), C_FIELD, C_BORDER);
        RECT bt = { box.left + S(14), box.top, box.right - S(14), box.bottom };
        WCHAR p[MAX_PATH + 2];
        swprintf(p, ARRAYSIZE(p), L"\x200E%ls", g_dir);
        text(dc, p, &bt, g_font, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_PATH_ELLIPSIS);
    } else if (g_page == PAGE_INSTALLING) {
        RECT a = { m, S(160), W - m, S(190) };
        text(dc, tr(L"Installing...", L"جارٍ التثبيت..."), &a, g_font_bold, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        RECT bar = { m, S(204), W - m, S(220) };
        rrect(dc, &bar, S(16), C_FIELD, C_FIELD);
        RECT fl = bar;
        fl.right = bar.left + (bar.right - bar.left) * g_progress / 100;
        if (fl.right - fl.left > S(16)) rrect(dc, &fl, S(16), C_ACCENT, C_ACCENT);
        RECT s = { m, S(230), W - m, S(254) };
        text(dc, g_status, &s, g_font_small, C_MUTED, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS);
    } else if (g_page == PAGE_DONE) {
        RECT a = { m, S(142), W - m, S(176) };
        text(dc, tr(L"Installation complete \x2713", L"تم التثبيت بنجاح \x2713"), &a, g_font_title, C_GREEN, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        RECT card = { m, S(186), W - m, S(290) };
        rrect(dc, &card, S(14), C_PANEL, C_BORDER);
        RECT c1 = { card.left + S(16), card.top + S(10), card.right - S(16), card.top + S(36) };
        text(dc, tr(L"Chrome integration", L"الربط مع كروم"), &c1, g_font_bold, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        RECT c2 = { card.left + S(16), card.top + S(38), card.right - S(16), card.bottom - S(8) };
        text(dc, STORE_EXTENSION_ID[0]
                 ? tr(L"Open Chrome - it will show \"New extension added\". Click \"Enable extension\" and you are done.",
                      L"افتح كروم وسيظهر إشعار \"تمت إضافة إضافة جديدة\" - اضغط \"تفعيل الإضافة\" وانتهى.")
                 : tr(L"When DAO starts it opens a short step-by-step guide to connect Chrome (one time, under a minute).",
                      L"عند تشغيل DAO لأول مرة سيفتح دليل الربط مع كروم خطوة بخطوة (مرة واحدة، أقل من دقيقة)."),
             &c2, g_font, C_MUTED, DT_WORDBREAK | DT_LEFT);
    } else if (g_page == PAGE_ERROR) {
        RECT a = { m, S(142), W - m, S(176) };
        text(dc, tr(L"Installation failed", L"فشل التثبيت"), &a, g_font_title, C_RED, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        RECT b = { m, S(186), W - m, S(290) };
        text(dc, g_error, &b, g_font, C_TEXT, DT_WORDBREAK | DT_LEFT);
    }
    (void)H;
}

static void draw_btn(DRAWITEMSTRUCT *di)
{
    Btn *b = btn((int)di->CtlID);
    HDC dc = di->hDC;
    RECT r = di->rcItem;
    int pressed = (di->itemState & ODS_SELECTED) != 0;
    fill(dc, &r, C_BG);
    if (b->style == 2) {   /* checkbox */
        int checked = b->id == ID_CHK_DESKTOP ? g_desktop : g_run;
        int s = S(20);
        RECT box = { r.left, (r.top + r.bottom - s) / 2, r.left + s, (r.top + r.bottom + s) / 2 };
        rrect(dc, &box, S(6), checked ? C_ACCENT : C_FIELD, checked ? C_ACCENT : (b->hover ? C_ACCENT2 : C_BORDER));
        if (checked) text(dc, L"\xE73E", &box, g_font_icon, RGB(255, 255, 255), DT_SINGLELINE | DT_VCENTER | DT_CENTER);
        RECT t = { box.right + S(10), r.top, r.right, r.bottom };
        text(dc, b->text, &t, g_font, C_TEXT, DT_SINGLELINE | DT_VCENTER | DT_LEFT);
        return;
    }
    COLORREF f, bd, fg;
    if (b->style == 1) { f = pressed ? C_ACCENT : (b->hover ? C_ACCENT2 : C_ACCENT); bd = f; fg = RGB(255, 255, 255); }
    else { f = pressed ? C_FIELD : (b->hover ? C_BTN_HI : C_BTN); bd = C_BORDER; fg = C_TEXT; }
    rrect(dc, &r, S(10), f, bd);
    text(dc, b->text, &r, g_font_bold, fg, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
}

static LRESULT CALLBACK btn_sub(HWND h, UINT m, WPARAM w, LPARAM l)
{
    Btn *b = (Btn *)GetWindowLongPtrW(h, GWLP_USERDATA);
    WNDPROC old = (WNDPROC)GetPropW(h, L"old");
    if (m == WM_MOUSEMOVE && !b->hover) {
        TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
        TrackMouseEvent(&t);
        b->hover = 1;
        InvalidateRect(h, NULL, FALSE);
    } else if (m == WM_MOUSELEAVE) {
        b->hover = 0;
        InvalidateRect(h, NULL, FALSE);
    } else if (m == WM_ERASEBKGND) {
        return 1;
    }
    return CallWindowProcW(old, h, m, w, l);
}

static void show_page(int p)
{
    g_page = p;
    ShowWindow(btn(ID_INSTALL)->hwnd,     p == PAGE_WELCOME);
    ShowWindow(btn(ID_CANCEL)->hwnd,      p == PAGE_WELCOME);
    ShowWindow(btn(ID_CHANGE)->hwnd,      p == PAGE_WELCOME);
    ShowWindow(btn(ID_CHK_DESKTOP)->hwnd, p == PAGE_WELCOME);
    ShowWindow(btn(ID_CHK_RUN)->hwnd,     p == PAGE_WELCOME || p == PAGE_DONE);
    ShowWindow(btn(ID_FINISH)->hwnd,      p == PAGE_DONE || p == PAGE_ERROR);
    InvalidateRect(g_wnd, NULL, TRUE);
}

/* ------------------------------------------------------------------ */
/*                               Install                               */
/* ------------------------------------------------------------------ */
static void progress(int pct, const WCHAR *msg)
{
    WCHAR *copy = _wcsdup(msg);
    PostMessageW(g_wnd, WM_PROGRESS, pct, (LPARAM)copy);
}

static int mkdirs(const WCHAR *path)
{
    WCHAR tmp[MAX_PATH];
    wcsncpy(tmp, path, MAX_PATH - 1);
    tmp[MAX_PATH - 1] = 0;
    for (WCHAR *p = tmp + 3; *p; p++) {
        if (*p == L'\\') { *p = 0; CreateDirectoryW(tmp, NULL); *p = L'\\'; }
    }
    return CreateDirectoryW(tmp, NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
}

static int extract(const PayloadFile *pf)
{
    HRSRC r = FindResourceW(g_inst, MAKEINTRESOURCEW(pf->id), (LPCWSTR)RT_RCDATA);
    if (!r) return 0;
    HGLOBAL g = LoadResource(g_inst, r);
    DWORD size = SizeofResource(g_inst, r);
    const void *data = LockResource(g);
    if (!data) return 0;

    WCHAR full[MAX_PATH * 2];
    swprintf(full, ARRAYSIZE(full), L"%ls\\%ls", g_dir, pf->path);
    WCHAR dir[MAX_PATH * 2];
    wcscpy(dir, full);
    WCHAR *s = wcsrchr(dir, L'\\');
    if (s) { *s = 0; mkdirs(dir); }

    for (int attempt = 0; attempt < 20; attempt++) {
        HANDLE f = CreateFileW(full, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            BOOL ok = WriteFile(f, data, size, &w, NULL) && w == size;
            CloseHandle(f);
            return ok;
        }
        Sleep(250);   /* file may still be in use */
    }
    return 0;
}

static void reg_str(HKEY root, const WCHAR *key, const WCHAR *name, const WCHAR *val)
{
    HKEY k;
    if (RegCreateKeyExW(root, key, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_SZ, (const BYTE *)val, (DWORD)((wcslen(val) + 1) * sizeof(WCHAR)));
        RegCloseKey(k);
    }
}

static void reg_dword(HKEY root, const WCHAR *key, const WCHAR *name, DWORD v)
{
    HKEY k;
    if (RegCreateKeyExW(root, key, 0, NULL, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE *)&v, sizeof v);
        RegCloseKey(k);
    }
}

static const WCHAR *g_host_keys[] = {
    L"Software\\Google\\Chrome\\NativeMessagingHosts\\" HOST_NAME,
    L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\" HOST_NAME,
    L"Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts\\" HOST_NAME,
    L"Software\\Chromium\\NativeMessagingHosts\\" HOST_NAME,
};

static int write_host_manifest(void)
{
    WCHAR host[MAX_PATH], esc[MAX_PATH * 2], json[MAX_PATH], origins[512], content[4096];
    swprintf(host, MAX_PATH, L"%ls\\dao-host.exe", g_dir);
    size_t j = 0;
    for (size_t i = 0; host[i] && j < ARRAYSIZE(esc) - 2; i++) {
        if (host[i] == L'\\' || host[i] == L'"') esc[j++] = L'\\';
        esc[j++] = host[i];
    }
    esc[j] = 0;
    if (STORE_EXTENSION_ID[0])
        swprintf(origins, 512, L"\"chrome-extension://%ls/\", \"chrome-extension://%ls/\"", DEV_EXTENSION_ID, STORE_EXTENSION_ID);
    else
        swprintf(origins, 512, L"\"chrome-extension://%ls/\"", DEV_EXTENSION_ID);
    swprintf(content, ARRAYSIZE(content),
             L"{\n  \"name\": \"" HOST_NAME L"\",\n  \"description\": \"DAO Download Manager\",\n"
             L"  \"path\": \"%ls\",\n  \"type\": \"stdio\",\n  \"allowed_origins\": [%ls]\n}\n", esc, origins);

    swprintf(json, MAX_PATH, L"%ls\\" HOST_NAME L".json", g_dir);
    char utf8[8192];
    int n = WideCharToMultiByte(CP_UTF8, 0, content, -1, utf8, sizeof utf8, NULL, NULL);
    HANDLE f = CreateFileW(json, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD w;
    WriteFile(f, utf8, n > 0 ? n - 1 : 0, &w, NULL);
    CloseHandle(f);

    for (int i = 0; i < 4; i++) reg_str(HKEY_LOCAL_MACHINE, g_host_keys[i], NULL, json);
    return 1;
}

static int make_shortcut(const WCHAR *lnk, const WCHAR *target, const WCHAR *desc)
{
    IShellLinkW *sl = NULL;
    IPersistFile *pf = NULL;
    int ok = 0;
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&sl))) return 0;
    IShellLinkW_SetPath(sl, target);
    IShellLinkW_SetWorkingDirectory(sl, g_dir);
    IShellLinkW_SetDescription(sl, desc);
    IShellLinkW_SetIconLocation(sl, target, 0);
    if (SUCCEEDED(IShellLinkW_QueryInterface(sl, &IID_IPersistFile, (void **)&pf))) {
        ok = SUCCEEDED(IPersistFile_Save(pf, lnk, TRUE));
        IPersistFile_Release(pf);
    }
    IShellLinkW_Release(sl);
    return ok;
}

static void shortcut_paths(WCHAR *start_lnk, WCHAR *desk_lnk)
{
    PWSTR p = NULL;
    start_lnk[0] = desk_lnk[0] = 0;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_CommonPrograms, 0, NULL, &p))) {
        swprintf(start_lnk, MAX_PATH, L"%ls\\DAO.lnk", p);
        CoTaskMemFree(p);
    }
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Desktop, 0, NULL, &p))) {
        swprintf(desk_lnk, MAX_PATH, L"%ls\\DAO.lnk", p);
        CoTaskMemFree(p);
    }
}

/* Close a running DAO so its files can be replaced. */
static int close_running_app(void)
{
    HWND w = FindWindowW(L"DaoMainWindow", NULL);
    if (!w) return 1;
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    HANDLE ph = OpenProcess(SYNCHRONIZE, FALSE, pid);
    PostMessageW(w, WM_CLOSE, 0, 0);
    DWORD r = ph ? WaitForSingleObject(ph, 20000) : WAIT_TIMEOUT;
    if (ph) CloseHandle(ph);
    return r == WAIT_OBJECT_0 || !FindWindowW(L"DaoMainWindow", NULL);
}

static DWORD dir_size_kb(void)
{
    DWORD total = 0;
    for (int i = 0; i < PAYLOAD_COUNT; i++) {
        HRSRC r = FindResourceW(g_inst, MAKEINTRESOURCEW(g_payload[i].id), (LPCWSTR)RT_RCDATA);
        if (r) total += SizeofResource(g_inst, r);
    }
    return total / 1024;
}

static DWORD WINAPI install_thread(LPVOID unused)
{
    (void)unused;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    WCHAR buf[MAX_PATH * 2];

    progress(3, tr(L"Closing DAO...", L"التأكد من إغلاق DAO..."));
    if (!close_running_app()) {
        wcscpy(g_error, tr(L"DAO is running with active downloads. Close it (or pause the downloads) and run setup again.",
                           L"برنامج DAO يعمل ويوجد تحميل نشط. أغلقه (أو أوقف التحميلات) ثم أعد التثبيت."));
        PostMessageW(g_wnd, WM_DONE, 0, 0);
        return 0;
    }

    if (!mkdirs(g_dir)) {
        swprintf(g_error, 512, tr(L"Could not create the install folder:\n%ls", L"تعذّر إنشاء مجلد التثبيت:\n%ls"), g_dir);
        PostMessageW(g_wnd, WM_DONE, 0, 0);
        return 0;
    }

    for (int i = 0; i < PAYLOAD_COUNT; i++) {
        swprintf(buf, ARRAYSIZE(buf), tr(L"Copying %ls", L"نسخ %ls"), g_payload[i].path);
        progress(5 + 70 * i / PAYLOAD_COUNT, buf);
        if (!extract(&g_payload[i])) {
            swprintf(g_error, 512, tr(L"Could not write file:\n%ls\\%ls", L"تعذّر كتابة الملف:\n%ls\\%ls"), g_dir, g_payload[i].path);
            PostMessageW(g_wnd, WM_DONE, 0, 0);
            return 0;
        }
    }

    /* the installer doubles as the uninstaller */
    progress(78, tr(L"Preparing uninstaller...", L"تجهيز أداة الإزالة..."));
    WCHAR self[MAX_PATH], unin[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    swprintf(unin, MAX_PATH, L"%ls\\uninstall.exe", g_dir);
    if (_wcsicmp(self, unin) != 0) CopyFileW(self, unin, FALSE);

    progress(82, tr(L"Registering browser integration (Chrome, Edge, Brave)...", L"ربط المتصفحات (كروم، إيدج، بريف)..."));
    write_host_manifest();
    if (STORE_EXTENSION_ID[0]) {
        /* Web Store extension: Chrome prompts the user to enable it */
        swprintf(buf, ARRAYSIZE(buf), L"Software\\Wow6432Node\\Google\\Chrome\\Extensions\\%ls", STORE_EXTENSION_ID);
        reg_str(HKEY_LOCAL_MACHINE, buf, L"update_url", L"https://clients2.google.com/service/update2/crx");
        swprintf(buf, ARRAYSIZE(buf), L"Software\\Wow6432Node\\Microsoft\\Edge\\Extensions\\%ls", STORE_EXTENSION_ID);
        reg_str(HKEY_LOCAL_MACHINE, buf, L"update_url", L"https://clients2.google.com/service/update2/crx");
    }
    reg_dword(HKEY_CURRENT_USER, USER_KEY, L"ShowChromeSetup", 1);

    progress(88, tr(L"Creating shortcuts...", L"إنشاء الاختصارات..."));
    WCHAR exe[MAX_PATH], start_lnk[MAX_PATH], desk_lnk[MAX_PATH];
    swprintf(exe, MAX_PATH, L"%ls\\dao.exe", g_dir);
    shortcut_paths(start_lnk, desk_lnk);
    if (start_lnk[0]) make_shortcut(start_lnk, exe, L"DAO Download Manager");
    if (g_desktop && desk_lnk[0]) make_shortcut(desk_lnk, exe, L"DAO Download Manager");

    progress(94, tr(L"Registering with Windows...", L"تسجيل البرنامج في قائمة التطبيقات..."));
    reg_str(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"DisplayName", L"DAO Download Manager");
    reg_str(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"DisplayVersion", APP_VERSION);
    reg_str(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"Publisher", PUBLISHER);
    reg_str(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"InstallLocation", g_dir);
    reg_str(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"DisplayIcon", exe);
    swprintf(buf, ARRAYSIZE(buf), L"\"%ls\" /uninstall", unin);
    reg_str(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"UninstallString", buf);
    reg_str(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"URLInfoAbout", L"https://flowp.uk");
    reg_dword(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"EstimatedSize", dir_size_kb());
    reg_dword(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"NoModify", 1);
    reg_dword(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"NoRepair", 1);

    progress(100, tr(L"Done", L"تم"));
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, NULL, NULL);
    CoUninitialize();
    PostMessageW(g_wnd, WM_DONE, 1, 0);
    return 0;
}

/* Launch DAO un-elevated by going through explorer.exe. */
static void run_app_unelevated(void)
{
    WCHAR exe[MAX_PATH + 4];
    swprintf(exe, ARRAYSIZE(exe), L"\"%ls\\dao.exe\"", g_dir);
    ShellExecuteW(NULL, L"open", L"explorer.exe", exe, NULL, SW_SHOWNORMAL);
}

/* ------------------------------------------------------------------ */
/*                              Uninstall                              */
/* ------------------------------------------------------------------ */
/* Only uninstall from the folder Windows has on record, never from a copy elsewhere. */
static int is_registered_install_dir(const WCHAR *dir)
{
    WCHAR loc[MAX_PATH];
    DWORD sz = sizeof(loc);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, L"InstallLocation", RRF_RT_REG_SZ,
                     NULL, loc, &sz) != ERROR_SUCCESS)
        return 0;
    return _wcsicmp(loc, dir) == 0;
}

static int uninstall(void)
{
    WCHAR self[MAX_PATH], buf[MAX_PATH * 2];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wcscpy(g_dir, self);
    WCHAR *s = wcsrchr(g_dir, L'\\');
    if (s) *s = 0;

    if (!is_registered_install_dir(g_dir)) {
        MessageBoxW(NULL, tr(L"This copy of the uninstaller is not inside the DAO install folder.\n"
                             L"Uninstall DAO from Settings > Apps instead.",
                             L"أداة الإزالة هذه ليست داخل مجلد تثبيت DAO.\n"
                             L"قم بإزالة البرنامج من الإعدادات > التطبيقات."),
                    tr(L"Uninstall DAO", L"إزالة DAO"), MB_ICONWARNING | MB_DIR);
        return 1;
    }

    if (MessageBoxW(NULL, tr(L"Remove DAO from this computer?\n\n(Your download list and downloaded files are kept.)",
                             L"هل تريد إزالة DAO من الجهاز؟\n\n(لن تُحذف قائمة التحميلات ولا الملفات التي حمّلتها)"),
                    tr(L"Uninstall DAO", L"إزالة DAO"), MB_YESNO | MB_ICONQUESTION | MB_DIR) != IDYES)
        return 0;

    if (!close_running_app()) {
        MessageBoxW(NULL, tr(L"Please close DAO first and try again.", L"أغلق برنامج DAO أولاً ثم أعد المحاولة."),
                    tr(L"Uninstall DAO", L"إزالة DAO"), MB_ICONWARNING | MB_DIR);
        return 1;
    }

    for (int i = 0; i < PAYLOAD_COUNT; i++) {
        swprintf(buf, ARRAYSIZE(buf), L"%ls\\%ls", g_dir, g_payload[i].path);
        DeleteFileW(buf);
    }
    swprintf(buf, ARRAYSIZE(buf), L"%ls\\" HOST_NAME L".json", g_dir);
    DeleteFileW(buf);

    for (int i = 0; i < 4; i++) {
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, g_host_keys[i], KEY_WOW64_64KEY, 0);
        RegDeleteKeyW(HKEY_CURRENT_USER, g_host_keys[i]);
    }
    if (STORE_EXTENSION_ID[0]) {
        swprintf(buf, ARRAYSIZE(buf), L"Software\\Wow6432Node\\Google\\Chrome\\Extensions\\%ls", STORE_EXTENSION_ID);
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, buf, KEY_WOW64_64KEY, 0);
        swprintf(buf, ARRAYSIZE(buf), L"Software\\Wow6432Node\\Microsoft\\Edge\\Extensions\\%ls", STORE_EXTENSION_ID);
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, buf, KEY_WOW64_64KEY, 0);
    }
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, KEY_WOW64_64KEY, 0);
    RegDeleteKeyW(HKEY_CURRENT_USER, USER_KEY);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    WCHAR start_lnk[MAX_PATH], desk_lnk[MAX_PATH];
    shortcut_paths(start_lnk, desk_lnk);
    if (start_lnk[0]) DeleteFileW(start_lnk);
    if (desk_lnk[0]) DeleteFileW(desk_lnk);
    CoUninitialize();

    /* remove the now-empty folders; only files we installed were deleted above */
    static const WCHAR *dirs[] = {
        L"chrome-extension\\_locales\\en", L"chrome-extension\\_locales\\ar", L"chrome-extension\\_locales",
        L"chrome-extension\\icons", L"chrome-extension",
    };
    for (size_t i = 0; i < ARRAYSIZE(dirs); i++) {
        swprintf(buf, ARRAYSIZE(buf), L"%ls\\%ls", g_dir, dirs[i]);
        RemoveDirectoryW(buf);
    }

    /* delete this uninstaller and the (empty) install folder after we exit */
    WCHAR cmd[MAX_PATH * 3];
    swprintf(cmd, ARRAYSIZE(cmd), L"cmd.exe /c ping 127.0.0.1 -n 3 > nul & del /f /q \"%ls\" & rmdir \"%ls\"", self, g_dir);
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi;
    WCHAR sysdir[MAX_PATH];
    GetSystemDirectoryW(sysdir, MAX_PATH);
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, sysdir, &si, &pi)) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    MessageBoxW(NULL, tr(L"DAO has been removed.", L"تمت إزالة DAO."), tr(L"Uninstall DAO", L"إزالة DAO"),
                MB_ICONINFORMATION | MB_DIR);
    return 0;
}

/* ------------------------------------------------------------------ */
/*                                Window                               */
/* ------------------------------------------------------------------ */
static void layout(void)
{
    RECT r;
    GetClientRect(g_wnd, &r);
    int W = r.right, H = r.bottom, m = S(28);
    int bh = S(44), by = H - m - bh;
    MoveWindow(btn(ID_CHANGE)->hwnd, W - m - S(100), S(204), S(100), S(40), TRUE);
    MoveWindow(btn(ID_CHK_DESKTOP)->hwnd, m, S(258), S(260), S(30), TRUE);
    MoveWindow(btn(ID_CHK_RUN)->hwnd, m, g_page == PAGE_DONE ? S(300) : S(290), S(260), S(30), TRUE);
    MoveWindow(btn(ID_INSTALL)->hwnd, W - m - S(160), by, S(160), bh, TRUE);
    MoveWindow(btn(ID_FINISH)->hwnd, W - m - S(160), by, S(160), bh, TRUE);
    MoveWindow(btn(ID_CANCEL)->hwnd, m, by, S(110), bh, TRUE);
}

static void pick_dir(void)
{
    BROWSEINFOW bi;
    memset(&bi, 0, sizeof bi);
    bi.hwndOwner = g_wnd;
    bi.lpszTitle = tr(L"Choose the install folder", L"اختر مجلد التثبيت");
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE p = SHBrowseForFolderW(&bi);
    if (p) {
        WCHAR d[MAX_PATH];
        if (SHGetPathFromIDListW(p, d)) {
            size_t L = wcslen(d);
            if (L > 4 && _wcsicmp(d + L - 4, L"\\DAO") != 0) swprintf(g_dir, MAX_PATH, L"%ls%lsDAO", d, d[L - 1] == L'\\' ? L"" : L"\\");
            else wcscpy(g_dir, d);
            InvalidateRect(g_wnd, NULL, FALSE);
        }
        CoTaskMemFree(p);
    }
}

static LRESULT CALLBACK wnd(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_CREATE:
        g_wnd = h;
        for (int i = 0; i < NBTN; i++) {
            g_btn[i].text = tr(g_btn[i].en, g_btn[i].ar);
            g_btn[i].hwnd = CreateWindowExW(0, L"BUTTON", g_btn[i].text, WS_CHILD | BS_OWNERDRAW | WS_TABSTOP,
                                            0, 0, 10, 10, h, (HMENU)(INT_PTR)g_btn[i].id, g_inst, NULL);
            SetWindowLongPtrW(g_btn[i].hwnd, GWLP_USERDATA, (LONG_PTR)&g_btn[i]);
            SetPropW(g_btn[i].hwnd, L"old", (HANDLE)SetWindowLongPtrW(g_btn[i].hwnd, GWLP_WNDPROC, (LONG_PTR)btn_sub));
        }
        show_page(PAGE_WELCOME);
        return 0;
    case WM_SIZE:
        layout();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        paint(dc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        draw_btn((DRAWITEMSTRUCT *)l);
        return TRUE;
    case WM_PROGRESS:
        g_progress = (int)w;
        if (l) { wcsncpy(g_status, (WCHAR *)l, 255); free((void *)l); }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_DONE:
        if (w) {
            show_page(PAGE_DONE);
            layout();
        } else {
            show_page(PAGE_ERROR);
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_INSTALL: {
            show_page(PAGE_INSTALLING);
            HANDLE t = CreateThread(NULL, 0, install_thread, NULL, 0, NULL);
            if (t) CloseHandle(t);
            break;
        }
        case ID_CANCEL: DestroyWindow(h); break;
        case ID_CHANGE: pick_dir(); break;
        case ID_CHK_DESKTOP: g_desktop = !g_desktop; InvalidateRect(btn(ID_CHK_DESKTOP)->hwnd, NULL, FALSE); break;
        case ID_CHK_RUN: g_run = !g_run; InvalidateRect(btn(ID_CHK_RUN)->hwnd, NULL, FALSE); break;
        case ID_FINISH:
            if (g_page == PAGE_DONE && g_run) run_app_unelevated();
            DestroyWindow(h);
            break;
        }
        return 0;
    case WM_CLOSE:
        if (g_page == PAGE_INSTALLING) return 0;
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static HFONT mkfont(int px, int weight, const WCHAR *face)
{
    return CreateFontW(-S(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

/* dwmapi.dll is not a KnownDLL: load it from System32 explicitly so an elevated
   installer started from the Downloads folder cannot pick up a planted copy. */
static void dark_title_bar(HWND h)
{
    typedef HRESULT (WINAPI *SetAttr)(HWND, DWORD, LPCVOID, DWORD);
    HMODULE dwm = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dwm) return;
    SetAttr set = (SetAttr)(void *)GetProcAddress(dwm, "DwmSetWindowAttribute");
    if (set) {
        BOOL dark = TRUE;
        COLORREF cap = C_PANEL;
        set(h, 20, &dark, sizeof dark);    /* DWMWA_USE_IMMERSIVE_DARK_MODE */
        set(h, 19, &dark, sizeof dark);    /* pre-20H1 builds */
        set(h, 35, &cap, sizeof cap);      /* DWMWA_CAPTION_COLOR */
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    /* restrict any later DLL loads (shell, COM) to System32 */
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

    (void)prev;
    g_inst = inst;
    g_ar = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_ARABIC;
    if (cmd && wcsstr(cmd, L"/uninstall")) return uninstall();

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    HDC sdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(sdc, LOGPIXELSY);
    ReleaseDC(NULL, sdc);

    PWSTR pf = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_ProgramFiles, 0, NULL, &pf))) {
        swprintf(g_dir, MAX_PATH, L"%ls\\DAO", pf);
        CoTaskMemFree(pf);
    } else {
        wcscpy(g_dir, L"C:\\Program Files\\DAO");
    }

    g_font       = mkfont(14, FW_NORMAL, L"Segoe UI");
    g_font_bold  = mkfont(14, FW_SEMIBOLD, L"Segoe UI");
    g_font_small = mkfont(12, FW_NORMAL, L"Segoe UI");
    g_font_title = mkfont(22, FW_BOLD, L"Segoe UI");
    g_font_icon  = mkfont(13, FW_NORMAL, L"Segoe MDL2 Assets");
    g_icon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, S(64), S(64), 0);

    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wnd;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"DaoSetup";
    RegisterClassExW(&wc);

    RECT rc = { 0, 0, S(600), S(420) };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    DWORD ex = g_ar ? WS_EX_LAYOUTRTL : 0;
    AdjustWindowRectEx(&rc, style, FALSE, ex);
    int ww = rc.right - rc.left, wh = rc.bottom - rc.top;
    HWND h = CreateWindowExW(ex, L"DaoSetup", tr(L"DAO Setup", L"تثبيت DAO"), style,
                             (GetSystemMetrics(SM_CXSCREEN) - ww) / 2, (GetSystemMetrics(SM_CYSCREEN) - wh) / 2,
                             ww, wh, NULL, NULL, inst, NULL);
    dark_title_bar(h);
    ShowWindow(h, show);
    UpdateWindow(h);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
