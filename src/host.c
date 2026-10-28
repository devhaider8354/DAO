/*
 * host.c - dao-host.exe, the Chrome Native Messaging host.
 *
 * Chrome starts this process and writes one message to stdin
 * (4-byte length + UTF-8 JSON). We parse the URL and browser headers,
 * make sure DAO is running (starting it if needed), hand the request over
 * with WM_COPYDATA and reply {"ok":true} on stdout.
 */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define MAX_MSG (1024 * 1024)

/* ---------------------------- Minimal JSON ---------------------------- */
typedef struct { const char *p, *end; } Js;

static void js_ws(Js *j) { while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n')) j->p++; }

static void put_utf8(char **o, char *oe, unsigned cp)
{
    char *q = *o;
    if (cp < 0x80)        { if (q + 1 > oe) return; *q++ = (char)cp; }
    else if (cp < 0x800)  { if (q + 2 > oe) return; *q++ = (char)(0xC0 | (cp >> 6)); *q++ = (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000){ if (q + 3 > oe) return; *q++ = (char)(0xE0 | (cp >> 12)); *q++ = (char)(0x80 | ((cp >> 6) & 0x3F)); *q++ = (char)(0x80 | (cp & 0x3F)); }
    else                  { if (q + 4 > oe) return; *q++ = (char)(0xF0 | (cp >> 18)); *q++ = (char)(0x80 | ((cp >> 12) & 0x3F)); *q++ = (char)(0x80 | ((cp >> 6) & 0x3F)); *q++ = (char)(0x80 | (cp & 0x3F)); }
    *o = q;
}

static unsigned hex4(const char *s)
{
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
    }
    return v;
}

/* Read a JSON string into out (UTF-8). Returns 0 on error. */
static int js_string(Js *j, char *out, size_t n)
{
    char *o = out, *oe = out + (n ? n - 1 : 0);
    if (j->p >= j->end || *j->p != '"') return 0;
    j->p++;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\' && j->p < j->end) {
            char e = *j->p++;
            switch (e) {
            case 'n': put_utf8(&o, oe, '\n'); break;
            case 't': put_utf8(&o, oe, '\t'); break;
            case 'r': put_utf8(&o, oe, '\r'); break;
            case 'b': put_utf8(&o, oe, '\b'); break;
            case 'f': put_utf8(&o, oe, '\f'); break;
            case 'u':
                if (j->end - j->p >= 4) {
                    unsigned cp = hex4(j->p);
                    j->p += 4;
                    if (cp >= 0xD800 && cp <= 0xDBFF && j->end - j->p >= 6 && j->p[0] == '\\' && j->p[1] == 'u') {
                        unsigned lo = hex4(j->p + 2);
                        if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); j->p += 6; }
                    }
                    put_utf8(&o, oe, cp);
                }
                break;
            default: put_utf8(&o, oe, (unsigned char)e); break;
            }
        } else if (o < oe) {
            *o++ = c;
        }
    }
    if (j->p < j->end) j->p++;   /* " */
    *o = 0;
    return 1;
}

static void js_skip_value(Js *j)
{
    js_ws(j);
    if (j->p >= j->end) return;
    if (*j->p == '"') { char tmp[4]; js_string(j, tmp, sizeof tmp); return; }
    if (*j->p == '{' || *j->p == '[') {
        int depth = 0, instr = 0;
        for (; j->p < j->end; j->p++) {
            char c = *j->p;
            if (instr) { if (c == '\\') j->p++; else if (c == '"') instr = 0; continue; }
            if (c == '"') instr = 1;
            else if (c == '{' || c == '[') depth++;
            else if (c == '}' || c == ']') { if (--depth == 0) { j->p++; return; } }
        }
        return;
    }
    while (j->p < j->end && *j->p != ',' && *j->p != '}') j->p++;
}

typedef struct {
    char type[32];
    char url[8192];
    char filename[1024];
    char referrer[8192];
    char cookies[65536];
    char ua[1024];
    char size[32];
} Msg;

static int parse_msg(const char *buf, size_t len, Msg *m)
{
    Js j = { buf, buf + len };
    char key[64];
    memset(m, 0, sizeof(*m));
    strcpy(m->size, "-1");
    js_ws(&j);
    if (j.p >= j.end || *j.p != '{') return 0;
    j.p++;
    for (;;) {
        js_ws(&j);
        if (j.p >= j.end) return 0;
        if (*j.p == '}') return 1;
        if (*j.p == ',') { j.p++; continue; }
        if (!js_string(&j, key, sizeof key)) return 0;
        js_ws(&j);
        if (j.p >= j.end || *j.p != ':') return 0;
        j.p++;
        js_ws(&j);

        char *dst = NULL; size_t dn = 0;
        if (!strcmp(key, "type"))      { dst = m->type;     dn = sizeof m->type; }
        else if (!strcmp(key, "url"))       { dst = m->url;      dn = sizeof m->url; }
        else if (!strcmp(key, "filename"))  { dst = m->filename; dn = sizeof m->filename; }
        else if (!strcmp(key, "referrer"))  { dst = m->referrer; dn = sizeof m->referrer; }
        else if (!strcmp(key, "cookies"))   { dst = m->cookies;  dn = sizeof m->cookies; }
        else if (!strcmp(key, "userAgent")) { dst = m->ua;       dn = sizeof m->ua; }
        else if (!strcmp(key, "size"))      { dst = m->size;     dn = sizeof m->size; }

        if (dst && j.p < j.end && *j.p == '"') {
            js_string(&j, dst, dn);
        } else if (dst) {   /* number */
            size_t i = 0;
            while (j.p < j.end && (*j.p == '-' || (*j.p >= '0' && *j.p <= '9') || *j.p == '.') && i < dn - 1) dst[i++] = *j.p++;
            dst[i] = 0;
            js_skip_value(&j);
        } else {
            js_skip_value(&j);
        }
    }
}

/* ------------------------- stdin / stdout ------------------------- */
static int read_all(HANDLE h, void *buf, DWORD n)
{
    DWORD got = 0, r;
    while (got < n) {
        if (!ReadFile(h, (char *)buf + got, n - got, &r, NULL) || r == 0) return 0;
        got += r;
    }
    return 1;
}

static void reply(const char *json)
{
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    UINT32 len = (UINT32)strlen(json);
    DWORD w;
    WriteFile(out, &len, 4, &w, NULL);
    WriteFile(out, json, len, &w, NULL);
    FlushFileBuffers(out);
}

/* -------------------------- Talking to DAO ---------------------------- */
static void app_path(WCHAR *out)
{
    GetModuleFileNameW(NULL, out, MAX_PATH);
    WCHAR *s = wcsrchr(out, L'\\');
    if (s) wcscpy(s + 1, L"dao.exe");
}

static HWND ensure_app(void)
{
    HWND w = FindWindowW(L"DaoMainWindow", NULL);
    if (w) return w;

    WCHAR exe[MAX_PATH], dir[MAX_PATH];
    app_path(exe);
    wcscpy(dir, exe);
    WCHAR *s = wcsrchr(dir, L'\\');
    if (s) *s = 0;

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    /* break away from Chrome's job object so DAO survives when the host exits */
    if (!CreateProcessW(exe, NULL, NULL, NULL, FALSE, CREATE_BREAKAWAY_FROM_JOB, NULL, dir, &si, &pi) &&
        !CreateProcessW(exe, NULL, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi))
        return NULL;
    AllowSetForegroundWindow(pi.dwProcessId);
    WaitForInputIdle(pi.hProcess, 10000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    for (int i = 0; i < 150 && !w; i++) {   /* up to 15 s */
        w = FindWindowW(L"DaoMainWindow", NULL);
        if (!w) Sleep(100);
    }
    return w;
}

static WCHAR *to_wide(const char *s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = (WCHAR *)malloc((n > 0 ? n : 1) * sizeof(WCHAR));
    if (!w) return NULL;
    if (n <= 0) { w[0] = 0; return w; }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

/* Fields are joined with 0x1F, so strip separators and line breaks. */
static void strip_ctrl(WCHAR *s) { for (; *s; s++) if (*s == 0x1F || *s == L'\r' || *s == L'\n' || *s == L'\t') *s = L' '; }

static int send_to_app(HWND w, Msg *m)
{
    const char *fields[6] = { m->url, m->filename, m->referrer, m->cookies, m->ua, m->size };
    WCHAR *wf[6];
    size_t total = 0;
    for (int i = 0; i < 6; i++) {
        wf[i] = to_wide(fields[i]);
        if (!wf[i]) return 0;
        strip_ctrl(wf[i]);
        total += wcslen(wf[i]) + 1;
    }
    WCHAR *buf = (WCHAR *)malloc(total * sizeof(WCHAR));
    if (!buf) return 0;
    WCHAR *p = buf;
    for (int i = 0; i < 6; i++) {
        size_t n = wcslen(wf[i]);
        memcpy(p, wf[i], n * sizeof(WCHAR));
        p += n;
        *p++ = (i < 5) ? 0x1F : 0;
        free(wf[i]);
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    AllowSetForegroundWindow(pid);

    COPYDATASTRUCT cds;
    cds.dwData = 2;
    cds.cbData = (DWORD)(total * sizeof(WCHAR));
    cds.lpData = buf;
    DWORD_PTR res = 0;
    LRESULT ok = SendMessageTimeoutW(w, WM_COPYDATA, 0, (LPARAM)&cds, SMTO_ABORTIFHUNG, 8000, &res);
    free(buf);
    return ok != 0;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show)
{
    (void)inst; (void)prev; (void)cmd; (void)show;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    UINT32 len = 0;

    if (!read_all(in, &len, 4) || len == 0 || len > MAX_MSG) return 1;
    char *buf = (char *)malloc(len + 1);
    if (!buf || !read_all(in, buf, len)) return 1;
    buf[len] = 0;

    static Msg m;
    if (!parse_msg(buf, len, &m)) { reply("{\"ok\":false,\"error\":\"bad message\"}"); return 1; }

    if (!strcmp(m.type, "ping")) {
        /* let the app know the extension is connected */
        HWND app = FindWindowW(L"DaoMainWindow", NULL);
        if (app) {
            COPYDATASTRUCT c = { 3, 0, NULL, };
            DWORD_PTR res;
            SendMessageTimeoutW(app, WM_COPYDATA, 0, (LPARAM)&c, SMTO_ABORTIFHUNG, 3000, &res);
        }
        WCHAR exe[MAX_PATH];
        app_path(exe);
        int exists = GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES;
        int running = FindWindowW(L"DaoMainWindow", NULL) != NULL;
        char r[128];
        snprintf(r, sizeof r, "{\"ok\":%s,\"version\":\"2.1\",\"running\":%s}",
                 exists ? "true" : "false", running ? "true" : "false");
        reply(r);
        return 0;
    }

    if (!strcmp(m.type, "download") && (!strncmp(m.url, "http://", 7) || !strncmp(m.url, "https://", 8))) {
        HWND w = ensure_app();
        if (!w) { reply("{\"ok\":false,\"error\":\"cannot start dao.exe\"}"); return 1; }
        if (!send_to_app(w, &m)) { reply("{\"ok\":false,\"error\":\"dao.exe not responding\"}"); return 1; }
        reply("{\"ok\":true}");
        return 0;
    }

    reply("{\"ok\":false,\"error\":\"unknown request\"}");
    return 1;
}
