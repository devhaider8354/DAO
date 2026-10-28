/*
 * i18n.c - UI string table (English / Arabic)
 *
 * Format strings must keep the same argument order in both languages.
 * U+200E / U+200F (LRM / RLM) keep sizes, URLs and paths readable in RTL text.
 */
#include "i18n.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

UiLang g_lang = LANG_EN;

static const wchar_t *const g_strings[S_COUNT][2] = {
    /* App */
    [S_APP_TITLE]        = { L"DAO - Download Manager", L"DAO - مدير التحميل" },
    [S_APP_TAGLINE]      = { L"Fast multi-connection downloads with resume support",
                             L"مدير التحميل السريع  •  تحميل متعدد الاتصالات مع الاستكمال" },
    [S_LANG_SWITCH]      = { L"عربي", L"English" },
    [S_LANG_RESTART_ASK] = { L"Active downloads will be paused while DAO restarts to switch the language.\nContinue?",
                             L"سيتم إيقاف التحميلات مؤقتاً وإعادة تشغيل البرنامج لتغيير اللغة.\nهل تريد المتابعة؟" },

    /* Toolbar */
    [S_BTN_FOLDER]       = { L"Folder",           L"المجلد" },
    [S_BTN_ADD]          = { L"Add download",     L"إضافة تحميل" },
    [S_BTN_RESUME]       = { L"Resume",           L"استئناف" },
    [S_BTN_PAUSE]        = { L"Pause",            L"إيقاف" },
    [S_BTN_DELETE]       = { L"Delete",           L"حذف" },
    [S_BTN_OPEN]         = { L"Open file",        L"فتح الملف" },
    [S_BTN_OPENDIR]      = { L"Open folder",      L"فتح المجلد" },
    [S_BTN_PAUSEALL]     = { L"Pause all",        L"إيقاف الكل" },
    [S_BTN_RESUMEALL]    = { L"Resume all",       L"استئناف الكل" },
    [S_BTN_CHROME]       = { L"Connect Chrome",   L"ربط كروم" },
    [S_BTN_CHROME_OK]    = { L"Chrome connected", L"كروم مربوط" },
    [S_CONNECTIONS]      = { L"Connections",      L"الاتصالات" },
    [S_URL_CUE]          = { L"Paste a download link here...", L"ألصق رابط التحميل هنا..." },

    /* Status */
    [S_ST_QUEUED]        = { L"Queued",         L"بالانتظار" },
    [S_ST_CONNECTING]    = { L"Connecting...",  L"جارٍ الاتصال..." },
    [S_ST_DOWNLOADING]   = { L"Downloading",    L"جارٍ التحميل" },
    [S_ST_PAUSING]       = { L"Pausing...",     L"جارٍ الإيقاف..." },
    [S_ST_PAUSED]        = { L"Paused",         L"متوقف" },
    [S_ST_DONE]          = { L"Completed",      L"اكتمل" },
    [S_ST_ERROR]         = { L"Error",          L"خطأ" },

    /* List */
    [S_COL_NAME]         = { L"File name",  L"اسم الملف" },
    [S_COL_SIZE]         = { L"Size",       L"الحجم" },
    [S_COL_PROGRESS]     = { L"Progress",   L"التقدم" },
    [S_COL_SPEED]        = { L"Speed",      L"السرعة" },
    [S_COL_ETA]          = { L"Time left",  L"المتبقي" },
    [S_COL_STATUS]       = { L"Status",     L"الحالة" },
    [S_EMPTY_TITLE]      = { L"No downloads yet", L"لا توجد تحميلات بعد" },
    [S_EMPTY_HINT]       = { L"Copy any download link - it will appear above automatically. Then click \"Add download\".",
                             L"انسخ أي رابط تحميل وسيظهر في الأعلى تلقائياً، ثم اضغط \"إضافة تحميل\"" },

    /* Details panel / status bar */
    [S_DET_NONE]         = { L"Select a download to see its details and segment map",
                             L"اختر تحميلاً من القائمة لعرض تفاصيله وخريطة الأجزاء" },
    [S_DET_ERROR]        = { L"Error: %ls", L"سبب الخطأ: %ls" },
    [S_DET_URL]          = { L"URL: \x200E%ls", L"\x200F" L"الرابط: \x200E%ls" },
    [S_DET_STATS_ACTIVE] = {
        L"Downloaded \x200E%ls / %ls\x200E   \x2022   Speed %ls/s   \x2022   Time left %ls   \x2022   Connections %ld   \x2022   Parts %d   \x2022   Resume: %ls",
        L"تم \x200E%ls / %ls\x200F   \x2022   السرعة \x200E%ls/s\x200F   \x2022   المتبقي \x200E%ls\x200F   \x2022   الاتصالات %ld   \x2022   الأجزاء %d   \x2022   الاستكمال: %ls" },
    [S_DET_STATS_IDLE]   = {
        L"Downloaded \x200E%ls / %ls\x200E   \x2022   Parts left %d   \x2022   Resume: %ls   \x2022   %ls",
        L"تم \x200E%ls / %ls\x200F   \x2022   الأجزاء المتبقية %d   \x2022   الاستكمال: %ls   \x2022   \x200E%ls" },
    [S_SUPPORTED]        = { L"supported",     L"مدعوم" },
    [S_NOT_SUPPORTED]    = { L"not supported", L"غير مدعوم" },
    [S_UNKNOWN]          = { L"unknown",       L"غير معروف" },
    [S_QMARK]            = { L"?",             L"؟" },
    [S_BAR_STATS]        = { L"Total speed: %ls/s     \x2022     Active: %d     \x2022     Completed: %d     \x2022     All: %d",
                             L"السرعة الكلية: \x200E%ls/s\x200F     \x2022     نشط: %d     \x2022     مكتمل: %d     \x2022     الكل: %d" },
    [S_BAR_FOLDER]       = { L"Save to: %ls", L"مجلد الحفظ: \x200E%ls" },

    /* Context menu */
    [S_MENU_RESUME]      = { L"Resume",                    L"استئناف" },
    [S_MENU_PAUSE]       = { L"Pause",                     L"إيقاف مؤقت" },
    [S_MENU_OPEN]        = { L"Open file",                 L"فتح الملف" },
    [S_MENU_OPENDIR]     = { L"Open folder",               L"فتح المجلد" },
    [S_MENU_COPYURL]     = { L"Copy link",                 L"نسخ الرابط" },
    [S_MENU_REMOVE]      = { L"Remove from list",          L"حذف من القائمة" },
    [S_MENU_REMOVEFILE]  = { L"Remove and delete file",    L"حذف مع الملف من القرص" },

    /* Message boxes */
    [S_MSG_BAD_URL]      = { L"The link must start with http:// or https://", L"يجب أن يبدأ الرابط بـ http:// أو https://" },
    [S_MSG_LIST_FULL]    = { L"The list is full. Remove some downloads first.", L"القائمة ممتلئة، احذف بعض التحميلات أولاً." },
    [S_DEL_TITLE]        = { L"Delete", L"حذف" },
    [S_DEL_ASK]          = { L"Also delete the files from disk?\n\nYes: remove from list and delete files\nNo: remove from list only",
                             L"هل تريد حذف الملفات من القرص أيضاً؟\n\nنعم: حذف من القائمة ومن القرص\nلا: حذف من القائمة فقط" },
    [S_EXIT_TITLE]       = { L"Exit", L"خروج" },
    [S_EXIT_ASK]         = { L"Some downloads are still running. They will be paused and can be resumed later.\nExit anyway?",
                             L"توجد تحميلات قيد التشغيل. سيتم إيقافها ويمكنك استكمالها لاحقاً.\nهل تريد الخروج؟" },
    [S_PICK_FOLDER]      = { L"Choose the download folder", L"اختر مجلد حفظ التحميلات" },

    /* New download dialog */
    [S_DLG_WINDOW]       = { L"DAO - New download", L"DAO - تحميل جديد" },
    [S_DLG_HEADER]       = { L"New download",       L"تحميل ملف جديد" },
    [S_DLG_FILENAME]     = { L"File name",          L"اسم الملف" },
    [S_DLG_FETCHING]     = { L"Fetching file information...", L"جارٍ جلب معلومات الملف..." },
    [S_DLG_SUPPORTED]    = { L"supported \x2713",   L"مدعوم \x2713" },
    [S_DLG_INFO]         = { L"Size:  %ls          Resume & multi-part:  %ls",
                             L"الحجم:  \x202A%ls\x202C          الاستكمال والتقسيم:  %ls" },
    [S_DLG_SAVE_TO]      = { L"Save to",        L"حفظ في" },
    [S_DLG_CHANGE]       = { L"Change",         L"تغيير" },
    [S_DLG_START]        = { L"Start download", L"بدء التحميل" },
    [S_DLG_LATER]        = { L"Later",          L"لاحقاً" },
    [S_DLG_CANCEL]       = { L"Cancel",         L"إلغاء" },
    [S_DLG_PROBE_FAILED] = { L"Could not check the link right now (%ls) - you can still start, DAO will retry.",
                             L"تعذّر فحص الرابط الآن (%ls) - يمكنك بدء التحميل وسيُعاد المحاولة تلقائياً" },

    /* Chrome setup guide */
    [S_GUIDE_WINDOW]     = { L"Connect Chrome", L"ربط كروم" },
    [S_GUIDE_TITLE]      = { L"Connect DAO with Chrome", L"ربط DAO مع كروم" },
    [S_GUIDE_SUBTITLE]   = { L"One-time setup - after this, every Chrome download opens in DAO",
                             L"خطوة لمرة واحدة - بعدها أي تحميل من كروم يُفتح في DAO تلقائياً" },
    [S_GUIDE_STEP1]      = { L"Open the Chrome extensions page", L"افتح صفحة الإضافات في كروم" },
    [S_GUIDE_STEP2]      = { L"Turn on  Developer mode", L"فعّل  Developer mode  (وضع المطوّر)" },
    [S_GUIDE_STEP3]      = { L"Click  Load unpacked, paste the path (Ctrl+V) and click  Select Folder",
                             L"اضغط  Load unpacked  ثم الصق المسار (Ctrl+V) واضغط  Select Folder" },
    [S_GUIDE_STEP1_HINT] = { L"The button opens Chrome and copies the extension path for you",
                             L"الزر يفتح كروم وينسخ مسار الإضافة تلقائياً" },
    [S_GUIDE_STEP2_HINT] = { L"The switch is in the top corner of the extensions page",
                             L"المفتاح موجود في الزاوية العليا من صفحة الإضافات" },
    [S_GUIDE_CONNECTED]  = { L"Connected \x2713  Try downloading any file from Chrome",
                             L"تم الربط بنجاح \x2713  جرّب تحميل أي ملف من كروم" },
    [S_GUIDE_WAITING]    = { L"Waiting for the extension... (updates automatically when you finish)",
                             L"بانتظار الإضافة... (تتحدث تلقائياً عند إكمال الخطوات)" },
    [S_GUIDE_OPEN]       = { L"Open Chrome", L"فتح كروم" },
    [S_GUIDE_COPY]       = { L"Copy path",   L"نسخ المسار" },
    [S_GUIDE_DONE]       = { L"Done",        L"تم" },
    [S_GUIDE_NO_CHROME]  = { L"Chrome was not found. Open Chrome and go to:\nchrome://extensions",
                             L"لم يتم العثور على كروم. افتح كروم واكتب في شريط العنوان:\nchrome://extensions" },

    /* Engine errors */
    [S_ERR_BAD_URL]      = { L"Invalid link (only http and https are supported)", L"رابط غير صالح (يدعم http و https فقط)" },
    [S_ERR_CONNECT]      = { L"Could not connect to the server (%lu)", L"فشل الاتصال بالسيرفر (%lu)" },
    [S_ERR_HTTP]         = { L"Server returned HTTP %lu",               L"السيرفر أعاد الخطأ HTTP %lu" },
    [S_ERR_DISK_WRITE]   = { L"Disk write failed (is the disk full?)",  L"فشل الكتابة على القرص (هل المساحة ممتلئة؟)" },
    [S_ERR_RETRIES]      = { L"Connection dropped too many times",      L"انقطع الاتصال عدة مرات" },
    [S_ERR_CREATE_FILE]  = { L"Could not create the file in the download folder", L"تعذّر إنشاء الملف في مجلد الحفظ" },
    [S_ERR_DISK_FULL]    = { L"Not enough disk space",                  L"مساحة القرص غير كافية" },
    [S_ERR_RENAME]       = { L"Could not rename the finished file",     L"تعذّر إعادة تسمية الملف النهائي" },
};

const wchar_t *T(StrId id)
{
    if (id < 0 || id >= S_COUNT) return L"";
    const wchar_t *s = g_strings[id][g_lang];
    return s ? s : g_strings[id][LANG_EN];
}

UiLang i18n_default_lang(void)
{
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_ARABIC ? LANG_AR : LANG_EN;
}
