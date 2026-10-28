/*
 * i18n.h - UI string table (English / Arabic)
 */
#ifndef DAO_I18N_H
#define DAO_I18N_H

#include <wchar.h>

typedef enum {
    LANG_EN = 0,
    LANG_AR = 1
} UiLang;

typedef enum {
    /* App */
    S_APP_TITLE,
    S_APP_TAGLINE,
    S_LANG_SWITCH,
    S_LANG_RESTART_ASK,

    /* Toolbar */
    S_BTN_FOLDER,
    S_BTN_ADD,
    S_BTN_RESUME,
    S_BTN_PAUSE,
    S_BTN_DELETE,
    S_BTN_OPEN,
    S_BTN_OPENDIR,
    S_BTN_PAUSEALL,
    S_BTN_RESUMEALL,
    S_BTN_CHROME,
    S_BTN_CHROME_OK,
    S_CONNECTIONS,
    S_URL_CUE,

    /* Status */
    S_ST_QUEUED,
    S_ST_CONNECTING,
    S_ST_DOWNLOADING,
    S_ST_PAUSING,
    S_ST_PAUSED,
    S_ST_DONE,
    S_ST_ERROR,

    /* List */
    S_COL_NAME,
    S_COL_SIZE,
    S_COL_PROGRESS,
    S_COL_SPEED,
    S_COL_ETA,
    S_COL_STATUS,
    S_EMPTY_TITLE,
    S_EMPTY_HINT,

    /* Details panel / status bar */
    S_DET_NONE,
    S_DET_ERROR,
    S_DET_URL,
    S_DET_STATS_ACTIVE,
    S_DET_STATS_IDLE,
    S_SUPPORTED,
    S_NOT_SUPPORTED,
    S_UNKNOWN,
    S_QMARK,
    S_BAR_STATS,
    S_BAR_FOLDER,

    /* Context menu */
    S_MENU_RESUME,
    S_MENU_PAUSE,
    S_MENU_OPEN,
    S_MENU_OPENDIR,
    S_MENU_COPYURL,
    S_MENU_REMOVE,
    S_MENU_REMOVEFILE,

    /* Message boxes */
    S_MSG_BAD_URL,
    S_MSG_LIST_FULL,
    S_DEL_TITLE,
    S_DEL_ASK,
    S_EXIT_TITLE,
    S_EXIT_ASK,
    S_PICK_FOLDER,

    /* New download dialog */
    S_DLG_WINDOW,
    S_DLG_HEADER,
    S_DLG_FILENAME,
    S_DLG_FETCHING,
    S_DLG_SUPPORTED,
    S_DLG_INFO,
    S_DLG_SAVE_TO,
    S_DLG_CHANGE,
    S_DLG_START,
    S_DLG_LATER,
    S_DLG_CANCEL,
    S_DLG_PROBE_FAILED,

    /* Chrome setup guide */
    S_GUIDE_WINDOW,
    S_GUIDE_TITLE,
    S_GUIDE_SUBTITLE,
    S_GUIDE_STEP1,
    S_GUIDE_STEP2,
    S_GUIDE_STEP3,
    S_GUIDE_STEP1_HINT,
    S_GUIDE_STEP2_HINT,
    S_GUIDE_CONNECTED,
    S_GUIDE_WAITING,
    S_GUIDE_OPEN,
    S_GUIDE_COPY,
    S_GUIDE_DONE,
    S_GUIDE_NO_CHROME,

    /* Engine errors */
    S_ERR_BAD_URL,
    S_ERR_CONNECT,
    S_ERR_HTTP,
    S_ERR_DISK_WRITE,
    S_ERR_RETRIES,
    S_ERR_CREATE_FILE,
    S_ERR_DISK_FULL,
    S_ERR_RENAME,

    S_COUNT
} StrId;

extern UiLang g_lang;

const wchar_t *T(StrId id);
UiLang         i18n_default_lang(void);

#endif
