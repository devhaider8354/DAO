/*
 * engine.h - Download engine: multi-connection, resumable, dynamic segment splitting.
 */
#ifndef DAO_ENGINE_H
#define DAO_ENGINE_H

#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <winhttp.h>

#define MAX_CONNECTIONS   32
#define MAX_SEGMENTS      512
#define DEFAULT_CONNS     8

typedef enum {
    ST_QUEUED = 0,
    ST_CONNECTING,
    ST_DOWNLOADING,
    ST_PAUSING,
    ST_PAUSED,
    ST_DONE,
    ST_ERROR
} DlStatus;

typedef enum {
    ERR_NONE = 0,
    ERR_BAD_URL,
    ERR_CONNECT,        /* detail = Win32 / WinHTTP error code */
    ERR_HTTP,           /* detail = HTTP status */
    ERR_DISK_WRITE,
    ERR_RETRIES,
    ERR_CREATE_FILE,
    ERR_DISK_FULL,
    ERR_RENAME
} DlError;

typedef struct {
    LONGLONG start, pos, end;   /* end is exclusive, -1 = unknown */
    int active;
} Segment;

typedef struct {
    WCHAR host[512];
    WCHAR path[8192];
    INTERNET_PORT port;
    int secure;
} UrlParts;

typedef struct Download {
    WCHAR url[4096];
    WCHAR folder[MAX_PATH];
    WCHAR name[MAX_PATH];
    WCHAR path[MAX_PATH * 2];     /* final file path */
    int   nconn;
    int   name_fixed;             /* name chosen by the user / browser, don't override */
    WCHAR *cookie;                /* optional, forwarded from the browser */
    WCHAR *referer;
    WCHAR *ua;
    WCHAR cookie_host[512];       /* cookies are only sent to this host */

    /* guarded by lock */
    CRITICAL_SECTION lock;
    volatile LONG status;
    DlError  err;
    DWORD    err_detail;
    LONGLONG total;               /* -1 = unknown */
    int      ranges;              /* server supports byte ranges */
    Segment  seg[MAX_SEGMENTS];
    int      nseg;
    HINTERNET reqs[MAX_CONNECTIONS];

    /* runtime */
    volatile LONG stop;
    volatile LONG remove_after;   /* 1 = remove from list, 2 = also delete files */
    volatile LONG conns;
    volatile LONGLONG session_bytes;
    HANDLE   thread;
    HANDLE   file;
    UrlParts u;

    /* UI bookkeeping */
    LONGLONG ui_last_bytes;
    DWORD    ui_last_tick;
    LONGLONG speed;
    HWND     notify_hwnd;
} Download;

#define WM_DL_CHANGED (WM_APP + 1)   /* lParam = Download* */

void      engine_init(void);
void      engine_shutdown(void);
Download *dl_create(const WCHAR *url, const WCHAR *folder, int nconn, HWND notify);
void      dl_start(Download *d);
void      dl_pause(Download *d);
int       dl_is_running(Download *d);
void      dl_wait(Download *d, DWORD ms);
void      dl_free(Download *d);
LONGLONG  dl_done_bytes(Download *d);
int       dl_load_state(Download *d);          /* read the .dao resume file */
void      dl_part_path(Download *d, WCHAR *out, size_t n);
void      dl_state_path(Download *d, WCHAR *out, size_t n);
void      dl_set_headers(Download *d, const WCHAR *cookie, const WCHAR *referer, const WCHAR *ua);
int       dl_fetch_info(Download *d);           /* probe only: name, size, range support */
void      name_from_url_str(const WCHAR *url, WCHAR *out, size_t n);

void      fmt_size(LONGLONG b, WCHAR *out, size_t n);
void      fmt_time(LONGLONG s, WCHAR *out, size_t n);

#endif
