// WinCalendar - shared declarations
#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define NOMINMAX
#ifndef _CRT_RAND_S
#define _CRT_RAND_S
#endif
#include <stdlib.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <climits>
#include "resource.h"

typedef long long MIN_T;               // minutes since 1601-01-01 00:00 (wall clock of some zone)
const MIN_T DAYMIN = 1440;
#define TZ_FLOATING L"*"

// ---------------------------------------------------------------- time helpers
int  MakeDay(int y, int m, int d);     // day index since 1601-01-01
void DayYMD(int day, int& y, int& m, int& d);
inline int DayOf(MIN_T m) { return (int)(m >= 0 ? m / DAYMIN : (m - DAYMIN + 1) / DAYMIN); }
inline int TimeOf(MIN_T m) { return (int)(m - (MIN_T)DayOf(m) * DAYMIN); }
inline int Weekday(int day) { return (day + 1) % 7; }   // 0 = Sunday
inline MIN_T MakeMin(int y, int mo, int d, int h = 0, int mi = 0) { return (MIN_T)MakeDay(y, mo, d) * DAYMIN + h * 60 + mi; }
int  DaysInMonth(int y, int m);
int  AddMonths(int day, int n);
MIN_T StToMin(const SYSTEMTIME& st);
SYSTEMTIME MinToSt(MIN_T m);
MIN_T NowUtc();
MIN_T NowDisplay();
int  TodayDisplay();

// ---------------------------------------------------------------- time zones
struct TzInfo { std::wstring key, display; DYNAMIC_TIME_ZONE_INFORMATION dtzi; };
const std::vector<TzInfo>& AllTimeZones();
const TzInfo* FindTz(const std::wstring& key);
void RefreshTimeZoneCache();
std::wstring SystemTzKey();
std::wstring DisplayTzKey();
std::wstring TzDisplayName(const std::wstring& key);
MIN_T ZoneToUtc(MIN_T wall, const std::wstring& key);    // key "" or "*" -> display zone
MIN_T UtcToZone(MIN_T utc, const std::wstring& key);
std::wstring IanaFromWindows(const std::wstring& win);
std::wstring WindowsFromIana(const std::wstring& iana);
int  TzOffsetMinutes(const std::wstring& key, MIN_T utc);  // local - utc

// ---------------------------------------------------------------- model
enum { RSVP_NONE = 0, RSVP_ACCEPTED = 1, RSVP_DECLINED = 2, RSVP_TENTATIVE = 3, RSVP_PROPOSED = 4 };
enum { BUSY_FREE = 0, BUSY_BUSY = 1, BUSY_TENTATIVE = 2, BUSY_OOO = 3, BUSY_FOCUS = 4 };
enum { REM_POPUP = 0, REM_EMAIL = 1, REM_BOTH = 2 };
enum { CONF_NONE = 0, CONF_TEAMS, CONF_ZOOM, CONF_MEET, CONF_WEBEX, CONF_JITSI, CONF_CUSTOM, CONF_COUNT };
enum { PROV_LOCAL = 0, PROV_GOOGLE, PROV_ICLOUD, PROV_EXCHANGE, PROV_OUTLOOKCOM, PROV_OTHER, PROV_COUNT };

struct Attendee { std::wstring name, email; int status = RSVP_NONE; MIN_T proposed = 0; bool optional = false; };
struct Reminder { int minutes = 15; int method = REM_POPUP; };
struct Recurrence {
    int freq = 0;          // 0 none, 1 daily, 2 weekly, 3 monthly, 4 yearly
    int interval = 1;
    int weekMask = 0;      // bit0 = Sunday
    int monthMode = 0;     // 0 = day of month, 1 = nth weekday
    int nth = 1;           // 1..4, 5 = last
    int nthDay = 1;        // weekday 0..6
    int endMode = 0;       // 0 never, 1 until date, 2 count
    MIN_T until = 0;
    int count = 10;
};
struct Event {
    int id = 0; std::wstring uid;
    int calendarId = 0;
    std::wstring title, location, description, category;
    MIN_T start = 0;       // wall clock in tz
    int duration = 60;     // minutes
    bool allDay = false;
    std::wstring tz;       // Windows tz key, "*" floating
    Recurrence rec;
    std::vector<int> exdates;   // excluded day indexes (event wall)
    int busy = BUSY_BUSY;
    bool autoDecline = false; std::wstring declineMsg;
    std::wstring organizer, organizerName;   // empty = me
    int myResponse = -1;   // -1 = I'm organizer, else RSVP_*
    std::vector<Attendee> attendees;
    int confProvider = CONF_NONE; std::wstring confLink;
    std::vector<Reminder> reminders;
    MIN_T modified = 0;
};
struct Member { std::wstring email; int perm = 0; };   // 0 view, 1 edit
struct Calendar {
    int id = 0; std::wstring name; COLORREF color = RGB(0, 120, 215); bool visible = true;
    int accountId = 0;
    std::wstring sharedPath, owner; std::vector<Member> members;
    FILETIME sharedStamp{};
};
struct Category { std::wstring name; COLORREF color; };
struct Account { int id = 0; std::wstring name; int provider = PROV_GOOGLE; std::wstring email, url; int calendarId = 0; MIN_T lastSync = 0; bool autoSync = true; std::wstring status; };
struct Task { int id = 0; std::wstring title, notes; int dueDay = 0; bool hasDue = false; bool done = false; int priority = 1; /*0 low 1 normal 2 high*/ };
struct Settings {
    std::wstring userName = L"Me", userEmail = L"me@example.com";
    bool autoTz = true; std::wstring tzKey;
    bool showSecondTz = false; std::wstring secondTz;
    int weekStart = 0; int workStart = 8, workEnd = 17;
    int defaultDuration = 60; int defaultCalendar = 0;
    int defaultReminder = 15; bool remindPopup = true, remindSound = true, remindEmail = false;
    int defaultConf = CONF_TEAMS; bool autoAddConf = true; std::wstring confRoom[CONF_COUNT];
    bool autoDecline = true; std::wstring declineMsg = L"I'm in a focus block / out of office and can't attend. Please propose another time.";
    int focusLength = 120;
    int view = 1; bool showCompleted = false;
};
struct Model {
    std::vector<Calendar> calendars; std::vector<Category> categories;
    std::vector<Account> accounts; std::vector<Event> events; std::vector<Task> tasks;
    Settings set; std::set<std::wstring> fired;
    int nextId = 1;
};
extern Model g;
extern HINSTANCE g_hinst;
extern HWND g_hwnd;
extern int g_dpi;
inline int S(int v) { return MulDiv(v, g_dpi, 96); }

struct Occ { int id; MIN_T wall; MIN_T start, end; MIN_T utc; bool allDay; };

int NewId();
std::wstring NewUid();
Event* FindEvent(int id);
Event* FindEventByUid(const std::wstring& uid);
Calendar* FindCalendar(int id);
Task* FindTask(int id);
Account* FindAccount(int id);
const Category* FindCategory(const std::wstring& name);
COLORREF EventColor(const Event& e);
bool CalendarReadOnly(const Calendar* c);
bool EventReadOnly(const Event& e);
MIN_T EventToDisplay(const Event& e, MIN_T wall);
MIN_T EventToUtc(const Event& e, MIN_T wall);
int  EffectiveUntilDay(const Event& e);
bool OccursOnDay(const Event& e, int day, int untilDay);
void ExpandEvent(const Event& e, MIN_T from, MIN_T to, std::vector<Occ>& out);
std::vector<Occ> Expand(MIN_T from, MIN_T to, bool onlyVisible = true);
bool FindFocusConflict(const Event& ev, std::wstring* blockTitle);
std::wstring RecurrenceText(const Recurrence& r, MIN_T start);
Event NewEventTemplate(MIN_T displayStart, bool allDay);
int  DefaultCalendarId();
void LoadModel();
void SaveModel();
bool ReloadSharedCalendars(bool force);
bool LoadSharedCalendarFile(Calendar& c);
std::wstring DataFolder();

// strings & formatting
std::wstring ToW(const std::string& s);
std::string ToU8(const std::wstring& s);
std::wstring Lower(std::wstring s);
std::wstring Trim(const std::wstring& s);
std::wstring FmtDate(MIN_T m, const wchar_t* fmt = nullptr);   // fmt: GetDateFormatEx picture or null = short date
std::wstring FmtTime(MIN_T m);
std::wstring FmtDuration(int minutes);
std::wstring FmtReminder(int minutes);
extern const wchar_t* kConfNames[CONF_COUNT];
extern const wchar_t* kProviderNames[PROV_COUNT];
extern const wchar_t* kRsvpNames[5];
extern const wchar_t* kBusyNames[5];
std::wstring GenerateConfLink(int provider);
int DetectConfProvider(const std::wstring& link);

// ---------------------------------------------------------------- ics / net / mail (ics.cpp)
struct IcsResult { std::wstring method; std::vector<Event> events; };
std::string BuildIcs(const std::vector<const Event*>& evs, const char* method, const Attendee* replyAs = nullptr, const std::wstring& comment = L"", MIN_T counterStart = 0);
bool ParseIcs(const std::string& text, IcsResult& out);
bool HttpGet(const std::wstring& url, std::string& body, std::wstring& err);
bool ReadFileBytes(const std::wstring& path, std::string& out);
bool WriteFileBytes(const std::wstring& path, const std::string& data);
bool SendMail(const std::vector<std::pair<std::wstring, std::wstring>>& to, const std::wstring& subject, const std::wstring& body,
              const std::string* ics, const std::wstring& icsName, bool dialog, HWND owner);
bool SyncAccount(Account& a, std::wstring& msg);
void SendInvitations(const Event& e, HWND owner, bool cancel = false);
void SendResponse(Event& e, int response, HWND owner, MIN_T proposeStart = 0, const std::wstring& comment = L"", bool silent = false);
int  ImportIcsFile(const std::wstring& path, HWND owner, int targetCal);
bool ExportIcsFile(const std::wstring& path, int calendarId);

// ---------------------------------------------------------------- views (views.cpp)
enum { VIEW_DAY = 0, VIEW_WEEK, VIEW_MONTH, VIEW_YEAR, VIEW_AGENDA };
extern int g_viewMode, g_curDay, g_selEvent;
extern MIN_T g_selWall;
extern MIN_T g_newAt;
HWND CreateCalView(HWND parent);
void View_Refresh();
void View_Navigate(int dir);
void View_SetMode(int mode);
void View_ScrollToWork();
void View_CreateFonts();

// ---------------------------------------------------------------- dialogs (dialogs.cpp)
bool EventDialog(HWND owner, Event& ev, bool isNew, int startPage);
bool OptionsDialog(HWND owner, int startPage);
void OrganizerDialog(HWND owner, int startPage);
void SearchDialog(HWND owner, const std::wstring& initial);
void MeetingsDialog(HWND owner, int startPage);
bool EditTaskDlg(HWND owner, Task& t, bool isNew);
void TasksManagerDialog(HWND owner);
bool GoToDateDialog(HWND owner, int& day);
bool ProposeTimeDialog(HWND owner, MIN_T& start, int duration, std::wstring* comment = nullptr);
void Reminders_Show(HWND owner);
void Reminders_Add(const Occ& o, int minutesBefore);
void Reminders_Tick();
HWND Reminders_Window();
bool PickColor(HWND owner, COLORREF& c);
HIMAGELIST MakeColorImageList(const std::vector<COLORREF>& colors, int size);
extern bool g_evSendInvites;
MIN_T NextOccurrence(const Event& e, MIN_T* wall = nullptr);
std::wstring FmtWhen(const Event& e, MIN_T displayStart);

// ---------------------------------------------------------------- app (main.cpp)
#define WM_APP_SELCHANGED (WM_APP + 1)
#define WM_APP_OPENSEL    (WM_APP + 2)
#define WM_APP_NEWAT      (WM_APP + 3)
#define WM_APP_CONTEXT    (WM_APP + 4)
#define WM_APP_GOTODAY    (WM_APP + 5)
#define WM_APP_TRAY       (WM_APP + 6)
void App_ModelChanged();
void App_EditEvent(HWND owner, int id, MIN_T wall, int page);
enum { NEW_EVENT = 0, NEW_MEETING, NEW_FOCUS, NEW_OOO, NEW_ALLDAY };
void App_NewEvent(HWND owner, int kind);
void App_SyncAll(HWND owner, bool interactive);
void App_GoToDay(int day, int view);
void App_SetStatus(const std::wstring& s);
void App_Notify(const std::wstring& title, const std::wstring& text);
