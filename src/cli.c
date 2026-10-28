/*
 * dao-cli - command-line version of the DAO download engine.
 *
 *   - Multi-connection downloads: the file is split into segments that are
 *     fetched in parallel with HTTP Range requests.
 *   - Dynamic splitting: a connection that finishes takes over half of the
 *     largest remaining segment, so every connection stays busy.
 *   - Resume: progress is kept in a .dao file next to the .part file; press
 *     Ctrl+C to pause and run the same command again to continue.
 *   - Automatic retries, HTTP/HTTPS, redirects, Unicode file names.
 *
 * Depends only on WinHTTP.
 *
 * Usage:
 *     dao-cli <URL> [-n connections] [-o filename] [-d folder]
 *     dao-cli              (no arguments: prompts for the URL)
 */

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <io.h>
#include <fcntl.h>

#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#endif

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

/* ------------------------------------------------------------------ */
/*                               Settings                              */
/* ------------------------------------------------------------------ */
#define DEFAULT_CONNECTIONS 8
#define MAX_CONNECTIONS     32
#define MAX_SEGMENTS        512
#define MIN_SPLIT_SIZE      (512LL * 1024)   /* smallest half when splitting */
#define BUFFER_SIZE         (64 * 1024)
#define MAX_RETRIES         15
#define STATE_MAGIC         0x31414F44u       /* "DAO1" */
#define USER_AGENT          L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) DAO/1.0"

/* ------------------------------------------------------------------ */
/*                                State                                */
/* ------------------------------------------------------------------ */
typedef struct {
    LONGLONG start;
    LONGLONG pos;     /* next byte to write */
    LONGLONG end;     /* exclusive, -1 = unknown size */
    int      active;  /* owned by a worker */
} Segment;

typedef struct {
    WCHAR    host[512];
    WCHAR    path[8192];
    INTERNET_PORT port;
    int      secure;
} UrlParts;

static CRITICAL_SECTION g_lock;
static Segment   g_seg[MAX_SEGMENTS];
static int       g_nseg = 0;
static LONGLONG  g_total = -1;          /* -1 = unknown */
static int       g_ranges = 0;          /* server supports byte ranges */
static volatile LONG g_stop = 0;        /* set by Ctrl+C */
static volatile LONG g_active_conns = 0;
static volatile LONGLONG g_session_bytes = 0;
static HINTERNET g_session = NULL;
static HANDLE    g_file = INVALID_HANDLE_VALUE;
static UrlParts  g_url;
static WCHAR     g_orig_url[8192];
static WCHAR     g_out_path[MAX_PATH * 2];
static WCHAR     g_part_path[MAX_PATH * 2];
static WCHAR     g_state_path[MAX_PATH * 2];

/* ------------------------------------------------------------------ */
/*                               Helpers                               */
/* ------------------------------------------------------------------ */
static void fmt_size(LONGLONG b, WCHAR *out, size_t n)
{
    const WCHAR *u[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double v = (double)b;
    int i = 0;
    while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
    swprintf(out, n, i == 0 ? L"%.0f %ls" : L"%.2f %ls", v, u[i]);
}

static void fmt_time(LONGLONG s, WCHAR *out, size_t n)
{
    if (s < 0) { swprintf(out, n, L"--:--"); return; }
    if (s >= 3600) swprintf(out, n, L"%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else           swprintf(out, n, L"%02lld:%02lld", s / 60, s % 60);
}

static BOOL WINAPI ctrl_handler(DWORD type)
{
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&g_stop, 1);
        return TRUE;
    }
    return FALSE;
}

static int parse_url(const WCHAR *url, UrlParts *u)
{
    URL_COMPONENTS c;
    WCHAR extra[4096];
    memset(&c, 0, sizeof(c));
    memset(u, 0, sizeof(*u));
    c.dwStructSize     = sizeof(c);
    c.lpszHostName     = u->host;   c.dwHostNameLength  = ARRAYSIZE(u->host);
    c.lpszUrlPath      = u->path;   c.dwUrlPathLength   = ARRAYSIZE(u->path);
    c.lpszExtraInfo    = extra;     c.dwExtraInfoLength = ARRAYSIZE(extra);
    if (!WinHttpCrackUrl(url, 0, 0, &c)) return 0;
    if (c.nScheme != INTERNET_SCHEME_HTTP && c.nScheme != INTERNET_SCHEME_HTTPS) return 0;
    u->port   = c.nPort;
    u->secure = (c.nScheme == INTERNET_SCHEME_HTTPS);
    if (u->path[0] == 0) wcscpy(u->path, L"/");
    if (wcslen(u->path) + wcslen(extra) < ARRAYSIZE(u->path)) wcscat(u->path, extra);
    return 1;
}

/* Decode %XX (UTF-8) escapes. */
static void url_decode(const WCHAR *in, WCHAR *out, size_t n)
{
    char buf[4096];
    size_t j = 0;
    for (size_t i = 0; in[i] && j < sizeof(buf) - 4; i++) {
        if (in[i] == L'%' && iswxdigit(in[i + 1]) && iswxdigit(in[i + 2])) {
            WCHAR h[3] = { in[i + 1], in[i + 2], 0 };
            buf[j++] = (char)wcstol(h, NULL, 16);
            i += 2;
        } else if (in[i] == L'+') {
            buf[j++] = ' ';
        } else if (in[i] < 0x80) {
            buf[j++] = (char)in[i];
        } else {
            j += WideCharToMultiByte(CP_UTF8, 0, &in[i], 1, buf + j, (int)(sizeof(buf) - j - 1), NULL, NULL);
        }
    }
    buf[j] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, buf, -1, out, (int)n)) wcsncpy(out, in, n - 1);
}

/* Strip path separators, reserved characters, bidi overrides, device names
   and trailing dots/spaces from a server-supplied file name. */
static void sanitize_name(WCHAR *s)
{
    WCHAR *w = s;
    for (WCHAR *p = s; *p; p++) {
        WCHAR c = *p;
        if (c == 0x200E || c == 0x200F || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069)) continue;
        if (wcschr(L"\\/:*?\"<>|", c) || c < 32) c = L'_';
        *w++ = c;
    }
    *w = 0;
    WCHAR *start = s;
    while (*start == L' ' || *start == L'.') start++;
    if (start != s) memmove(s, start, (wcslen(start) + 1) * sizeof(WCHAR));
    size_t n = wcslen(s);
    while (n && (s[n - 1] == L' ' || s[n - 1] == L'.')) s[--n] = 0;
    if (!n) { wcscpy(s, L"download.bin"); return; }

    size_t base = wcscspn(s, L".");
    int reserved = (base == 3 && (!_wcsnicmp(s, L"CON", 3) || !_wcsnicmp(s, L"PRN", 3) ||
                                  !_wcsnicmp(s, L"AUX", 3) || !_wcsnicmp(s, L"NUL", 3))) ||
                   (base == 4 && (!_wcsnicmp(s, L"COM", 3) || !_wcsnicmp(s, L"LPT", 3)) && s[3] >= L'1' && s[3] <= L'9');
    if (reserved && n + 1 < MAX_PATH) {
        memmove(s + 1, s, (n + 1) * sizeof(WCHAR));
        s[0] = L'_';
    }
}

/* ------------------------------------------------------------------ */
/*                                 HTTP                                */
/* ------------------------------------------------------------------ */
typedef struct {
    HINTERNET conn;
    HINTERNET req;
} HttpReq;

static void http_close(HttpReq *r)
{
    if (r->req)  WinHttpCloseHandle(r->req);
    if (r->conn) WinHttpCloseHandle(r->conn);
    r->req = r->conn = NULL;
}

/* Send a GET with an optional Range header. Returns the HTTP status, 0 on failure. */
static DWORD http_get(const UrlParts *u, LONGLONG from, LONGLONG to, HttpReq *r)
{
    DWORD status = 0, sz = sizeof(status);
    r->conn = r->req = NULL;

    r->conn = WinHttpConnect(g_session, u->host, u->port, 0);
    if (!r->conn) return 0;
    r->req = WinHttpOpenRequest(r->conn, L"GET", u->path, NULL, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                (u->secure ? WINHTTP_FLAG_SECURE : 0) | WINHTTP_FLAG_REFRESH);
    if (!r->req) { http_close(r); return 0; }

    if (from >= 0) {
        WCHAR hdr[128];
        if (to >= 0) swprintf(hdr, 128, L"Range: bytes=%lld-%lld", from, to);
        else         swprintf(hdr, 128, L"Range: bytes=%lld-", from);
        WinHttpAddRequestHeaders(r->req, hdr, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    WinHttpAddRequestHeaders(r->req, L"Accept-Encoding: identity", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(r->req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(r->req, NULL)) {
        http_close(r);
        return 0;
    }
    WinHttpQueryHeaders(r->req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    return status;
}

static int query_header(HINTERNET req, DWORD what, WCHAR *out, DWORD outChars)
{
    DWORD sz = outChars * sizeof(WCHAR);
    out[0] = 0;
    return WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, out, &sz, WINHTTP_NO_HEADER_INDEX);
}

/* File name from a Content-Disposition header. */
static int name_from_disposition(const WCHAR *cd, WCHAR *out, size_t n)
{
    const WCHAR *p = wcsstr(cd, L"filename*=");
    WCHAR tmp[1024];
    size_t i = 0;
    if (p) {
        p += 10;
        const WCHAR *q = wcsstr(p, L"''");
        if (q) p = q + 2;
        while (*p && *p != L';' && *p != L'"' && i < ARRAYSIZE(tmp) - 1) tmp[i++] = *p++;
        tmp[i] = 0;
        url_decode(tmp, out, n);
        return out[0] != 0;
    }
    p = wcsstr(cd, L"filename=");
    if (!p) return 0;
    p += 9;
    if (*p == L'"') p++;
    while (*p && *p != L'"' && *p != L';' && i < ARRAYSIZE(tmp) - 1) tmp[i++] = *p++;
    tmp[i] = 0;
    url_decode(tmp, out, n);
    return out[0] != 0;
}

static void name_from_url(const WCHAR *path, WCHAR *out, size_t n)
{
    WCHAR tmp[4096];
    wcsncpy(tmp, path, ARRAYSIZE(tmp) - 1);
    tmp[ARRAYSIZE(tmp) - 1] = 0;
    WCHAR *q = wcschr(tmp, L'?'); if (q) *q = 0;
    q = wcschr(tmp, L'#');        if (q) *q = 0;
    WCHAR *s = wcsrchr(tmp, L'/');
    s = s ? s + 1 : tmp;
    if (!*s) s = L"download.bin";
    url_decode(s, out, n);
}

/*
 * Probe with "Range: bytes=0-0":
 *   206 -> ranges supported, total size comes from Content-Range
 *   200 -> no range support, fall back to a single connection
 */
static int probe(const WCHAR *url, WCHAR *suggested_name, size_t n)
{
    HttpReq r;
    WCHAR buf[8192];
    DWORD st;

    if (!parse_url(url, &g_url)) {
        wprintf(L"[x] Invalid URL (only http/https supported)\n");
        return 0;
    }
    st = http_get(&g_url, 0, 0, &r);
    if (st == 0) {
        wprintf(L"[x] Connection failed (error %lu)\n", GetLastError());
        return 0;
    }
    if (st >= 400) {
        wprintf(L"[x] Server returned HTTP %lu\n", st);
        http_close(&r);
        return 0;
    }

    /* final URL after redirects */
    DWORD sz = sizeof(buf);
    if (WinHttpQueryOption(r.req, WINHTTP_OPTION_URL, buf, &sz)) {
        UrlParts fin;
        if (parse_url(buf, &fin)) g_url = fin;
    }

    if (st == 206 && query_header(r.req, WINHTTP_QUERY_CONTENT_RANGE, buf, ARRAYSIZE(buf))) {
        WCHAR *slash = wcschr(buf, L'/');
        if (slash && slash[1] != L'*') {
            g_total  = _wtoi64(slash + 1);
            g_ranges = g_total > 0;
        }
    }
    if (!g_ranges) {
        g_total = -1;
        if (query_header(r.req, WINHTTP_QUERY_CONTENT_LENGTH, buf, ARRAYSIZE(buf)))
            g_total = _wtoi64(buf);
        if (st == 206) g_total = -1;  /* 206 without a usable total */
    }

    suggested_name[0] = 0;
    if (query_header(r.req, WINHTTP_QUERY_CONTENT_DISPOSITION, buf, ARRAYSIZE(buf)))
        name_from_disposition(buf, suggested_name, n);
    if (!suggested_name[0]) name_from_url(g_url.path, suggested_name, n);
    sanitize_name(suggested_name);

    http_close(&r);
    return 1;
}

/* ------------------------------------------------------------------ */
/*                             Resume state                            */
/* ------------------------------------------------------------------ */
static void save_state(void)
{
    if (!g_ranges) return;
    WCHAR tmp[MAX_PATH * 2 + 8];
    swprintf(tmp, ARRAYSIZE(tmp), L"%ls.tmp", g_state_path);
    FILE *f = _wfopen(tmp, L"wb");
    if (!f) return;

    EnterCriticalSection(&g_lock);
    unsigned magic = STATE_MAGIC;
    unsigned ulen = (unsigned)wcslen(g_orig_url);
    fwrite(&magic, sizeof magic, 1, f);
    fwrite(&ulen, sizeof ulen, 1, f);
    fwrite(g_orig_url, sizeof(WCHAR), ulen, f);
    fwrite(&g_total, sizeof g_total, 1, f);
    int cnt = 0;
    for (int i = 0; i < g_nseg; i++) if (g_seg[i].pos < g_seg[i].end) cnt++;
    fwrite(&cnt, sizeof cnt, 1, f);
    for (int i = 0; i < g_nseg; i++) {
        if (g_seg[i].pos >= g_seg[i].end) continue;  /* skip finished segments */
        fwrite(&g_seg[i].start, sizeof(LONGLONG), 1, f);
        fwrite(&g_seg[i].pos,   sizeof(LONGLONG), 1, f);
        fwrite(&g_seg[i].end,   sizeof(LONGLONG), 1, f);
    }
    LeaveCriticalSection(&g_lock);

    fclose(f);
    MoveFileExW(tmp, g_state_path, MOVEFILE_REPLACE_EXISTING);
}

static int load_state(void)
{
    FILE *f = _wfopen(g_state_path, L"rb");
    if (!f) return 0;
    unsigned magic = 0, ulen = 0;
    WCHAR url[8192];
    LONGLONG total = 0;
    int cnt = 0, ok = 0;

    if (fread(&magic, sizeof magic, 1, f) != 1 || magic != STATE_MAGIC) goto done;
    if (fread(&ulen, sizeof ulen, 1, f) != 1 || ulen >= ARRAYSIZE(url)) goto done;
    if (fread(url, sizeof(WCHAR), ulen, f) != ulen) goto done;
    url[ulen] = 0;
    if (fread(&total, sizeof total, 1, f) != 1 || total != g_total) goto done;
    if (fread(&cnt, sizeof cnt, 1, f) != 1 || cnt < 0 || cnt > MAX_SEGMENTS) goto done;
    if (wcscmp(url, g_orig_url) != 0)
        wprintf(L"[!] Note: resuming a file that was started from a different URL\n");

    for (int i = 0; i < cnt; i++) {
        Segment s = { 0 };
        if (fread(&s.start, sizeof(LONGLONG), 1, f) != 1 ||
            fread(&s.pos,   sizeof(LONGLONG), 1, f) != 1 ||
            fread(&s.end,   sizeof(LONGLONG), 1, f) != 1) goto done;
        if (s.start < 0 || s.pos < s.start || s.end > total || s.pos > s.end) goto done;
        g_seg[i] = s;
    }
    g_nseg = cnt;
    ok = 1;
done:
    fclose(f);
    if (!ok) g_nseg = 0;
    return ok;
}

static LONGLONG remaining_bytes(void)
{
    LONGLONG r = 0;
    for (int i = 0; i < g_nseg; i++)
        if (g_seg[i].end > g_seg[i].pos) r += g_seg[i].end - g_seg[i].pos;
    return r;
}

/* ------------------------------------------------------------------ */
/*                          Dynamic splitting                          */
/* ------------------------------------------------------------------ */
/* Caller holds g_lock. Returns a segment index for the worker, or -1. */
static int pick_segment(void)
{
    /* 1) an unfinished segment nobody is working on */
    for (int i = 0; i < g_nseg; i++) {
        if (!g_seg[i].active && (g_seg[i].end < 0 || g_seg[i].pos < g_seg[i].end)) {
            g_seg[i].active = 1;
            return i;
        }
    }
    if (!g_ranges || g_nseg >= MAX_SEGMENTS) return -1;

    /* 2) split the largest active segment and take its second half */
    int best = -1;
    LONGLONG bestRem = 0;
    for (int i = 0; i < g_nseg; i++) {
        LONGLONG rem = g_seg[i].end - g_seg[i].pos;
        if (g_seg[i].active && rem > bestRem) { bestRem = rem; best = i; }
    }
    if (best < 0 || bestRem < 2 * MIN_SPLIT_SIZE) return -1;

    LONGLONG mid = g_seg[best].pos + bestRem / 2;
    Segment ns;
    ns.start  = mid;
    ns.pos    = mid;
    ns.end    = g_seg[best].end;
    ns.active = 1;
    g_seg[best].end = mid;   /* the original worker stops at mid */
    g_seg[g_nseg] = ns;
    return g_nseg++;
}

static int write_at(LONGLONG off, const void *data, DWORD len)
{
    OVERLAPPED ov;
    DWORD written = 0;
    memset(&ov, 0, sizeof(ov));
    ov.Offset     = (DWORD)(off & 0xFFFFFFFF);
    ov.OffsetHigh = (DWORD)(off >> 32);
    return WriteFile(g_file, data, len, &written, &ov) && written == len;
}

/* Download one segment. Returns 1 = done, 0 = retry, -1 = permanent error. */
static int download_segment(int si, char *buf)
{
    LONGLONG from, to;
    HttpReq r;
    DWORD st;

    EnterCriticalSection(&g_lock);
    from = g_seg[si].pos;
    to   = g_seg[si].end;
    LeaveCriticalSection(&g_lock);
    if (to >= 0 && from >= to) return 1;

    if (g_ranges) st = http_get(&g_url, from, to - 1, &r);
    else          st = http_get(&g_url, -1, -1, &r);
    if (st == 0) return 0;

    if (g_ranges && st != 206) {
        http_close(&r);
        /* a 200 here means the Range header was ignored */
        return (st >= 400 && st < 500 && st != 408 && st != 429) ? -1 : 0;
    }
    if (!g_ranges && st != 200) { http_close(&r); return st >= 400 && st < 500 ? -1 : 0; }

    InterlockedIncrement(&g_active_conns);
    int result = 0;
    for (;;) {
        DWORD got = 0;
        if (g_stop) break;
        if (!WinHttpReadData(r.req, buf, BUFFER_SIZE, &got)) break;   /* connection dropped */
        if (got == 0) {                                              /* end of body */
            if (g_seg[si].end < 0) {                                 /* unknown size */
                EnterCriticalSection(&g_lock);
                g_seg[si].end = g_seg[si].pos;
                g_total = g_seg[si].pos;
                LeaveCriticalSection(&g_lock);
                result = 1;
            } else {
                result = g_seg[si].pos >= g_seg[si].end;
            }
            break;
        }

        EnterCriticalSection(&g_lock);
        LONGLONG pos = g_seg[si].pos, end = g_seg[si].end;
        LeaveCriticalSection(&g_lock);

        if (end >= 0 && pos + got > end) got = (DWORD)(end - pos);   /* segment was split */
        if (got && !write_at(pos, buf, got)) {
            wprintf(L"\n[x] Disk write failed (error %lu)\n", GetLastError());
            InterlockedExchange(&g_stop, 1);
            result = -1;
            break;
        }

        EnterCriticalSection(&g_lock);
        g_seg[si].pos += got;
        int done = g_seg[si].end >= 0 && g_seg[si].pos >= g_seg[si].end;
        LeaveCriticalSection(&g_lock);
        InterlockedExchangeAdd64(&g_session_bytes, got);
        if (done) { result = 1; break; }
    }
    InterlockedDecrement(&g_active_conns);
    http_close(&r);
    return result;
}

static DWORD WINAPI worker(LPVOID arg)
{
    char *buf = (char *)malloc(BUFFER_SIZE);
    int si = -1, fails = 0;
    (void)arg;
    if (!buf) return 1;

    while (!g_stop) {
        if (si < 0) {
            EnterCriticalSection(&g_lock);
            si = pick_segment();
            LeaveCriticalSection(&g_lock);
            if (si < 0) break;                      /* nothing left */
            fails = 0;
        }

        LONGLONG before = g_seg[si].pos;
        int r = download_segment(si, buf);
        if (r == 1) {
            EnterCriticalSection(&g_lock);
            g_seg[si].active = 0;
            LeaveCriticalSection(&g_lock);
            si = -1;
            continue;
        }
        if (r == -1 || g_stop) break;

        /* transient failure: retry with backoff */
        if (g_seg[si].pos > before) fails = 0;     /* progress resets the counter */
        if (!g_ranges) {                           /* no ranges: start over */
            EnterCriticalSection(&g_lock);
            InterlockedExchangeAdd64(&g_session_bytes, -(g_seg[si].pos - g_seg[si].start));
            g_seg[si].pos = g_seg[si].start;
            LeaveCriticalSection(&g_lock);
        }
        if (++fails > MAX_RETRIES) break;
        DWORD wait = 500u * (DWORD)(fails < 10 ? fails : 10);
        for (DWORD t = 0; t < wait && !g_stop; t += 100) Sleep(100);
    }

    if (si >= 0) {
        EnterCriticalSection(&g_lock);
        g_seg[si].active = 0;
        LeaveCriticalSection(&g_lock);
    }
    free(buf);
    return 0;
}

/* ------------------------------------------------------------------ */
/*                               Output                                */
/* ------------------------------------------------------------------ */
static void print_progress(LONGLONG speed, int final)
{
    WCHAR a[32], b[32], c[32], t[32];
    LONGLONG done;
    EnterCriticalSection(&g_lock);
    done = (g_total > 0 && g_ranges) ? g_total - remaining_bytes()
                                     : (g_nseg ? g_seg[0].pos : 0);
    LeaveCriticalSection(&g_lock);

    fmt_size(done, a, 32);
    fmt_size(speed, c, 32);
    if (g_total > 0) {
        double pct = 100.0 * (double)done / (double)g_total;
        int bars = (int)(pct / 4);
        WCHAR bar[26];
        for (int i = 0; i < 25; i++) bar[i] = i < bars ? L'#' : L'-';
        bar[25] = 0;
        fmt_size(g_total, b, 32);
        fmt_time(speed > 0 ? (g_total - done) / speed : -1, t, 32);
        wprintf(L"\r[%ls] %5.1f%%  %ls / %ls  %ls/s  ETA %ls  conn:%ld   ",
                bar, pct, a, b, c, t, g_active_conns);
    } else {
        wprintf(L"\r%ls  %ls/s  conn:%ld   ", a, c, g_active_conns);
    }
    if (final) wprintf(L"\n");
    fflush(stdout);
}

/* ------------------------------------------------------------------ */
/*                              main                                   */
/* ------------------------------------------------------------------ */
static void usage(void)
{
    wprintf(L"DAO Download Manager 1.0\n"
            L"Usage: dao <URL> [-n connections] [-o filename] [-d folder]\n"
            L"  -n  number of connections (1-%d, default %d)\n"
            L"  -o  output file name\n"
            L"  -d  output folder\n"
            L"Press Ctrl+C to pause; run the same command again to resume.\n",
            MAX_CONNECTIONS, DEFAULT_CONNECTIONS);
}

int wmain(int argc, wchar_t **argv)
{
    WCHAR url[8192] = { 0 }, out_name[MAX_PATH] = { 0 }, out_dir[MAX_PATH] = { 0 };
    WCHAR name[MAX_PATH];
    int nconn = DEFAULT_CONNECTIONS;

    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stdin),  _O_U16TEXT);

    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"-n") && i + 1 < argc)      nconn = _wtoi(argv[++i]);
        else if (!wcscmp(argv[i], L"-o") && i + 1 < argc) wcsncpy(out_name, argv[++i], MAX_PATH - 1);
        else if (!wcscmp(argv[i], L"-d") && i + 1 < argc) wcsncpy(out_dir,  argv[++i], MAX_PATH - 1);
        else if (!wcscmp(argv[i], L"-h") || !wcscmp(argv[i], L"--help")) { usage(); return 0; }
        else wcsncpy(url, argv[i], ARRAYSIZE(url) - 1);
    }
    if (!url[0]) {
        usage();
        wprintf(L"\nURL: ");
        fflush(stdout);
        if (!fgetws(url, ARRAYSIZE(url), stdin)) return 1;
        url[wcscspn(url, L"\r\n")] = 0;
        /* strip surrounding quotes */
        if (url[0] == L'"') { memmove(url, url + 1, wcslen(url) * sizeof(WCHAR)); WCHAR *q = wcschr(url, L'"'); if (q) *q = 0; }
        if (!url[0]) return 1;
    }
    if (nconn < 1) nconn = 1;
    if (nconn > MAX_CONNECTIONS) nconn = MAX_CONNECTIONS;
    wcsncpy(g_orig_url, url, ARRAYSIZE(g_orig_url) - 1);

    InitializeCriticalSection(&g_lock);
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    g_session = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!g_session) { wprintf(L"[x] WinHttpOpen failed\n"); return 1; }
    {
        DWORD proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(g_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof proto)) {
            proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;   /* older Windows without TLS 1.3 */
            WinHttpSetOption(g_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof proto);
        }
        WinHttpSetTimeouts(g_session, 15000, 15000, 30000, 30000);
        /* WinHTTP limits connections per server by default */
        DWORD maxc = 128;
        WinHttpSetOption(g_session, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &maxc, sizeof maxc);
        WinHttpSetOption(g_session, WINHTTP_OPTION_MAX_CONNS_PER_1_0_SERVER, &maxc, sizeof maxc);
    }

    wprintf(L"[*] Connecting...\n");
    if (!probe(url, name, ARRAYSIZE(name))) return 1;
    if (out_name[0]) wcscpy(name, out_name);

    if (out_dir[0]) {
        CreateDirectoryW(out_dir, NULL);
        size_t L = wcslen(out_dir);
        swprintf(g_out_path, ARRAYSIZE(g_out_path), L"%ls%ls%ls", out_dir,
                 (out_dir[L - 1] == L'\\' || out_dir[L - 1] == L'/') ? L"" : L"\\", name);
    } else {
        wcsncpy(g_out_path, name, ARRAYSIZE(g_out_path) - 1);
    }
    swprintf(g_part_path,  ARRAYSIZE(g_part_path),  L"%ls.part", g_out_path);
    swprintf(g_state_path, ARRAYSIZE(g_state_path), L"%ls.dao",  g_out_path);

    {
        WCHAR s[32];
        if (g_total > 0) fmt_size(g_total, s, 32); else wcscpy(s, L"unknown");
        wprintf(L"[*] File   : %ls\n[*] Size   : %ls\n[*] Resume : %ls\n",
                g_out_path, s, g_ranges ? L"supported" : L"NOT supported (single connection)");
    }

    /* ---- resume or start fresh ---- */
    int resumed = 0;
    if (g_ranges && GetFileAttributesW(g_part_path) != INVALID_FILE_ATTRIBUTES && load_state()) {
        resumed = 1;
        WCHAR s[32];
        fmt_size(g_total - remaining_bytes(), s, 32);
        wprintf(L"[*] Resuming, %ls already downloaded\n", s);
    }

    g_file = CreateFileW(g_part_path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                         resumed ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_file == INVALID_HANDLE_VALUE) {
        wprintf(L"[x] Cannot create file (error %lu)\n", GetLastError());
        return 1;
    }

    if (!resumed) {
        if (g_ranges) {
            /* preallocate and split into equal segments */
            LARGE_INTEGER li; li.QuadPart = g_total;
            if (!SetFilePointerEx(g_file, li, NULL, FILE_BEGIN) || !SetEndOfFile(g_file)) {
                wprintf(L"[x] Not enough disk space\n");
                CloseHandle(g_file);
                DeleteFileW(g_part_path);
                return 1;
            }
            int n = nconn;
            while (n > 1 && g_total / n < MIN_SPLIT_SIZE) n--;
            LONGLONG chunk = g_total / n;
            for (int i = 0; i < n; i++) {
                g_seg[i].start = g_seg[i].pos = chunk * i;
                g_seg[i].end   = (i == n - 1) ? g_total : chunk * (i + 1);
                g_seg[i].active = 0;
            }
            g_nseg = n;
            save_state();
        } else {
            g_seg[0].start = g_seg[0].pos = 0;
            g_seg[0].end = g_total > 0 ? g_total : -1;
            g_nseg = 1;
            nconn = 1;
        }
    }
    if (!g_ranges) nconn = 1;

    /* ---- start workers ---- */
    HANDLE th[MAX_CONNECTIONS];
    int nth = 0;
    for (int i = 0; i < nconn; i++) {
        th[nth] = CreateThread(NULL, 0, worker, NULL, 0, NULL);
        if (th[nth]) nth++;
    }
    wprintf(L"[*] Downloading with %d connection(s)  (Ctrl+C to pause)\n", nth);

    LONGLONG last_bytes = 0, speed = 0;
    DWORD last_tick = GetTickCount(), last_save = last_tick;
    for (;;) {
        DWORD w = WaitForMultipleObjects(nth, th, TRUE, 500);
        DWORD now = GetTickCount();
        if (now - last_tick >= 1000 || w != WAIT_TIMEOUT) {
            LONGLONG cur = g_session_bytes;
            DWORD dt = now - last_tick;
            if (dt > 0) {
                LONGLONG inst = (cur - last_bytes) * 1000 / dt;
                speed = speed ? (speed * 7 + inst * 3) / 10 : inst;   /* smoothing */
            }
            last_bytes = cur;
            last_tick = now;
        }
        if (now - last_save >= 2000) { save_state(); last_save = now; }
        print_progress(speed, w != WAIT_TIMEOUT);
        if (w != WAIT_TIMEOUT) break;
    }
    for (int i = 0; i < nth; i++) CloseHandle(th[i]);

    /* ---- result ---- */
    int complete = 1;
    for (int i = 0; i < g_nseg; i++)
        if (g_seg[i].end < 0 || g_seg[i].pos < g_seg[i].end) complete = 0;

    if (complete && !g_ranges && g_total > 0) {
        /* single-stream download: trim to the real size */
        LARGE_INTEGER li; li.QuadPart = g_total;
        SetFilePointerEx(g_file, li, NULL, FILE_BEGIN);
        SetEndOfFile(g_file);
    }
    CloseHandle(g_file);

    if (complete) {
        if (!MoveFileExW(g_part_path, g_out_path, MOVEFILE_REPLACE_EXISTING)) {
            wprintf(L"[x] Could not rename .part file (error %lu)\n", GetLastError());
            return 1;
        }
        DeleteFileW(g_state_path);
        wprintf(L"[+] Done: %ls\n", g_out_path);
        WinHttpCloseHandle(g_session);
        return 0;
    }

    save_state();
    if (g_ranges)
        wprintf(L"[!] Paused/stopped. Run the same command again to resume.\n");
    else
        wprintf(L"[x] Download stopped. This server does not support resume.\n");
    WinHttpCloseHandle(g_session);
    return 2;
}
