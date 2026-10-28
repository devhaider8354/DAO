/*
 * engine.c - Download engine
 *
 * Each download has a manager thread that probes the URL and spawns worker
 * threads. Every worker fetches one segment with an HTTP Range request and
 * writes it at its own offset in the .part file. When a worker finishes, it
 * splits the largest remaining segment in half and takes the second half,
 * so all connections stay busy until the end. Progress is saved to a .dao
 * file every two seconds so downloads can be resumed.
 */
#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#endif

#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

#define MIN_SPLIT_SIZE  (512LL * 1024)
#define BUFFER_SIZE     (64 * 1024)
#define MAX_RETRIES     15
#define STATE_MAGIC     0x31414F44u
#define USER_AGENT      L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) DAO/2.0"

static HINTERNET g_session;

/* ------------------------------------------------------------------ */
void fmt_size(LONGLONG b, WCHAR *out, size_t n)
{
    const WCHAR *u[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double v = (double)b;
    int i = 0;
    if (b < 0) { swprintf(out, n, L"?"); return; }
    while (v >= 1024.0 && i < 4) { v /= 1024.0; i++; }
    swprintf(out, n, i == 0 ? L"%.0f %ls" : (v < 10 ? L"%.2f %ls" : L"%.1f %ls"), v, u[i]);
}

void fmt_time(LONGLONG s, WCHAR *out, size_t n)
{
    if (s < 0) { swprintf(out, n, L"--:--"); return; }
    if (s >= 3600) swprintf(out, n, L"%lld:%02lld:%02lld", s / 3600, (s / 60) % 60, s % 60);
    else           swprintf(out, n, L"%02lld:%02lld", s / 60, s % 60);
}

void engine_init(void)
{
    g_session = WinHttpOpen(USER_AGENT, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (g_session) {
        DWORD proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(g_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof proto)) {
            proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(g_session, WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof proto);
        }
        WinHttpSetTimeouts(g_session, 15000, 15000, 30000, 30000);
        /* WinHTTP limits connections per server by default */
        DWORD maxc = 128;
        WinHttpSetOption(g_session, WINHTTP_OPTION_MAX_CONNS_PER_SERVER, &maxc, sizeof maxc);
        WinHttpSetOption(g_session, WINHTTP_OPTION_MAX_CONNS_PER_1_0_SERVER, &maxc, sizeof maxc);
    }
}

void engine_shutdown(void)
{
    if (g_session) WinHttpCloseHandle(g_session);
    g_session = NULL;
}

static void set_status(Download *d, DlStatus s)
{
    InterlockedExchange(&d->status, s);
    if (d->notify_hwnd) PostMessageW(d->notify_hwnd, WM_DL_CHANGED, 0, (LPARAM)d);
}

static void set_error(Download *d, DlError err, DWORD detail)
{
    EnterCriticalSection(&d->lock);
    d->err = err;
    d->err_detail = detail;
    LeaveCriticalSection(&d->lock);
}

/* ------------------------------------------------------------------ */
/*                       URL and file name helpers                     */
/* ------------------------------------------------------------------ */
static int parse_url(const WCHAR *url, UrlParts *u)
{
    URL_COMPONENTS c;
    WCHAR extra[4096];
    memset(&c, 0, sizeof(c));
    memset(u, 0, sizeof(*u));
    c.dwStructSize  = sizeof(c);
    c.lpszHostName  = u->host;  c.dwHostNameLength  = ARRAYSIZE(u->host);
    c.lpszUrlPath   = u->path;  c.dwUrlPathLength   = ARRAYSIZE(u->path);
    c.lpszExtraInfo = extra;    c.dwExtraInfoLength = ARRAYSIZE(extra);
    if (!WinHttpCrackUrl(url, 0, 0, &c)) return 0;
    if (c.nScheme != INTERNET_SCHEME_HTTP && c.nScheme != INTERNET_SCHEME_HTTPS) return 0;
    u->port   = c.nPort;
    u->secure = (c.nScheme == INTERNET_SCHEME_HTTPS);
    if (!u->path[0]) wcscpy(u->path, L"/");
    if (wcslen(u->path) + wcslen(extra) < ARRAYSIZE(u->path)) wcscat(u->path, extra);
    return 1;
}

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
    if (!MultiByteToWideChar(CP_UTF8, 0, buf, -1, out, (int)n)) {
        wcsncpy(out, in, n - 1);
        out[n - 1] = 0;
    }
}

static int is_bidi_control(WCHAR c)
{
    return c == 0x200E || c == 0x200F || (c >= 0x202A && c <= 0x202E) || (c >= 0x2066 && c <= 0x2069);
}

/* CON, PRN, AUX, NUL, COM1-9, LPT1-9 (with or without an extension) open devices, not files. */
static int is_reserved_name(const WCHAR *s)
{
    static const WCHAR *names[] = { L"CON", L"PRN", L"AUX", L"NUL", NULL };
    size_t base = wcscspn(s, L".");
    for (int i = 0; names[i]; i++)
        if (base == 3 && !_wcsnicmp(s, names[i], 3)) return 1;
    if (base == 4 && (!_wcsnicmp(s, L"COM", 3) || !_wcsnicmp(s, L"LPT", 3)) && s[3] >= L'1' && s[3] <= L'9')
        return 1;
    return 0;
}

/* Make a server-supplied name safe to create on disk: no path separators or
   reserved characters, no bidi overrides (e.g. "invoice\u202Efdp.exe" posing
   as a PDF), no device names, no trailing dots or spaces. */
void dl_sanitize_name(WCHAR *s)
{
    WCHAR *w = s;
    for (WCHAR *p = s; *p; p++) {
        WCHAR c = *p;
        if (is_bidi_control(c)) continue;
        if (wcschr(L"\\/:*?\"<>|", c) || c < 32) c = L'_';
        *w++ = c;
    }
    *w = 0;

    WCHAR *start = s;
    while (*start == L' ' || *start == L'.') start++;
    if (start != s) memmove(s, start, (wcslen(start) + 1) * sizeof(WCHAR));
    size_t n = wcslen(s);
    while (n && (s[n - 1] == L' ' || s[n - 1] == L'.')) s[--n] = 0;

    if (!s[0]) { wcscpy(s, L"download.bin"); return; }
    if (is_reserved_name(s) && n + 1 < MAX_PATH) {
        memmove(s + 1, s, (n + 1) * sizeof(WCHAR));
        s[0] = L'_';
    }
}


/* ---------- File names embedded in token (JWT) links ---------- */
static int b64url_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-' || c == '+') return 62;
    if (c == '_' || c == '/') return 63;
    return -1;
}

static int has_extension(const WCHAR *s)
{
    const WCHAR *dot = wcsrchr(s, L'.');
    if (!dot || dot == s) return 0;
    size_t n = wcslen(dot + 1);
    if (n < 1 || n > 6) return 0;
    for (const WCHAR *p = dot + 1; *p; p++) if (!iswalnum(*p)) return 0;
    return 1;
}

/* Scan a flat JSON object and return the string value that best looks like a file name. */
static int json_pick_filename(const char *js, WCHAR *out, size_t n)
{
    static const char *pref[] = { "filename", "file_name", "fileName", "name", "file", "title", "sub", NULL };
    WCHAR best[MAX_PATH] = L"";
    int best_rank = 1000;
    const char *p = js;
    while ((p = strchr(p, '"')) != NULL) {
        /* key */
        char key[64]; size_t k = 0;
        p++;
        while (*p && *p != '"' && k < sizeof(key) - 1) key[k++] = *p++;
        key[k] = 0;
        if (*p != '"') break;
        p++;
        while (*p == ' ') p++;
        if (*p != ':') continue;
        p++;
        while (*p == ' ') p++;
        if (*p != '"') continue;
        p++;
        /* string value */
        char val[1024]; size_t v = 0;
        while (*p && *p != '"' && v < sizeof(val) - 4) {
            if (*p == '\\' && p[1]) {
                p++;
                if (*p == 'u' && p[1] && p[2] && p[3] && p[4]) {
                    char hx[5] = { p[1], p[2], p[3], p[4], 0 };
                    unsigned cp = (unsigned)strtoul(hx, NULL, 16);
                    if (cp < 0x80) val[v++] = (char)cp;
                    else if (cp < 0x800) { val[v++] = (char)(0xC0 | (cp >> 6)); val[v++] = (char)(0x80 | (cp & 0x3F)); }
                    else { val[v++] = (char)(0xE0 | (cp >> 12)); val[v++] = (char)(0x80 | ((cp >> 6) & 0x3F)); val[v++] = (char)(0x80 | (cp & 0x3F)); }
                    p += 5;
                    continue;
                }
                val[v++] = (*p == 'n' || *p == 't') ? ' ' : *p;
                p++;
                continue;
            }
            val[v++] = *p++;
        }
        val[v] = 0;
        if (*p == '"') p++;

        WCHAR w[MAX_PATH];
        if (!MultiByteToWideChar(CP_UTF8, 0, val, -1, w, MAX_PATH)) continue;
        if (!has_extension(w) || wcsstr(w, L"://") || wcslen(w) > 240) continue;
        int rank = 100;
        for (int i = 0; pref[i]; i++) if (!strcmp(key, pref[i])) { rank = i; break; }
        if (rank < best_rank) { best_rank = rank; wcscpy(best, w); }
    }
    if (!best[0]) return 0;
    wcsncpy(out, best, n - 1);
    out[n - 1] = 0;
    return 1;
}

/* If the last path segment is a JWT (xxx.yyy.zzz), look for a file name in its payload. */
static int name_from_jwt(const WCHAR *seg, WCHAR *out, size_t n)
{
    const WCHAR *d1 = wcschr(seg, L'.');
    if (!d1 || wcsncmp(seg, L"eyJ", 3) != 0) return 0;
    const WCHAR *d2 = wcschr(d1 + 1, L'.');
    if (!d2) return 0;
    size_t len = (size_t)(d2 - d1 - 1);
    if (len < 8 || len > 6000) return 0;
    char *dec = (char *)malloc(len + 4);
    if (!dec) return 0;
    size_t o = 0;
    unsigned acc = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++) {
        int v = b64url_val((char)d1[1 + i]);
        if (v < 0) { if (d1[1 + i] == L'=') break; free(dec); return 0; }
        acc = (acc << 6) | (unsigned)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; dec[o++] = (char)((acc >> bits) & 0xFF); }
    }
    dec[o] = 0;
    int ok = dec[0] == '{' && json_pick_filename(dec, out, n);
    free(dec);
    return ok;
}

/* Windows rejects names longer than 255 characters; shorten but keep the extension. */
#define MAX_NAME_LEN 150
static void limit_name(WCHAR *name)
{
    size_t L = wcslen(name);
    if (L <= MAX_NAME_LEN) return;
    WCHAR ext[16] = L"";
    WCHAR *dot = wcsrchr(name, L'.');
    if (dot && wcslen(dot) < 12) wcscpy(ext, dot);
    name[MAX_NAME_LEN - wcslen(ext)] = 0;
    /* no trailing spaces or dots */
    size_t k = wcslen(name);
    while (k && (name[k - 1] == L' ' || name[k - 1] == L'.')) name[--k] = 0;
    wcscat(name, ext);
}

/* Add an extension from Content-Type when the name has none. */
static void add_ext_from_mime(WCHAR *name, size_t n, const WCHAR *ctype)
{
    static const WCHAR *map[][2] = {
        { L"application/zip", L".zip" }, { L"application/x-zip-compressed", L".zip" },
        { L"application/x-rar", L".rar" }, { L"application/vnd.rar", L".rar" }, { L"application/x-rar-compressed", L".rar" },
        { L"application/x-7z-compressed", L".7z" }, { L"application/pdf", L".pdf" },
        { L"application/x-msdownload", L".exe" }, { L"application/x-msi", L".msi" },
        { L"application/vnd.android.package-archive", L".apk" }, { L"application/x-iso9660-image", L".iso" },
        { L"video/mp4", L".mp4" }, { L"video/x-matroska", L".mkv" }, { L"video/webm", L".webm" },
        { L"audio/mpeg", L".mp3" }, { L"image/jpeg", L".jpg" }, { L"image/png", L".png" },
        { L"application/gzip", L".gz" }, { L"application/x-tar", L".tar" },
    };
    if (!ctype || !ctype[0] || has_extension(name)) return;
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (!_wcsnicmp(ctype, map[i][0], wcslen(map[i][0]))) {
            if (wcslen(name) + wcslen(map[i][1]) < n) wcscat(name, map[i][1]);
            return;
        }
    }
}

static void name_from_path(const WCHAR *path, WCHAR *out, size_t n)
{
    WCHAR tmp[4096];
    wcsncpy(tmp, path, ARRAYSIZE(tmp) - 1);
    tmp[ARRAYSIZE(tmp) - 1] = 0;
    WCHAR *q = wcschr(tmp, L'?'); if (q) *q = 0;
    q = wcschr(tmp, L'#');        if (q) *q = 0;
    WCHAR *s = wcsrchr(tmp, L'/');
    s = s ? s + 1 : tmp;
    if (!*s) { wcsncpy(out, L"download.bin", n); return; }
    if (name_from_jwt(s, out, n)) { dl_sanitize_name(out); limit_name(out); return; }
    url_decode(s, out, n);
    dl_sanitize_name(out);
    limit_name(out);
}

void name_from_url_str(const WCHAR *url, WCHAR *out, size_t n)
{
    UrlParts u;
    if (parse_url(url, &u)) name_from_path(u.path, out, n);
    else wcsncpy(out, L"download.bin", n);
}

static int name_from_disposition(const WCHAR *cd, WCHAR *out, size_t n)
{
    const WCHAR *p = wcsstr(cd, L"filename*=");
    WCHAR tmp[1024];
    size_t i = 0;
    if (p) {
        p += 10;
        const WCHAR *q = wcsstr(p, L"''");
        if (q) p = q + 2;
    } else {
        p = wcsstr(cd, L"filename=");
        if (!p) return 0;
        p += 9;
        if (*p == L'"') p++;
    }
    while (*p && *p != L'"' && *p != L';' && i < ARRAYSIZE(tmp) - 1) tmp[i++] = *p++;
    tmp[i] = 0;
    url_decode(tmp, out, n);
    dl_sanitize_name(out);
    limit_name(out);
    return out[0] != 0;
}

void dl_part_path(Download *d, WCHAR *out, size_t n)  { swprintf(out, n, L"%ls.part", d->path); }
void dl_state_path(Download *d, WCHAR *out, size_t n) { swprintf(out, n, L"%ls.dao", d->path); }

static int file_exists(const WCHAR *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }

/* Pick a free path: name.ext, name (1).ext, ... */
static void make_unique_path(Download *d)
{
    WCHAR base[MAX_PATH], ext[64] = L"", part[MAX_PATH * 2 + 8];
    wcsncpy(base, d->name, MAX_PATH - 1);
    base[MAX_PATH - 1] = 0;
    WCHAR *dot = wcsrchr(base, L'.');
    if (dot && dot != base && wcslen(dot) < 60) { wcscpy(ext, dot); *dot = 0; }

    size_t L = wcslen(d->folder);
    const WCHAR *sep = (L && (d->folder[L - 1] == L'\\' || d->folder[L - 1] == L'/')) ? L"" : L"\\";
    for (int i = 0; i < 1000; i++) {
        if (i == 0) swprintf(d->path, ARRAYSIZE(d->path), L"%ls%ls%ls%ls", d->folder, sep, base, ext);
        else        swprintf(d->path, ARRAYSIZE(d->path), L"%ls%ls%ls (%d)%ls", d->folder, sep, base, i, ext);
        swprintf(part, ARRAYSIZE(part), L"%ls.part", d->path);
        if (!file_exists(d->path) && !file_exists(part)) break;
    }
    const WCHAR *s = wcsrchr(d->path, L'\\');
    wcsncpy(d->name, s ? s + 1 : d->path, MAX_PATH - 1);
}

/* ------------------------------------------------------------------ */
/*                              HTTP                                   */
/* ------------------------------------------------------------------ */
typedef struct { HINTERNET conn, req; } HttpReq;

/* With slot >= 0 the request handle is registered so dl_pause() can abort it. */
static void add_header(HINTERNET req, const WCHAR *name, const WCHAR *value)
{
    size_t n = wcslen(name) + wcslen(value) + 4;
    WCHAR *h = (WCHAR *)malloc(n * sizeof(WCHAR));
    if (!h) return;
    swprintf(h, n, L"%ls: %ls", name, value);
    WinHttpAddRequestHeaders(req, h, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    free(h);
}

/* Replay browser headers so links that need a login session keep working. */
static void add_browser_headers(Download *d, const UrlParts *u, HINTERNET req)
{
    if (d->ua && d->ua[0])           add_header(req, L"User-Agent", d->ua);
    if (d->referer && d->referer[0]) add_header(req, L"Referer", d->referer);
    if (d->cookie && d->cookie[0] && !_wcsicmp(d->cookie_host, u->host))
        add_header(req, L"Cookie", d->cookie);
}

/* Resolve a Location header against the current URL. */
static int resolve_location(const UrlParts *cur, const WCHAR *loc, UrlParts *out)
{
    WCHAR url[8192 + 600];
    const WCHAR *scheme = cur->secure ? L"https" : L"http";
    INTERNET_PORT def = cur->secure ? 443 : 80;
    WCHAR origin[600];
    if (cur->port == def) swprintf(origin, ARRAYSIZE(origin), L"%ls://%ls", scheme, cur->host);
    else                  swprintf(origin, ARRAYSIZE(origin), L"%ls://%ls:%u", scheme, cur->host, cur->port);

    if (!_wcsnicmp(loc, L"http://", 7) || !_wcsnicmp(loc, L"https://", 8)) {
        wcsncpy(url, loc, ARRAYSIZE(url) - 1);
        url[ARRAYSIZE(url) - 1] = 0;
    } else if (loc[0] == L'/' && loc[1] == L'/') {
        swprintf(url, ARRAYSIZE(url), L"%ls:%ls", scheme, loc);
    } else if (loc[0] == L'/') {
        swprintf(url, ARRAYSIZE(url), L"%ls%ls", origin, loc);
    } else {
        WCHAR dir[8192];
        wcsncpy(dir, cur->path, ARRAYSIZE(dir) - 1);
        dir[ARRAYSIZE(dir) - 1] = 0;
        WCHAR *q = wcschr(dir, L'?');
        if (q) *q = 0;
        WCHAR *slash = wcsrchr(dir, L'/');
        if (slash) slash[1] = 0; else wcscpy(dir, L"/");
        swprintf(url, ARRAYSIZE(url), L"%ls%ls%ls", origin, dir, loc);
    }
    return parse_url(url, out);
}

static void http_close(Download *d, int slot, HttpReq *r);

/* One request, redirects disabled. Returns the HTTP status or 0 on failure. */
static DWORD http_send(Download *d, int slot, const UrlParts *u, LONGLONG from, LONGLONG to, HttpReq *r)
{
    DWORD status = 0, sz = sizeof(status);
    r->conn = r->req = NULL;

    r->conn = WinHttpConnect(g_session, u->host, u->port, 0);
    if (!r->conn) return 0;
    r->req = WinHttpOpenRequest(r->conn, L"GET", u->path, NULL, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                (u->secure ? WINHTTP_FLAG_SECURE : 0) | WINHTTP_FLAG_REFRESH);
    if (!r->req) return 0;

    if (slot >= 0) {
        EnterCriticalSection(&d->lock);
        if (d->stop) {
            LeaveCriticalSection(&d->lock);
            WinHttpCloseHandle(r->req);
            r->req = NULL;
            return 0;
        }
        d->reqs[slot] = r->req;
        LeaveCriticalSection(&d->lock);
    }

    /* Redirects are followed manually so cookies never reach another host. */
    DWORD feature = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(r->req, WINHTTP_OPTION_DISABLE_FEATURE, &feature, sizeof(feature));

    if (from >= 0) {
        WCHAR hdr[128];
        if (to >= 0) swprintf(hdr, 128, L"Range: bytes=%lld-%lld", from, to);
        else         swprintf(hdr, 128, L"Range: bytes=%lld-", from);
        WinHttpAddRequestHeaders(r->req, hdr, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    }
    WinHttpAddRequestHeaders(r->req, L"Accept-Encoding: identity", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    add_browser_headers(d, u, r->req);

    if (!WinHttpSendRequest(r->req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(r->req, NULL))
        return 0;
    WinHttpQueryHeaders(r->req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
    return status;
}

#define MAX_REDIRECTS 10

/* GET with an optional Range header, following redirects. The URL that
   answered is stored in *final when given. */
static DWORD http_get(Download *d, int slot, LONGLONG from, LONGLONG to, HttpReq *r, UrlParts *final)
{
    UrlParts cur = d->u;
    WCHAR loc[8192];

    for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
        DWORD st = http_send(d, slot, &cur, from, to, r);
        int redirect = st == 301 || st == 302 || st == 303 || st == 307 || st == 308;
        if (!redirect) {
            if (final) *final = cur;
            return st;
        }
        DWORD sz = sizeof(loc);
        UrlParts next;
        int ok = WinHttpQueryHeaders(r->req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                     loc, &sz, WINHTTP_NO_HEADER_INDEX)
                 && resolve_location(&cur, loc, &next)
                 && !(cur.secure && !next.secure);              /* no HTTPS -> HTTP downgrade */
        http_close(d, slot, r);
        if (!ok) break;
        cur = next;
    }
    SetLastError(ERROR_WINHTTP_REDIRECT_FAILED);
    return 0;
}

static void http_close(Download *d, int slot, HttpReq *r)
{
    if (r->req) {
        int mine = 1;
        if (slot >= 0) {
            EnterCriticalSection(&d->lock);
            mine = (d->reqs[slot] == r->req);    /* dl_pause() may have closed it already */
            if (mine) d->reqs[slot] = NULL;
            LeaveCriticalSection(&d->lock);
        }
        if (mine) WinHttpCloseHandle(r->req);
    }
    if (r->conn) WinHttpCloseHandle(r->conn);
    r->req = r->conn = NULL;
}

static int query_header(HINTERNET req, DWORD what, WCHAR *out, DWORD outChars)
{
    DWORD sz = outChars * sizeof(WCHAR);
    out[0] = 0;
    return WinHttpQueryHeaders(req, what, WINHTTP_HEADER_NAME_BY_INDEX, out, &sz, WINHTTP_NO_HEADER_INDEX);
}

/* Probe the URL: size, range support and file name. */
static int probe(Download *d, int assign_path)
{
    HttpReq r;
    WCHAR buf[8192];
    WCHAR name[MAX_PATH] = L"";
    DWORD st;
    LONGLONG total = -1;
    int ranges = 0;

    if (!parse_url(d->url, &d->u)) { set_error(d, ERR_BAD_URL, 0); return 0; }
    UrlParts final_url;
    st = http_get(d, 0, 0, 0, &r, &final_url);
    if (st == 0) {
        DWORD e = GetLastError();
        http_close(d, 0, &r);
        if (d->stop) return 0;
        set_error(d, ERR_CONNECT, e);
        return 0;
    }
    if (st >= 400) {
        http_close(d, 0, &r);
        set_error(d, ERR_HTTP, st);
        return 0;
    }

    d->u = final_url;
    if (st == 206 && query_header(r.req, WINHTTP_QUERY_CONTENT_RANGE, buf, ARRAYSIZE(buf))) {
        WCHAR *slash = wcschr(buf, L'/');
        if (slash && slash[1] != L'*') { total = _wtoi64(slash + 1); ranges = total > 0; }
    }
    if (!ranges) {
        total = -1;
        if (st == 200 && query_header(r.req, WINHTTP_QUERY_CONTENT_LENGTH, buf, ARRAYSIZE(buf)))
            total = _wtoi64(buf);
    }
    if (query_header(r.req, WINHTTP_QUERY_CONTENT_DISPOSITION, buf, ARRAYSIZE(buf)))
        name_from_disposition(buf, name, MAX_PATH);
    if (!name[0]) {
        /* prefer a name embedded in the original (pre-redirect) URL */
        UrlParts orig;
        WCHAR alt[MAX_PATH] = L"";
        if (parse_url(d->url, &orig)) {
            const WCHAR *seg = wcsrchr(orig.path, L'/');
            if (seg && name_from_jwt(seg + 1, alt, MAX_PATH)) { dl_sanitize_name(alt); limit_name(alt); wcscpy(name, alt); }
        }
        if (!name[0]) name_from_path(d->u.path, name, MAX_PATH);
        if (query_header(r.req, WINHTTP_QUERY_CONTENT_TYPE, buf, ARRAYSIZE(buf)))
            add_ext_from_mime(name, MAX_PATH, buf);
    }
    http_close(d, 0, &r);

    EnterCriticalSection(&d->lock);
    d->total  = total;
    d->ranges = ranges;
    if (!d->name_fixed || !d->name[0]) wcsncpy(d->name, name, MAX_PATH - 1);
    dl_sanitize_name(d->name);
    limit_name(d->name);
    if (assign_path && d->path[0]) {
        /* an old path with an over-long name that never got a .part file: pick a new one */
        const WCHAR *fn = wcsrchr(d->path, L'\\');
        WCHAR pp[MAX_PATH * 2 + 8];
        dl_part_path(d, pp, ARRAYSIZE(pp));
        if (fn && wcslen(fn + 1) > MAX_NAME_LEN + 10 && !file_exists(pp)) {
            d->path[0] = 0;
            if (!d->name_fixed) wcsncpy(d->name, name, MAX_PATH - 1);
            limit_name(d->name);
        }
    }
    if (assign_path && !d->path[0]) make_unique_path(d);
    LeaveCriticalSection(&d->lock);
    return 1;
}

/* ------------------------------------------------------------------ */
/*                         Resume state (.dao)                         */
/* ------------------------------------------------------------------ */
static void save_state(Download *d)
{
    WCHAR sp[MAX_PATH * 2 + 8], tmp[MAX_PATH * 2 + 16];
    if (!d->ranges) return;
    dl_state_path(d, sp, ARRAYSIZE(sp));
    swprintf(tmp, ARRAYSIZE(tmp), L"%ls.tmp", sp);
    FILE *f = _wfopen(tmp, L"wb");
    if (!f) return;

    EnterCriticalSection(&d->lock);
    unsigned magic = STATE_MAGIC, ulen = (unsigned)wcslen(d->url);
    int cnt = 0;
    fwrite(&magic, sizeof magic, 1, f);
    fwrite(&ulen, sizeof ulen, 1, f);
    fwrite(d->url, sizeof(WCHAR), ulen, f);
    fwrite(&d->total, sizeof d->total, 1, f);
    for (int i = 0; i < d->nseg; i++) if (d->seg[i].pos < d->seg[i].end) cnt++;
    fwrite(&cnt, sizeof cnt, 1, f);
    for (int i = 0; i < d->nseg; i++) {
        if (d->seg[i].pos >= d->seg[i].end) continue;
        fwrite(&d->seg[i].start, sizeof(LONGLONG), 1, f);
        fwrite(&d->seg[i].pos,   sizeof(LONGLONG), 1, f);
        fwrite(&d->seg[i].end,   sizeof(LONGLONG), 1, f);
    }
    LeaveCriticalSection(&d->lock);
    fclose(f);
    MoveFileExW(tmp, sp, MOVEFILE_REPLACE_EXISTING);
}

int dl_load_state(Download *d)
{
    WCHAR sp[MAX_PATH * 2 + 8];
    unsigned magic = 0, ulen = 0;
    LONGLONG total = 0;
    int cnt = 0, ok = 0;
    Segment tmp[MAX_SEGMENTS];
    WCHAR url[4096];

    dl_state_path(d, sp, ARRAYSIZE(sp));
    FILE *f = _wfopen(sp, L"rb");
    if (!f) return 0;
    if (fread(&magic, sizeof magic, 1, f) != 1 || magic != STATE_MAGIC) goto done;
    if (fread(&ulen, sizeof ulen, 1, f) != 1 || ulen >= ARRAYSIZE(url)) goto done;
    if (fread(url, sizeof(WCHAR), ulen, f) != ulen) goto done;
    if (fread(&total, sizeof total, 1, f) != 1 || total <= 0) goto done;
    if (fread(&cnt, sizeof cnt, 1, f) != 1 || cnt < 0 || cnt > MAX_SEGMENTS) goto done;
    for (int i = 0; i < cnt; i++) {
        Segment s = { 0 };
        if (fread(&s.start, sizeof(LONGLONG), 1, f) != 1 ||
            fread(&s.pos,   sizeof(LONGLONG), 1, f) != 1 ||
            fread(&s.end,   sizeof(LONGLONG), 1, f) != 1) goto done;
        if (s.start < 0 || s.pos < s.start || s.end > total || s.pos > s.end) goto done;
        tmp[i] = s;
    }
    ok = 1;
done:
    fclose(f);
    if (ok) {
        EnterCriticalSection(&d->lock);
        memcpy(d->seg, tmp, sizeof(Segment) * cnt);
        d->nseg   = cnt;
        d->total  = total;
        d->ranges = 1;
        LeaveCriticalSection(&d->lock);
    }
    return ok;
}

static LONGLONG remaining_locked(Download *d)
{
    LONGLONG r = 0;
    for (int i = 0; i < d->nseg; i++)
        if (d->seg[i].end > d->seg[i].pos) r += d->seg[i].end - d->seg[i].pos;
    return r;
}

LONGLONG dl_done_bytes(Download *d)
{
    LONGLONG v;
    EnterCriticalSection(&d->lock);
    if (d->status == ST_DONE)              v = d->total;
    else if (d->ranges && d->total > 0 && d->nseg > 0) v = d->total - remaining_locked(d);
    else if (d->ranges && d->total > 0)    v = 0;
    else                                   v = d->nseg ? d->seg[0].pos : 0;
    LeaveCriticalSection(&d->lock);
    return v;
}

/* ------------------------------------------------------------------ */
/*                  Segment scheduling and worker threads              */
/* ------------------------------------------------------------------ */
static int pick_segment(Download *d)   /* caller holds d->lock */
{
    for (int i = 0; i < d->nseg; i++)
        if (!d->seg[i].active && (d->seg[i].end < 0 || d->seg[i].pos < d->seg[i].end)) {
            d->seg[i].active = 1;
            return i;
        }
    if (!d->ranges || d->nseg >= MAX_SEGMENTS) return -1;

    int best = -1;
    LONGLONG bestRem = 0;
    for (int i = 0; i < d->nseg; i++) {
        LONGLONG rem = d->seg[i].end - d->seg[i].pos;
        if (d->seg[i].active && rem > bestRem) { bestRem = rem; best = i; }
    }
    if (best < 0 || bestRem < 2 * MIN_SPLIT_SIZE) return -1;

    LONGLONG mid = d->seg[best].pos + bestRem / 2;
    Segment ns = { mid, mid, d->seg[best].end, 1 };
    d->seg[best].end = mid;
    d->seg[d->nseg] = ns;
    return d->nseg++;
}

static int write_at(Download *d, LONGLONG off, const void *data, DWORD len)
{
    OVERLAPPED ov;
    DWORD written = 0;
    memset(&ov, 0, sizeof(ov));
    ov.Offset     = (DWORD)(off & 0xFFFFFFFF);
    ov.OffsetHigh = (DWORD)(off >> 32);
    return WriteFile(d->file, data, len, &written, &ov) && written == len;
}

/* Returns 1 = segment done, 0 = transient failure, -1 = permanent error. */
static int download_segment(Download *d, int slot, int si, char *buf)
{
    LONGLONG from, to;
    HttpReq r;
    DWORD st;
    int result = 0;

    EnterCriticalSection(&d->lock);
    from = d->seg[si].pos;
    to   = d->seg[si].end;
    LeaveCriticalSection(&d->lock);
    if (to >= 0 && from >= to) return 1;

    st = d->ranges ? http_get(d, slot, from, to - 1, &r, NULL) : http_get(d, slot, -1, -1, &r, NULL);
    if (st == 0) { http_close(d, slot, &r); return 0; }
    if ((d->ranges && st != 206) || (!d->ranges && st != 200)) {
        http_close(d, slot, &r);
        if (st >= 400 && st < 500 && st != 408 && st != 429) {
            set_error(d, ERR_HTTP, st);
            return -1;
        }
        return 0;
    }

    InterlockedIncrement(&d->conns);
    for (;;) {
        DWORD got = 0;
        if (d->stop) break;
        if (!WinHttpReadData(r.req, buf, BUFFER_SIZE, &got)) break;
        if (got == 0) {
            EnterCriticalSection(&d->lock);
            if (d->seg[si].end < 0) {
                d->seg[si].end = d->seg[si].pos;
                d->total = d->seg[si].pos;
                result = 1;
            } else {
                result = d->seg[si].pos >= d->seg[si].end;
            }
            LeaveCriticalSection(&d->lock);
            break;
        }

        EnterCriticalSection(&d->lock);
        LONGLONG pos = d->seg[si].pos, end = d->seg[si].end;
        LeaveCriticalSection(&d->lock);
        if (end >= 0 && pos + got > end) got = (DWORD)(end - pos);
        if (got && !write_at(d, pos, buf, got)) {
            set_error(d, ERR_DISK_WRITE, GetLastError());
            result = -1;
            break;
        }

        EnterCriticalSection(&d->lock);
        d->seg[si].pos += got;
        int done = d->seg[si].end >= 0 && d->seg[si].pos >= d->seg[si].end;
        LeaveCriticalSection(&d->lock);
        InterlockedExchangeAdd64(&d->session_bytes, got);
        if (done) { result = 1; break; }
    }
    InterlockedDecrement(&d->conns);
    http_close(d, slot, &r);
    return result;
}

typedef struct { Download *d; int slot; } WorkerArg;

static DWORD WINAPI worker(LPVOID p)
{
    WorkerArg *a = (WorkerArg *)p;
    Download *d = a->d;
    int slot = a->slot, si = -1, fails = 0;
    char *buf = (char *)malloc(BUFFER_SIZE);
    free(a);
    if (!buf) return 1;

    while (!d->stop) {
        if (si < 0) {
            EnterCriticalSection(&d->lock);
            si = pick_segment(d);
            LeaveCriticalSection(&d->lock);
            if (si < 0) break;
            fails = 0;
        }
        LONGLONG before = d->seg[si].pos;
        int r = download_segment(d, slot, si, buf);
        if (r == 1) {
            EnterCriticalSection(&d->lock);
            d->seg[si].active = 0;
            LeaveCriticalSection(&d->lock);
            si = -1;
            continue;
        }
        if (r == -1) { InterlockedExchange(&d->stop, 1); break; }
        if (d->stop) break;

        if (d->seg[si].pos > before) fails = 0;
        if (!d->ranges) {                          /* no range support: restart from zero */
            EnterCriticalSection(&d->lock);
            InterlockedExchangeAdd64(&d->session_bytes, -(d->seg[si].pos - d->seg[si].start));
            d->seg[si].pos = d->seg[si].start;
            LeaveCriticalSection(&d->lock);
        }
        if (++fails > MAX_RETRIES) { set_error(d, ERR_RETRIES, 0); break; }
        DWORD wait = 500u * (DWORD)(fails < 10 ? fails : 10);
        for (DWORD t = 0; t < wait && !d->stop; t += 100) Sleep(100);
    }
    if (si >= 0) {
        EnterCriticalSection(&d->lock);
        d->seg[si].active = 0;
        LeaveCriticalSection(&d->lock);
    }
    free(buf);
    return 0;
}

/* ------------------------------------------------------------------ */
/*                            Manager thread                           */
/* ------------------------------------------------------------------ */
static void finish(Download *d, DlStatus s)
{
    WCHAR pp[MAX_PATH * 2 + 8], sp[MAX_PATH * 2 + 8];
    if (d->remove_after == 2 && d->path[0]) {
        dl_part_path(d, pp, ARRAYSIZE(pp));
        dl_state_path(d, sp, ARRAYSIZE(sp));
        DeleteFileW(pp);
        DeleteFileW(sp);
        if (s == ST_DONE) DeleteFileW(d->path);
    }
    set_status(d, s);
}

static DWORD WINAPI manager(LPVOID p)
{
    Download *d = (Download *)p;
    WCHAR pp[MAX_PATH * 2 + 8], sp[MAX_PATH * 2 + 8];
    LONGLONG prev_total = d->total;
    int resumed = 0;

    set_status(d, ST_CONNECTING);
    int ok = 0;
    for (int attempt = 0; attempt < 5 && !d->stop; attempt++) {
        if (attempt) for (int t = 0; t < 20 * attempt && !d->stop; t++) Sleep(100);
        if ((ok = probe(d, 1))) break;
        if (d->err == ERR_BAD_URL || (d->err == ERR_HTTP && d->err_detail >= 400 && d->err_detail < 500)) break;
    }
    if (!ok) { finish(d, d->stop ? ST_PAUSED : ST_ERROR); return 0; }
    if (d->stop)   { finish(d, ST_PAUSED); return 0; }

    dl_part_path(d, pp, ARRAYSIZE(pp));
    dl_state_path(d, sp, ARRAYSIZE(sp));

    if (d->ranges && file_exists(pp)) {
        LONGLONG now_total = d->total;
        if (dl_load_state(d) && d->total == now_total) resumed = 1;
        else { EnterCriticalSection(&d->lock); d->total = now_total; d->nseg = 0; d->ranges = 1; LeaveCriticalSection(&d->lock); }
    }
    (void)prev_total;

    d->file = CreateFileW(pp, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                          resumed ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (d->file == INVALID_HANDLE_VALUE) {
        set_error(d, ERR_CREATE_FILE, GetLastError());
        finish(d, ST_ERROR);
        return 0;
    }

    int nconn = d->nconn < 1 ? 1 : (d->nconn > MAX_CONNECTIONS ? MAX_CONNECTIONS : d->nconn);
    if (!resumed) {
        EnterCriticalSection(&d->lock);
        if (d->ranges) {
            LARGE_INTEGER li; li.QuadPart = d->total;
            if (!SetFilePointerEx(d->file, li, NULL, FILE_BEGIN) || !SetEndOfFile(d->file)) {
                LeaveCriticalSection(&d->lock);
                CloseHandle(d->file); d->file = INVALID_HANDLE_VALUE;
                DeleteFileW(pp);
                set_error(d, ERR_DISK_FULL, 0);
                finish(d, ST_ERROR);
                return 0;
            }
            int n = nconn;
            while (n > 1 && d->total / n < 512 * 1024) n--;
            LONGLONG chunk = d->total / n;
            for (int i = 0; i < n; i++) {
                d->seg[i].start = d->seg[i].pos = chunk * i;
                d->seg[i].end = (i == n - 1) ? d->total : chunk * (i + 1);
                d->seg[i].active = 0;
            }
            d->nseg = n;
        } else {
            d->seg[0].start = d->seg[0].pos = 0;
            d->seg[0].end = d->total > 0 ? d->total : -1;
            d->seg[0].active = 0;
            d->nseg = 1;
        }
        LeaveCriticalSection(&d->lock);
        save_state(d);
    }
    if (!d->ranges) nconn = 1;

    set_error(d, ERR_NONE, 0);
    set_status(d, ST_DOWNLOADING);

    HANDLE th[MAX_CONNECTIONS];
    int nth = 0;
    for (int i = 0; i < nconn; i++) {
        WorkerArg *a = (WorkerArg *)malloc(sizeof(WorkerArg));
        if (!a) break;
        a->d = d; a->slot = i;
        th[nth] = CreateThread(NULL, 0, worker, a, 0, NULL);
        if (th[nth]) nth++; else free(a);
    }

    while (WaitForMultipleObjects(nth, th, TRUE, 2000) == WAIT_TIMEOUT)
        save_state(d);
    for (int i = 0; i < nth; i++) CloseHandle(th[i]);

    int complete = 1;
    EnterCriticalSection(&d->lock);
    for (int i = 0; i < d->nseg; i++)
        if (d->seg[i].end < 0 || d->seg[i].pos < d->seg[i].end) complete = 0;
    LeaveCriticalSection(&d->lock);

    if (complete && !d->ranges && d->total > 0) {
        LARGE_INTEGER li; li.QuadPart = d->total;
        SetFilePointerEx(d->file, li, NULL, FILE_BEGIN);
        SetEndOfFile(d->file);
    }
    CloseHandle(d->file);
    d->file = INVALID_HANDLE_VALUE;

    if (complete) {
        if (!MoveFileExW(pp, d->path, MOVEFILE_REPLACE_EXISTING)) {
            set_error(d, ERR_RENAME, GetLastError());
            finish(d, ST_ERROR);
            return 0;
        }
        DeleteFileW(sp);
        finish(d, ST_DONE);
        return 0;
    }

    save_state(d);
    if (d->err != ERR_NONE && !(d->stop && d->status == ST_PAUSING)) finish(d, ST_ERROR);
    else finish(d, ST_PAUSED);
    return 0;
}

/* ------------------------------------------------------------------ */
/*                              Public API                             */
/* ------------------------------------------------------------------ */
Download *dl_create(const WCHAR *url, const WCHAR *folder, int nconn, HWND notify)
{
    Download *d = (Download *)calloc(1, sizeof(Download));
    if (!d) return NULL;
    InitializeCriticalSection(&d->lock);
    wcsncpy(d->url, url, ARRAYSIZE(d->url) - 1);
    wcsncpy(d->folder, folder, MAX_PATH - 1);
    d->nconn = nconn;
    d->total = -1;
    d->status = ST_QUEUED;
    d->file = INVALID_HANDLE_VALUE;
    d->notify_hwnd = notify;
    name_from_url_str(url, d->name, MAX_PATH);
    return d;
}

static WCHAR *dupw(const WCHAR *s)
{
    if (!s || !s[0]) return NULL;
    size_t n = wcslen(s) + 1;
    WCHAR *p = (WCHAR *)malloc(n * sizeof(WCHAR));
    if (p) memcpy(p, s, n * sizeof(WCHAR));
    return p;
}

void dl_set_headers(Download *d, const WCHAR *cookie, const WCHAR *referer, const WCHAR *ua)
{
    /* duplicate first: callers may pass the current values back in */
    WCHAR *c = dupw(cookie), *r = dupw(referer), *u = dupw(ua);
    dl_clear_cookie(d);
    free(d->referer);
    free(d->ua);
    d->cookie  = c;
    d->referer = r;
    d->ua      = u;
    UrlParts p;
    if (parse_url(d->url, &p)) wcsncpy(d->cookie_host, p.host, ARRAYSIZE(d->cookie_host) - 1);
}

void dl_clear_cookie(Download *d)
{
    if (d->cookie) {
        SecureZeroMemory(d->cookie, wcslen(d->cookie) * sizeof(WCHAR));
        free(d->cookie);
        d->cookie = NULL;
    }
}

int dl_fetch_info(Download *d)
{
    return probe(d, 0);
}

int dl_is_running(Download *d)
{
    if (!d->thread) return 0;
    if (WaitForSingleObject(d->thread, 0) == WAIT_OBJECT_0) {
        CloseHandle(d->thread);
        d->thread = NULL;
        return 0;
    }
    return 1;
}

void dl_start(Download *d)
{
    if (dl_is_running(d) || d->status == ST_DONE) return;
    d->stop = 0;
    d->err = ERR_NONE;
    d->err_detail = 0;
    d->ui_last_bytes = d->session_bytes;
    d->ui_last_tick = GetTickCount();
    d->speed = 0;
    d->status = ST_CONNECTING;
    d->thread = CreateThread(NULL, 0, manager, d, 0, NULL);
}

void dl_pause(Download *d)
{
    if (!dl_is_running(d)) return;
    InterlockedExchange(&d->stop, 1);
    InterlockedExchange(&d->status, ST_PAUSING);
    EnterCriticalSection(&d->lock);
    for (int i = 0; i < MAX_CONNECTIONS; i++)
        if (d->reqs[i]) { WinHttpCloseHandle(d->reqs[i]); d->reqs[i] = NULL; }   /* abort in-flight reads */
    LeaveCriticalSection(&d->lock);
}

void dl_wait(Download *d, DWORD ms)
{
    if (d->thread) {
        WaitForSingleObject(d->thread, ms);
        dl_is_running(d);
    }
}

void dl_free(Download *d)
{
    if (!d) return;
    if (d->thread) { WaitForSingleObject(d->thread, INFINITE); CloseHandle(d->thread); }
    DeleteCriticalSection(&d->lock);
    dl_clear_cookie(d);
    free(d->referer);
    free(d->ua);
    free(d);
}
