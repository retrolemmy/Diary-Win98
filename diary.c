/*
 * Diary for Windows 95/98: a simple one-page-per-day journal.
 *
 * One window: a month calendar and the list of entries on the left, the
 * open day on the right. Entries are text files named YYYY-MM-DD in a
 * "Diary Entries" folder next to the program, or on a Diary Server.
 * Plain Win32 without a C runtime (see crt.c), so it runs on Windows 95
 * with Internet Explorer 4 or later, Windows 98, ME and newer.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include "crt.h"
#include "net.h"
#include "resource.h"

#define kMargin      8
#define kButtonH     24
#define kMaxSearch   100
#define kMaxEntry    60000      /* Windows 9x edit controls hold about 64 KB */

typedef struct {
    int *days;                  /* days since 1 Jan 1970, newest first */
    int count, capacity;
} DayList;

static HINSTANCE gInstance;
static HWND gWindow, gCalendar, gList, gPrev, gToday, gNext, gDate, gEdit;
static HFONT gGuiFont, gBoldFont, gTextFont;
static LOGFONTA gTextLogFont;
static int gCalendarW, gCalendarH;

static DayList gAllDays, gShownDays;
static int gDay;                        /* the open day */
static char gSearch[kMaxSearch + 1];    /* the current search, "" if none */
static char gFolder[MAX_PATH];          /* the "Diary Entries" folder */
static char gIniPath[MAX_PATH];         /* Diary.ini, for the settings */
static BOOL gUseServer;                 /* FALSE also after "work locally" */
static char gServer[64], gServerKey[64];
static char gEntryVersion[16];          /* the server's version of the entry */
static char gTitle[100];
static BOOL gSyncing;                   /* updating controls, not the user */

static const char *const kWeekdays[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const kMonths[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static void RefreshList(void);
static void RefreshCalendar(void);

/* ---------- Memory ---------- */

static void *Alloc(long size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static void Free(void *p)
{
    if (p != NULL)
        HeapFree(GetProcessHeap(), 0, p);
}

/* ---------- Dates ---------- */

/* Civil date from days since 1 Jan 1970 (H. Hinnant's algorithm). */
static void DayToDate(int day, int *year, int *month, int *dayOfMonth)
{
    int z = day + 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int m = mp < 10 ? mp + 3 : mp - 9;

    *year = yoe + era * 400 + (m <= 2);
    *month = m;
    *dayOfMonth = doy - (153 * mp + 2) / 5 + 1;
}

static int DateToDay(int year, int month, int dayOfMonth)
{
    int y = year - (month <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    int yoe = y - era * 400;
    int doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + dayOfMonth - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;

    return era * 146097 + doe - 719468;
}

static int Today(void)
{
    SYSTEMTIME now;

    GetLocalTime(&now);
    return DateToDay(now.wYear, now.wMonth, now.wDay);
}

static int Weekday(int day)
{
    return ((day + 4) % 7 + 7) % 7;     /* 1 Jan 1970 was a Thursday */
}

static void MakeEntryName(int day, char *name)
{
    int y, m, d;

    DayToDate(day, &y, &m, &d);
    wsprintfA(name, "%04d-%02d-%02d", y, m, d);
}

/* Parses a YYYY-MM-DD name; rejects anything else, including 31 April. */
static BOOL ParseEntryName(const char *s, int *day)
{
    int y, m, d, i, cy, cm, cd;

    for (i = 0; i < 10; i++)
        if ((i == 4 || i == 7) ? s[i] != '-' : (s[i] < '0' || s[i] > '9'))
            return FALSE;
    if (s[10] != 0)
        return FALSE;
    y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    m = (s[5] - '0') * 10 + (s[6] - '0');
    d = (s[8] - '0') * 10 + (s[9] - '0');
    if (y < 1904 || m < 1 || m > 12 || d < 1 || d > 31)
        return FALSE;
    *day = DateToDay(y, m, d);
    DayToDate(*day, &cy, &cm, &cd);
    return cy == y && cm == m && cd == d;
}

static void MakeLongDate(int day, char *s)
{
    int y, m, d;

    DayToDate(day, &y, &m, &d);
    wsprintfA(s, "%s, %d %s %d", kWeekdays[Weekday(day)], d, kMonths[m - 1], y);
}

static void MakeShortDate(int day, char *s)
{
    int y, m, d;

    DayToDate(day, &y, &m, &d);
    wsprintfA(s, "%.3s %02d %.3s %d", kWeekdays[Weekday(day)], d, kMonths[m - 1], y);
}

/* ---------- Day lists ---------- */

static int FindDay(const DayList *list, int day)
{
    int i;

    for (i = 0; i < list->count; i++)
        if (list->days[i] == day)
            return i;
    return -1;
}

/* Keeps the list newest first. */
static BOOL InsertDay(DayList *list, int day)
{
    int i;

    if (FindDay(list, day) >= 0)
        return TRUE;
    if (list->count == list->capacity) {
        int capacity = list->capacity < 64 ? 64 : list->capacity * 2;
        int *days = list->days == NULL
                    ? HeapAlloc(GetProcessHeap(), 0, capacity * sizeof(int))
                    : HeapReAlloc(GetProcessHeap(), 0, list->days, capacity * sizeof(int));
        if (days == NULL)
            return FALSE;
        list->days = days;
        list->capacity = capacity;
    }
    for (i = list->count; i > 0 && list->days[i - 1] < day; i--)
        list->days[i] = list->days[i - 1];
    list->days[i] = day;
    list->count++;
    return TRUE;
}

static void RemoveDay(DayList *list, int day)
{
    int i = FindDay(list, day);

    if (i < 0)
        return;
    memmove(list->days + i, list->days + i + 1, (list->count - i - 1) * sizeof(int));
    list->count--;
}

static void CopyDays(DayList *to, const DayList *from)
{
    int i;

    to->count = 0;
    for (i = 0; i < from->count; i++)
        InsertDay(to, from->days[i]);
}

/* ---------- Messages ---------- */

static void ShowMessage(const char *text)
{
    MessageBoxA(gWindow, text, "Diary", MB_OK | MB_ICONINFORMATION);
}

static void ShowError(const char *text)
{
    MessageBoxA(gWindow, text, "Diary", MB_OK | MB_ICONEXCLAMATION);
}

/* Shows a server problem: what failed, then why. */
static void ShowServerError(const char *what, const char *why)
{
    char message[400];

    CopyString(message, what, sizeof(message));
    if (strlen(message) + strlen(why) + 3 < sizeof(message)) {
        strcat(message, "\r\n\r\n");
        strcat(message, why);
    }
    ShowError(message);
}

static const char *Reason(const HttpResponse *response)
{
    if (response->status == 403)
        return "The server wants a different key.";
    return response->body != NULL && response->body[0] != 0 ? response->body
                                                              : "The server refused the request.";
}

static void UpdateTitle(void)
{
    if (gUseServer)
        wsprintfA(gTitle, "Diary - %s", gServer);
    else
        strcpy(gTitle, "Diary");
    SetWindowTextA(gWindow, gTitle);
}

/* ---------- Character sets ---------- */

/* MacRoman 0x80..0xFF in Windows-1252 (generated from Python's codecs;
   plain look-alikes where Windows-1252 has no match). */
static const unsigned char kMacToWindows[128] = {
    0xC4, 0xC5, 0xC7, 0xC9, 0xD1, 0xD6, 0xDC, 0xE1,  /* 80 */
    0xE0, 0xE2, 0xE4, 0xE3, 0xE5, 0xE7, 0xE9, 0xE8,  /* 88 */
    0xEA, 0xEB, 0xED, 0xEC, 0xEE, 0xEF, 0xF1, 0xF3,  /* 90 */
    0xF2, 0xF4, 0xF6, 0xF5, 0xFA, 0xF9, 0xFB, 0xFC,  /* 98 */
    0x86, 0xB0, 0xA2, 0xA3, 0xA7, 0x95, 0xB6, 0xDF,  /* A0 */
    0xAE, 0xA9, 0x99, 0xB4, 0xA8, 0x23, 0xC6, 0xD8,  /* A8 */
    0x38, 0xB1, 0x3C, 0x3E, 0xA5, 0xB5, 0x64, 0x53,  /* B0 */
    0x50, 0x70, 0x53, 0xAA, 0xBA, 0x3F, 0xE6, 0xF8,  /* B8 */
    0xBF, 0xA1, 0xAC, 0x56, 0x83, 0x7E, 0x44, 0xAB,  /* C0 */
    0xBB, 0x85, 0xA0, 0xC0, 0xC3, 0xD5, 0x8C, 0x9C,  /* C8 */
    0x96, 0x97, 0x93, 0x94, 0x91, 0x92, 0xF7, 0x6F,  /* D0 */
    0xFF, 0x9F, 0x2F, 0x80, 0x8B, 0x9B, 0x66, 0x66,  /* D8 */
    0x87, 0xB7, 0x82, 0x84, 0x89, 0xC2, 0xCA, 0xC1,  /* E0 */
    0xCB, 0xC8, 0xCD, 0xCE, 0xCF, 0xCC, 0xD3, 0xD4,  /* E8 */
    0x3F, 0xD2, 0xDA, 0xDB, 0xD9, 0x69, 0x88, 0x98,  /* F0 */
    0xAF, 0x7E, 0x2E, 0x6F, 0xB8, 0x22, 0x2C, 0x76,  /* F8 */
};

/* The Unicode characters of Windows-1252 0x80..0x9F (0 = unused). */
static const unsigned short kWindowsHigh[32] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,  /* 80 */
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,  /* 88 */
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,  /* 90 */
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,  /* 98 */
};

static unsigned char UnicodeToWindows(unsigned long c)
{
    int i;

    if (c < 0x80 || (c >= 0xA0 && c <= 0xFF))
        return (unsigned char)c;
    for (i = 0; i < 32; i++)
        if (kWindowsHigh[i] == c)
            return (unsigned char)(0x80 + i);
    return '?';
}

/* Decodes UTF-8 in place into Windows-1252; FALSE (and nothing changed)
   if text isn't valid UTF-8 or is plain ASCII. */
static BOOL ConvertUtf8(unsigned char *text, long *length)
{
    long i, n = 0;
    BOOL wide = FALSE;

    for (i = 0; i < *length; ) {
        unsigned char c = text[i];
        int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2
                    : (c & 0xF8) == 0xF0 ? 3 : -1;
        int k;

        if (extra < 0 || (extra > 0 && i + extra >= *length))
            return FALSE;       /* not UTF-8, or cut off at the end */
        for (k = 1; k <= extra; k++)
            if ((text[i + k] & 0xC0) != 0x80)
                return FALSE;
        if (extra > 0)
            wide = TRUE;
        i += extra + 1;
    }
    if (!wide)
        return FALSE;

    for (i = 0; i < *length; ) {
        unsigned char c = text[i];
        unsigned long code;
        int extra = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : 3;
        int k;

        code = extra == 0 ? c : extra == 1 ? (c & 0x1F) : extra == 2 ? (c & 0x0F) : (c & 0x07);
        for (k = 1; k <= extra; k++)
            code = (code << 6) | (text[i + k] & 0x3F);
        text[n++] = UnicodeToWindows(code);
        i += extra + 1;
    }
    *length = n;
    return TRUE;
}

/* Turns an entry file from any Diary version into Windows-1252 with CR LF
   line ends; returns a new NUL-terminated buffer (Free() it) or NULL.
   Mac OS X writes UTF-8; the classic Mac version writes MacRoman with CR
   line ends and never an LF; this one and the Amiga's (ISO Latin-1, LF)
   need no conversion apart from the line ends. */
static char *ConvertForeignText(unsigned char *text, long length)
{
    char *out;
    long i, n = 0;
    BOOL hasLF = FALSE;

    if (!ConvertUtf8(text, &length)) {
        for (i = 0; i < length; i++)
            if (text[i] == '\n')
                hasLF = TRUE;
        if (!hasLF)
            for (i = 0; i < length; i++)
                if (text[i] >= 0x80)
                    text[i] = kMacToWindows[text[i] - 0x80];
    }

    out = Alloc(length * 2 + 1);
    if (out == NULL)
        return NULL;
    for (i = 0; i < length; i++) {
        if (text[i] == '\r' || text[i] == '\n') {
            out[n++] = '\r';
            out[n++] = '\n';
            if (text[i] == '\r' && i + 1 < length && text[i + 1] == '\n')
                i++;
        } else if (text[i] != 0) {
            out[n++] = text[i];
        }
    }
    while (n >= 2 && out[n - 2] == '\r' && out[n - 1] == '\n')
        n -= 2;                 /* SaveEntry adds one line end */
    out[n] = 0;
    return out;
}

/* ---------- Entry files ---------- */

static void EntryPath(int day, char *path)
{
    strcpy(path, gFolder);
    strcat(path, "\\");
    MakeEntryName(day, path + strlen(path));
}

static char *ReadEntryFile(int day)
{
    char path[MAX_PATH];
    HANDLE file;
    DWORD size, got = 0;
    unsigned char *data;
    char *text = NULL;

    EntryPath(day, path);
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return NULL;
    size = GetFileSize(file, NULL);
    if (size > kMaxEntry * 2)
        size = kMaxEntry * 2;
    data = Alloc(size + 1);
    if (data != NULL && ReadFile(file, data, size, &got, NULL))
        text = ConvertForeignText(data, got);
    Free(data);
    CloseHandle(file);
    return text;
}

static BOOL WriteEntryFile(int day, const char *text, long length)
{
    char path[MAX_PATH];
    HANDLE file;
    DWORD written;
    BOOL ok;

    CreateDirectoryA(gFolder, NULL);
    EntryPath(day, path);
    file = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    ok = WriteFile(file, text, length, &written, NULL) && written == (DWORD)length
         && WriteFile(file, "\r\n", 2, &written, NULL) && written == 2;
    CloseHandle(file);
    return ok;
}

static void ScanLocalEntries(void)
{
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA found;
    HANDLE search;
    int day;

    strcpy(pattern, gFolder);
    strcat(pattern, "\\*");
    search = FindFirstFileA(pattern, &found);
    if (search == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && found.nFileSizeLow > 0
                && ParseEntryName(found.cFileName, &day))
            InsertDay(&gAllDays, day);
    } while (FindNextFileA(search, &found));
    FindClose(search);
}

/* Adds the days of a server reply ("YYYY-MM-DD ..." lines) to list. */
static void AddDaysFromReply(DayList *list, const char *reply)
{
    while (*reply != 0) {
        char name[11];
        int day;

        CopyString(name, reply, sizeof(name));
        if (ParseEntryName(name, &day))
            InsertDay(list, day);
        reply = strchr(reply, '\n');
        if (reply == NULL)
            break;
        reply++;
    }
}

static BOOL ScanServerEntries(char *error, int errorSize)
{
    HttpResponse response;

    if (!HttpRequest("GET", "/entries", NULL, 0, NULL, &response, error, errorSize))
        return FALSE;
    if (response.status != 200) {
        CopyString(error, Reason(&response), errorSize);
        HttpFree(&response);
        return FALSE;
    }
    AddDaysFromReply(&gAllDays, response.body);
    HttpFree(&response);
    return TRUE;
}

/* Reads the list of entries, from the server if one is set. If it can't be
   reached, Diary can work with the local folder for this session. */
static void ScanEntries(void)
{
    gAllDays.count = 0;
    while (gUseServer) {
        char error[200], message[400];
        HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
        BOOL ok = ScanServerEntries(error, sizeof(error));

        SetCursor(old);
        if (ok)
            return;
        gAllDays.count = 0;
        wsprintfA(message, "Diary couldn't get the entries from the Diary Server:\r\n\r\n%s\r\n\r\n"
                           "Click Retry to try again, or Cancel to work with the local "
                           "\"Diary Entries\" folder for now.", error);
        if (MessageBoxA(gWindow, message, "Diary", MB_RETRYCANCEL | MB_ICONEXCLAMATION) != IDRETRY) {
            gUseServer = FALSE;     /* for this session; the setting stays */
            UpdateTitle();
        }
    }
    ScanLocalEntries();
}

/* ---------- Settings (Diary.ini next to the program) ---------- */

static void LoadSettings(void)
{
    static const char kSection[] = "Diary";

    GetPrivateProfileStringA(kSection, "Server", "", gServer, sizeof(gServer), gIniPath);
    GetPrivateProfileStringA(kSection, "Key", "", gServerKey, sizeof(gServerKey), gIniPath);

    memset(&gTextLogFont, 0, sizeof(gTextLogFont));
    GetPrivateProfileStringA(kSection, "Font", "", gTextLogFont.lfFaceName,
                             sizeof(gTextLogFont.lfFaceName), gIniPath);
    gTextLogFont.lfHeight = GetPrivateProfileIntA(kSection, "FontHeight", -13, gIniPath);
    gTextLogFont.lfWeight = GetPrivateProfileIntA(kSection, "FontWeight", FW_NORMAL, gIniPath);
    gTextLogFont.lfItalic = (BYTE)GetPrivateProfileIntA(kSection, "FontItalic", 0, gIniPath);
    gTextLogFont.lfCharSet = DEFAULT_CHARSET;
    if (gTextLogFont.lfFaceName[0] == 0)
        strcpy(gTextLogFont.lfFaceName, "Arial");
}

static void WriteIniInt(const char *key, int value)
{
    char number[16];

    wsprintfA(number, "%d", value);
    WritePrivateProfileStringA("Diary", key, number, gIniPath);
}

static void SaveSettings(void)
{
    WritePrivateProfileStringA("Diary", "Server", gServer, gIniPath);
    WritePrivateProfileStringA("Diary", "Key", gServerKey, gIniPath);
    WritePrivateProfileStringA("Diary", "Font", gTextLogFont.lfFaceName, gIniPath);
    WriteIniInt("FontHeight", gTextLogFont.lfHeight);
    WriteIniInt("FontWeight", gTextLogFont.lfWeight);
    WriteIniInt("FontItalic", gTextLogFont.lfItalic);
}

static void ApplyTextFont(void)
{
    HFONT font = CreateFontIndirectA(&gTextLogFont);

    if (font == NULL)
        return;
    SendMessageA(gEdit, WM_SETFONT, (WPARAM)font, TRUE);
    if (gTextFont != NULL)
        DeleteObject(gTextFont);
    gTextFont = font;
}

/* ---------- The open entry ---------- */

static char *ReadServerEntry(int day)
{
    char path[32], error[200];
    HttpResponse response;
    HCURSOR old;
    BOOL ok;

    strcpy(path, "/entries/");
    MakeEntryName(day, path + strlen(path));
    strcpy(gEntryVersion, "none");
    old = SetCursor(LoadCursor(NULL, IDC_WAIT));
    ok = HttpRequest("GET", path, NULL, 0, NULL, &response, error, sizeof(error));
    SetCursor(old);
    if (!ok) {
        ShowServerError("Diary couldn't load the entry from the Diary Server:", error);
        return NULL;
    }
    if (response.status == 200) {
        strcpy(gEntryVersion, response.version);
        return response.body;       /* keep the buffer; Free() it */
    }
    if (response.status != 404)
        ShowServerError("Diary couldn't load the entry from the Diary Server:", Reason(&response));
    HttpFree(&response);
    return NULL;
}

static void SyncListSelection(void)
{
    int row = FindDay(&gShownDays, gDay);

    SendMessageA(gList, LB_SETCURSEL, row, 0);     /* -1 clears it */
}

static void LoadEntry(void)
{
    char *text = gUseServer ? ReadServerEntry(gDay) : ReadEntryFile(gDay);
    char date[64];
    SYSTEMTIME selected;
    int y, m, d;

    gSyncing = TRUE;
    SetWindowTextA(gEdit, text != NULL ? text : "");
    Free(text);
    SendMessageA(gEdit, EM_SETMODIFY, FALSE, 0);
    SendMessageA(gEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    SendMessageA(gEdit, EM_SETSEL, 0, 0);
    SendMessageA(gEdit, EM_SCROLLCARET, 0, 0);

    MakeLongDate(gDay, date);
    SetWindowTextA(gDate, date);

    DayToDate(gDay, &y, &m, &d);
    memset(&selected, 0, sizeof(selected));
    selected.wYear = y;
    selected.wMonth = m;
    selected.wDay = d;
    selected.wDayOfWeek = Weekday(gDay);
    SendMessageA(gCalendar, MCM_SETCURSEL, 0, (LPARAM)&selected);
    RefreshCalendar();
    SyncListSelection();
    gSyncing = FALSE;
}

/* Saves (or, if length is 0, deletes) the open entry on the server. If it
   was changed on another computer since it was loaded, asks first. */
static BOOL SaveServerEntry(const char *text, long length)
{
    char path[32], error[200];
    const char *base = gEntryVersion;

    strcpy(path, "/entries/");
    MakeEntryName(gDay, path + strlen(path));
    for (;;) {
        HttpResponse response;
        HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
        BOOL ok = HttpRequest(length > 0 ? "PUT" : "DELETE", path, length > 0 ? text : NULL,
                              length, base, &response, error, sizeof(error));
        int status;

        SetCursor(old);
        if (!ok) {
            ShowServerError("Diary couldn't save the entry on the Diary Server; "
                            "your text is still in the window:", error);
            return FALSE;
        }
        status = response.status;
        if (status == 200)
            CopyString(gEntryVersion, response.version[0] ? response.version : "none",
                       sizeof(gEntryVersion));
        else if (status != 409)
            ShowServerError("The Diary Server refused to save the entry; "
                            "your text is still in the window:", Reason(&response));
        HttpFree(&response);

        if (status == 200)
            return TRUE;
        if (status != 409)
            return FALSE;
        if (MessageBoxA(gWindow, "This entry was changed on another computer since you "
                                 "opened it.\r\n\r\nSave your version over it?",
                        "Diary", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return FALSE;
        base = NULL;                /* replace it unconditionally */
    }
}

static BOOL SaveEntry(void)
{
    long length;
    char *text;
    BOOL ok;

    if (!SendMessageA(gEdit, EM_GETMODIFY, 0, 0))
        return TRUE;
    length = GetWindowTextLengthA(gEdit);
    text = Alloc(length + 1);
    if (text == NULL) {
        ShowError("Not enough memory to save the entry.");
        return FALSE;
    }
    length = GetWindowTextA(gEdit, text, length + 1);
    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r'))
        length--;
    text[length] = 0;

    if (gUseServer) {
        ok = SaveServerEntry(text, length);
    } else if (length == 0) {
        char path[MAX_PATH];
        /* An emptied entry is removed rather than kept as an empty file. */
        EntryPath(gDay, path);
        DeleteFileA(path);
        ok = TRUE;
    } else {
        ok = WriteEntryFile(gDay, text, length);
        if (!ok)
            ShowError("Diary couldn't save the entry. Check that the \"Diary Entries\" "
                      "folder next to the program can be written to.");
    }
    Free(text);
    if (!ok)
        return FALSE;

    if (length == 0) {
        RemoveDay(&gAllDays, gDay);
        RemoveDay(&gShownDays, gDay);
        RefreshList();
    } else if (FindDay(&gAllDays, gDay) < 0) {
        InsertDay(&gAllDays, gDay);
        if (gSearch[0] == 0)
            InsertDay(&gShownDays, gDay);
        RefreshList();
    }
    SendMessageA(gEdit, EM_SETMODIFY, FALSE, 0);
    return TRUE;
}

static void GoToDay(int day)
{
    if (day == gDay || !SaveEntry()) {
        gSyncing = TRUE;
        SyncListSelection();
        gSyncing = FALSE;
        return;
    }
    gDay = day;
    LoadEntry();
}

/* ---------- Searching ---------- */

/* Finds pattern in text, ignoring case (as Windows does for the user's
   language), from position start; returns the position or -1. */
static long FindInText(const char *text, long start, const char *pattern)
{
    long length = strlen(text), n = strlen(pattern), i;
    char *a, *b;
    long found = -1;

    if (n == 0 || start > length)
        return -1;
    a = Alloc(length + 1);
    b = Alloc(n + 1);
    if (a != NULL && b != NULL) {
        strcpy(a, text);
        strcpy(b, pattern);
        CharLowerBuffA(a, length);
        CharLowerBuffA(b, n);
        for (i = start; i + n <= length; i++)
            if (memcmp(a + i, b, n) == 0) {
                found = i;
                break;
            }
    }
    Free(a);
    Free(b);
    return found;
}

static BOOL EntryContains(int day)
{
    char *text;
    BOOL found;

    /* With a server, the listed days are its search result. */
    if (gUseServer)
        return FindDay(&gShownDays, day) >= 0;
    text = ReadEntryFile(day);
    if (text == NULL)
        return FALSE;
    found = FindInText(text, 0, gSearch) >= 0;
    Free(text);
    return found;
}

/* Selects the first match in the open entry, or the next one after the selection. */
static BOOL SelectMatch(BOOL fromTop)
{
    long length = GetWindowTextLengthA(gEdit), start = 0, found;
    DWORD selStart, selEnd;
    char *text = Alloc(length + 1);

    if (text == NULL)
        return FALSE;
    GetWindowTextA(gEdit, text, length + 1);
    if (!fromTop) {
        SendMessageA(gEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        start = selEnd;
    }
    found = FindInText(text, start, gSearch);
    Free(text);
    if (found < 0)
        return FALSE;
    SendMessageA(gEdit, EM_SETSEL, found, found + strlen(gSearch));
    SendMessageA(gEdit, EM_SCROLLCARET, 0, 0);
    SetFocus(gEdit);
    return TRUE;
}

static void OpenDay(int day)
{
    GoToDay(day);
    if (gSearch[0] != 0 && day == gDay)
        SelectMatch(TRUE);
}

static void ShowAllEntries(void)
{
    gSearch[0] = 0;
    CopyDays(&gShownDays, &gAllDays);
    RefreshList();
}

static char gAskText[kMaxSearch + 1];

static INT_PTR CALLBACK FindDialog(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG:
        SendDlgItemMessageA(dialog, IDC_FINDTEXT, EM_LIMITTEXT, kMaxSearch, 0);
        SetDlgItemTextA(dialog, IDC_FINDTEXT, gAskText);
        SendDlgItemMessageA(dialog, IDC_FINDTEXT, EM_SETSEL, 0, -1);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
            GetDlgItemTextA(dialog, IDC_FINDTEXT, gAskText, sizeof(gAskText));
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static void DoFind(void)
{
    DayList matches = { NULL, 0, 0 };
    char previous[kMaxSearch + 1];
    int i;

    strcpy(gAskText, gSearch);
    if (DialogBoxParamA(gInstance, MAKEINTRESOURCEA(IDD_FIND), gWindow, FindDialog, 0) != IDOK)
        return;
    if (gAskText[0] == 0) {
        ShowAllEntries();
        return;
    }
    if (!SaveEntry())
        return;

    strcpy(previous, gSearch);
    strcpy(gSearch, gAskText);
    if (gUseServer) {
        char path[3 * kMaxSearch + 16], error[200];
        HttpResponse response;
        HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));
        BOOL ok;

        strcpy(path, "/search?q=");
        NetEncode(gSearch, path + strlen(path), sizeof(path) - strlen(path));
        ok = HttpRequest("GET", path, NULL, 0, NULL, &response, error, sizeof(error));
        SetCursor(old);
        if (!ok || response.status != 200) {
            strcpy(gSearch, previous);
            if (!ok) {
                ShowServerError("Diary couldn't search on the Diary Server:", error);
            } else {
                ShowServerError("The Diary Server refused the search:", Reason(&response));
                HttpFree(&response);
            }
            return;
        }
        AddDaysFromReply(&matches, response.body);
        HttpFree(&response);
    } else {
        HCURSOR old = SetCursor(LoadCursor(NULL, IDC_WAIT));

        for (i = 0; i < gAllDays.count; i++)
            if (EntryContains(gAllDays.days[i]))
                InsertDay(&matches, gAllDays.days[i]);
        SetCursor(old);
    }

    if (matches.count == 0) {
        ShowMessage("No entries contain that text.");
        strcpy(gSearch, previous);      /* keep the current filter */
    } else {
        CopyDays(&gShownDays, &matches);
        RefreshList();
        if (FindDay(&gShownDays, gDay) < 0)
            GoToDay(gShownDays.days[0]);
        SelectMatch(TRUE);
    }
    Free(matches.days);
}

/* Moves to the next match: first within the open entry, then on to the
   following (older) matching entries, wrapping around to the newest. */
static void DoFindNext(void)
{
    int start, step;

    if (gSearch[0] == 0) {
        DoFind();
        return;
    }
    if (SelectMatch(FALSE))
        return;

    start = FindDay(&gShownDays, gDay);
    for (step = 1; step <= gShownDays.count; step++) {
        int i = start < 0 ? step - 1 : (start + step) % gShownDays.count;
        int day = gShownDays.days[i];

        if (day == gDay) {
            if (SelectMatch(TRUE))
                return;
        } else if (EntryContains(day)) {
            OpenDay(day);
            return;
        }
    }
    MessageBeep(MB_OK);
}

/* ---------- List and calendar ---------- */

static void RefreshList(void)
{
    char date[32];
    int i;

    gSyncing = TRUE;
    SendMessageA(gList, WM_SETREDRAW, FALSE, 0);
    SendMessageA(gList, LB_RESETCONTENT, 0, 0);
    for (i = 0; i < gShownDays.count; i++) {
        MakeShortDate(gShownDays.days[i], date);
        SendMessageA(gList, LB_ADDSTRING, 0, (LPARAM)date);
    }
    SyncListSelection();
    SendMessageA(gList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(gList, NULL, TRUE);
    gSyncing = FALSE;
    RefreshCalendar();
}

/* Marks the days with an entry (bold) for the months the calendar shows,
   starting with the month of first. */
static void FillDayState(const SYSTEMTIME *first, int count, MONTHDAYSTATE *states)
{
    int year = first->wYear, month = first->wMonth, i, k;

    for (i = 0; i < count; i++) {
        int start = DateToDay(year, month, 1);
        states[i] = 0;
        for (k = 0; k < gShownDays.count; k++) {
            int offset = gShownDays.days[k] - start;
            if (offset >= 0 && offset < 31) {
                int y, m, d;
                DayToDate(gShownDays.days[k], &y, &m, &d);
                if (y == year && m == month)
                    states[i] |= 1UL << (d - 1);
            }
        }
        if (++month > 12) {
            month = 1;
            year++;
        }
    }
}

static void RefreshCalendar(void)
{
    SYSTEMTIME range[2];
    MONTHDAYSTATE states[12];
    int count = (int)SendMessageA(gCalendar, MCM_GETMONTHRANGE, GMR_DAYSTATE, (LPARAM)range);

    if (count <= 0 || count > 12)
        return;
    FillDayState(&range[0], count, states);
    SendMessageA(gCalendar, MCM_SETDAYSTATE, count, (LPARAM)states);
}

/* ---------- Window ---------- */

static HWND MakeControl(const char *cls, const char *text, DWORD style, int id)
{
    HWND control = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10,
                                   gWindow, (HMENU)(INT_PTR)id, gInstance, NULL);
    SendMessageA(control, WM_SETFONT, (WPARAM)gGuiFont, FALSE);
    return control;
}

static void LayOut(int width, int height)
{
    int right = kMargin + gCalendarW + kMargin;
    int listTop = kMargin + gCalendarH + kMargin;
    int editTop = kMargin + kButtonH + kMargin;

    MoveWindow(gCalendar, kMargin, kMargin, gCalendarW, gCalendarH, TRUE);
    MoveWindow(gList, kMargin, listTop, gCalendarW, height - listTop - kMargin, TRUE);
    MoveWindow(gPrev, right, kMargin, 32, kButtonH, TRUE);
    MoveWindow(gToday, right + 36, kMargin, 64, kButtonH, TRUE);
    MoveWindow(gNext, right + 104, kMargin, 32, kButtonH, TRUE);
    MoveWindow(gDate, right + 148, kMargin + 4, width - right - 148 - kMargin, kButtonH - 4, TRUE);
    MoveWindow(gEdit, right, editTop, width - right - kMargin, height - editTop - kMargin, TRUE);
}

static void CreateControls(void)
{
    LOGFONTA bold;
    RECT size;

    gGuiFont = GetStockObject(DEFAULT_GUI_FONT);
    GetObjectA(gGuiFont, sizeof(bold), &bold);
    bold.lfWeight = FW_BOLD;
    gBoldFont = CreateFontIndirectA(&bold);

    gCalendar = CreateWindowExA(0, MONTHCAL_CLASSA, "", WS_CHILD | WS_VISIBLE | WS_BORDER | MCS_DAYSTATE,
                                0, 0, 10, 10, gWindow, (HMENU)IDC_CALENDAR, gInstance, NULL);
    SendMessageA(gCalendar, MCM_GETMINREQRECT, 0, (LPARAM)&size);
    gCalendarW = size.right - size.left;
    gCalendarH = size.bottom - size.top;
    if (gCalendarW < 150)
        gCalendarW = 180;
    if (gCalendarH < 120)
        gCalendarH = 160;

    gList = MakeControl("LISTBOX", "", WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT,
                        IDC_LIST);
    gPrev = MakeControl("BUTTON", "<", BS_PUSHBUTTON, IDC_PREV);
    gToday = MakeControl("BUTTON", "Today", BS_PUSHBUTTON, IDC_TODAYBUTTON);
    gNext = MakeControl("BUTTON", ">", BS_PUSHBUTTON, IDC_NEXT);
    gDate = MakeControl("STATIC", "", SS_LEFT | SS_NOPREFIX, IDC_DATE);
    SendMessageA(gDate, WM_SETFONT, (WPARAM)gBoldFont, FALSE);
    gEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL
                            | ES_WANTRETURN | ES_NOHIDESEL,
                            0, 0, 10, 10, gWindow, (HMENU)IDC_EDIT, gInstance, NULL);
    SendMessageA(gEdit, EM_LIMITTEXT, kMaxEntry, 0);
    ApplyTextFont();
}

static void ChooseTextFont(void)
{
    CHOOSEFONTA choose;
    LOGFONTA font = gTextLogFont;

    memset(&choose, 0, sizeof(choose));
    choose.lStructSize = sizeof(choose);
    choose.hwndOwner = gWindow;
    choose.lpLogFont = &font;
    choose.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT;
    if (!ChooseFontA(&choose))
        return;
    gTextLogFont = font;
    ApplyTextFont();
    SaveSettings();
}

static char gAskServer[sizeof(gServer)], gAskKey[sizeof(gServerKey)];

static INT_PTR CALLBACK ServerDialog(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_INITDIALOG:
        SendDlgItemMessageA(dialog, IDC_ADDRESS, EM_LIMITTEXT, sizeof(gServer) - 1, 0);
        SendDlgItemMessageA(dialog, IDC_KEY, EM_LIMITTEXT, sizeof(gServerKey) - 1, 0);
        SetDlgItemTextA(dialog, IDC_ADDRESS, gServer);
        SetDlgItemTextA(dialog, IDC_KEY, gServerKey);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetDlgItemTextA(dialog, IDC_ADDRESS, gAskServer, sizeof(gAskServer));
            GetDlgItemTextA(dialog, IDC_KEY, gAskKey, sizeof(gAskKey));
        }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL) {
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/* File > Diary Server...: where the entries are kept. */
static void ChooseServer(void)
{
    char *p;

    if (DialogBoxParamA(gInstance, MAKEINTRESOURCEA(IDD_SERVER), gWindow, ServerDialog, 0) != IDOK)
        return;
    if (!SaveEntry())
        return;

    for (p = gAskServer; *p == ' '; p++)
        ;
    CopyString(gServer, p, sizeof(gServer));
    for (p = gServer + strlen(gServer); p > gServer && p[-1] == ' '; p--)
        p[-1] = 0;
    CopyString(gServerKey, gServer[0] != 0 ? gAskKey : "", sizeof(gServerKey));
    gUseServer = gServer[0] != 0;
    NetSetServer(gServer, gServerKey);
    SaveSettings();
    UpdateTitle();

    ScanEntries();
    ShowAllEntries();
    LoadEntry();
}

static void ShowAbout(void)
{
    MessageBoxA(gWindow, "Diary 1.0\r\n\r\nA simple day-by-day journal for Windows 95 and 98.\r\n\r\n"
                         "https://github.com/retrolemmy/Diary-Win98\r\n"
                         "No rights reserved (CC0).", "About Diary", MB_OK | MB_ICONINFORMATION);
}

static BOOL ConfirmQuit(void)
{
    if (SaveEntry())
        return TRUE;
    return MessageBoxA(gWindow, "The entry could not be saved. Quit anyway?", "Diary",
                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES;
}

static void DoCommand(int id, int code)
{
    switch (id) {
    case IDM_SAVE:       SaveEntry(); break;
    case IDM_SERVER:     ChooseServer(); break;
    case IDM_EXIT:       PostMessageA(gWindow, WM_CLOSE, 0, 0); break;
    case IDM_UNDO:       SendMessageA(gEdit, EM_UNDO, 0, 0); break;
    case IDM_CUT:        SendMessageA(gEdit, WM_CUT, 0, 0); break;
    case IDM_COPY:       SendMessageA(gEdit, WM_COPY, 0, 0); break;
    case IDM_PASTE:      SendMessageA(gEdit, WM_PASTE, 0, 0); break;
    case IDM_SELECTALL:  SetFocus(gEdit); SendMessageA(gEdit, EM_SETSEL, 0, -1); break;
    case IDM_FIND:       DoFind(); break;
    case IDM_FINDNEXT:   DoFindNext(); break;
    case IDM_SHOWALL:    ShowAllEntries(); break;
    case IDM_PREVDAY:
    case IDC_PREV:       GoToDay(gDay - 1); SetFocus(gEdit); break;
    case IDM_NEXTDAY:
    case IDC_NEXT:       GoToDay(gDay + 1); SetFocus(gEdit); break;
    case IDM_TODAY:
    case IDC_TODAYBUTTON: GoToDay(Today()); SetFocus(gEdit); break;
    case IDM_FONT:       ChooseTextFont(); break;
    case IDM_ABOUT:      ShowAbout(); break;
    case IDC_LIST:
        if (code == LBN_SELCHANGE && !gSyncing) {
            int row = (int)SendMessageA(gList, LB_GETCURSEL, 0, 0);
            if (row >= 0 && row < gShownDays.count)
                OpenDay(gShownDays.days[row]);
        }
        break;
    }
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        gWindow = window;
        CreateControls();
        return 0;
    case WM_SIZE:
        LayOut(LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO *)lParam)->ptMinTrackSize.x = 520;
        ((MINMAXINFO *)lParam)->ptMinTrackSize.y = 380;
        return 0;
    case WM_SETFOCUS:
        SetFocus(gEdit);
        return 0;
    case WM_INITMENUPOPUP:
        EnableMenuItem((HMENU)wParam, IDM_SHOWALL, gSearch[0] != 0 ? MF_ENABLED : MF_GRAYED);
        EnableMenuItem((HMENU)wParam, IDM_UNDO,
                       SendMessageA(gEdit, EM_CANUNDO, 0, 0) ? MF_ENABLED : MF_GRAYED);
        return 0;
    case WM_COMMAND:
        DoCommand(LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_NOTIFY: {
        NMHDR *header = (NMHDR *)lParam;
        if (header->idFrom == IDC_CALENDAR && header->code == MCN_GETDAYSTATE) {
            NMDAYSTATE *state = (NMDAYSTATE *)lParam;
            FillDayState(&state->stStart, state->cDayState, state->prgDayState);
        } else if (header->idFrom == IDC_CALENDAR && header->code == MCN_SELECT && !gSyncing) {
            SYSTEMTIME *picked = &((NMSELCHANGE *)lParam)->stSelStart;
            OpenDay(DateToDay(picked->wYear, picked->wMonth, picked->wDay));
        }
        return 0;
    }
    case WM_CLOSE:
        if (ConfirmQuit())
            DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

/* The month calendar needs comctl32.dll 4.70 or later: Windows 98, or
   Windows 95 with Internet Explorer 4. Ask for it by name, so that Diary
   can explain instead of failing to start. */
static BOOL InitControls(void)
{
    typedef BOOL (WINAPI *InitProc)(const INITCOMMONCONTROLSEX *);
    HMODULE comctl = LoadLibraryA("comctl32.dll");
    InitProc init = comctl != NULL ? (InitProc)GetProcAddress(comctl, "InitCommonControlsEx") : NULL;
    INITCOMMONCONTROLSEX controls;

    if (init == NULL)
        return FALSE;
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_DATE_CLASSES;
    return init(&controls);
}

static void FindFiles(void)
{
    char *slash;

    GetModuleFileNameA(NULL, gFolder, sizeof(gFolder) - 32);
    slash = gFolder + strlen(gFolder);
    while (slash > gFolder && slash[-1] != '\\')
        slash--;
    *slash = 0;
    strcpy(gIniPath, gFolder);
    strcat(gIniPath, "Diary.ini");
    strcat(gFolder, "Diary Entries");
}

static int Run(void)
{
    WNDCLASSA wc;
    HACCEL accelerators;
    MSG msg;

    gInstance = GetModuleHandleA(NULL);
    if (!InitControls()) {
        MessageBoxA(NULL, "Diary needs Windows 98, or Windows 95 with Internet Explorer 4 or later.",
                    "Diary", MB_OK | MB_ICONSTOP);
        return 1;
    }
    FindFiles();
    LoadSettings();
    gUseServer = gServer[0] != 0;
    NetSetServer(gServer, gServerKey);

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = gInstance;
    wc.hIcon = LoadIconA(gInstance, MAKEINTRESOURCEA(IDI_DIARY));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName = MAKEINTRESOURCEA(IDR_MENU);
    wc.lpszClassName = "DiaryWindow";
    if (!RegisterClassA(&wc))
        return 1;

    if (CreateWindowExA(0, "DiaryWindow", "Diary", WS_OVERLAPPEDWINDOW,
                        CW_USEDEFAULT, CW_USEDEFAULT, 760, 520,
                        NULL, NULL, gInstance, NULL) == NULL)
        return 1;
    UpdateTitle();
    gDay = Today();
    ScanEntries();
    ShowAllEntries();
    LoadEntry();
    ShowWindow(gWindow, SW_SHOWDEFAULT);
    UpdateWindow(gWindow);
    SetFocus(gEdit);

    accelerators = LoadAcceleratorsA(gInstance, MAKEINTRESOURCEA(IDR_ACCEL));
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (TranslateAcceleratorA(gWindow, accelerators, &msg))
            continue;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    SaveSettings();
    NetCleanup();
    return (int)msg.wParam;
}

/* The program's entry point, in place of the C runtime's. */
void WINAPI Start(void)
{
    ExitProcess(Run());
}
