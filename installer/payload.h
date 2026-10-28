/* Files embedded in the installer (resource id -> install path). Keep in sync with setup.rc. */
#ifndef DAO_PAYLOAD_H
#define DAO_PAYLOAD_H

typedef struct { int id; const wchar_t *path; } PayloadFile;

static const PayloadFile g_payload[] = {
    { 1000, L"dao.exe" },
    { 1001, L"dao-host.exe" },
    { 1002, L"dao-cli.exe" },
    { 1003, L"README.md" },
    { 1004, L"LICENSE" },
    { 1005, L"chrome-extension\\manifest.json" },
    { 1006, L"chrome-extension\\background.js" },
    { 1007, L"chrome-extension\\popup.html" },
    { 1008, L"chrome-extension\\popup.css" },
    { 1009, L"chrome-extension\\popup.js" },
    { 1010, L"chrome-extension\\_locales\\en\\messages.json" },
    { 1011, L"chrome-extension\\_locales\\ar\\messages.json" },
    { 1012, L"chrome-extension\\icons\\16.png" },
    { 1013, L"chrome-extension\\icons\\32.png" },
    { 1014, L"chrome-extension\\icons\\48.png" },
    { 1015, L"chrome-extension\\icons\\128.png" },
    { 1016, L"chrome-extension\\icons\\16-off.png" },
    { 1017, L"chrome-extension\\icons\\32-off.png" },
    { 1018, L"chrome-extension\\icons\\48-off.png" },
};

#define PAYLOAD_COUNT ((int)(sizeof(g_payload) / sizeof(g_payload[0])))

#endif
