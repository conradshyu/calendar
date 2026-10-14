// WinCalendar - data model, time zones, recurrence, persistence
#include "app.h"
#include <objbase.h>
#include <icu.h>
#pragma comment(lib, "icu.lib")

Model g;

// ============================================================ dates
int MakeDay(int y, int m, int d) {
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int)doe - 719468 + 134774;
}
void DayYMD(int day, int& y, int& m, int& d) {
    int z = day - 134774 + 719468;
    int era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = yy + (m <= 2);
}
int DaysInMonth(int y, int m) {
    static const int dm[] = { 31,28,31,30,31,30,31,31,30,31,30,31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return dm[m - 1];
}
int AddMonths(int day, int n) {
    int y, m, d; DayYMD(day, y, m, d);
    int t = y * 12 + (m - 1) + n; y = t / 12; m = t % 12 + 1;
    return MakeDay(y, m, std::min(d, DaysInMonth(y, m)));
}
MIN_T StToMin(const SYSTEMTIME& st) { return MakeMin(st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute); }
SYSTEMTIME MinToSt(MIN_T m) {
    SYSTEMTIME st{}; int y, mo, d; int day = DayOf(m); DayYMD(day, y, mo, d);
    st.wYear = (WORD)y; st.wMonth = (WORD)mo; st.wDay = (WORD)d; st.wDayOfWeek = (WORD)Weekday(day);
    int t = TimeOf(m); st.wHour = (WORD)(t / 60); st.wMinute = (WORD)(t % 60);
    return st;
}
MIN_T NowUtc() {
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (MIN_T)(u.QuadPart / 600000000ULL);
}
MIN_T NowDisplay() { return UtcToZone(NowUtc(), L""); }
int TodayDisplay() { return DayOf(NowDisplay()); }

// ============================================================ time zones
static std::vector<TzInfo> s_tz;
static std::wstring s_sysKey;
static DYNAMIC_TIME_ZONE_INFORMATION s_sysDtzi;

const std::vector<TzInfo>& AllTimeZones() {
    if (s_tz.empty()) {
        DYNAMIC_TIME_ZONE_INFORMATION d;
        for (DWORD i = 0; EnumDynamicTimeZoneInformation(i, &d) == ERROR_SUCCESS; ++i) {
            TzInfo t; t.key = d.TimeZoneKeyName; t.dtzi = d;
            wchar_t buf[256]; DWORD cb = sizeof(buf);
            std::wstring sub = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Time Zones\\" + t.key;
            if (RegGetValueW(HKEY_LOCAL_MACHINE, sub.c_str(), L"Display", RRF_RT_REG_SZ, nullptr, buf, &cb) == ERROR_SUCCESS) t.display = buf;
            else t.display = t.key;
            s_tz.push_back(t);
        }
        std::sort(s_tz.begin(), s_tz.end(), [](const TzInfo& a, const TzInfo& b) {
            if (a.dtzi.Bias != b.dtzi.Bias) return a.dtzi.Bias > b.dtzi.Bias;
            return a.display < b.display; });
    }
    return s_tz;
}
const TzInfo* FindTz(const std::wstring& key) {
    for (auto& t : AllTimeZones()) if (_wcsicmp(t.key.c_str(), key.c_str()) == 0) return &t;
    return nullptr;
}
void RefreshTimeZoneCache() { s_sysKey.clear(); }
std::wstring SystemTzKey() {
    if (s_sysKey.empty()) { GetDynamicTimeZoneInformation(&s_sysDtzi); s_sysKey = s_sysDtzi.TimeZoneKeyName; }
    return s_sysKey;
}
std::wstring DisplayTzKey() {
    if (!g.set.autoTz && !g.set.tzKey.empty() && FindTz(g.set.tzKey)) return g.set.tzKey;
    return SystemTzKey();
}
std::wstring TzDisplayName(const std::wstring& key) {
    if (key == TZ_FLOATING) return L"Floating (same clock time everywhere)";
    const TzInfo* t = FindTz(key); return t ? t->display : key;
}
static const DYNAMIC_TIME_ZONE_INFORMATION* GetDtzi(const std::wstring& key) {
    std::wstring k = (key.empty() || key == TZ_FLOATING) ? DisplayTzKey() : key;
    if (const TzInfo* t = FindTz(k)) return &t->dtzi;
    SystemTzKey(); return &s_sysDtzi;
}
MIN_T ZoneToUtc(MIN_T wall, const std::wstring& key) {
    SYSTEMTIME l = MinToSt(wall), u;
    if (!TzSpecificLocalTimeToSystemTimeEx(GetDtzi(key), &l, &u)) return wall;
    return StToMin(u);
}
MIN_T UtcToZone(MIN_T utc, const std::wstring& key) {
    SYSTEMTIME u = MinToSt(utc), l;
    if (!SystemTimeToTzSpecificLocalTimeEx(GetDtzi(key), &u, &l)) return utc;
    return StToMin(l);
}
int TzOffsetMinutes(const std::wstring& key, MIN_T utc) { return (int)(UtcToZone(utc, key) - utc); }

std::wstring WindowsFromIana(const std::wstring& iana) {
    UErrorCode st = U_ZERO_ERROR; UChar out[128];
    int n = ucal_getWindowsTimeZoneID((const UChar*)iana.c_str(), (int32_t)iana.size(), out, 128, &st);
    if (U_FAILURE(st) || n <= 0) return L"";
    return std::wstring((const wchar_t*)out, n);
}
std::wstring IanaFromWindows(const std::wstring& win) {
    UErrorCode st = U_ZERO_ERROR; UChar out[128];
    int n = ucal_getTimeZoneIDForWindowsID((const UChar*)win.c_str(), (int32_t)win.size(), nullptr, out, 128, &st);
    if (U_FAILURE(st) || n <= 0) return L"";
    return std::wstring((const wchar_t*)out, n);
}

// ============================================================ lookups
int NewId() { return g.nextId++; }
std::wstring NewUid() {
    GUID gd; CoCreateGuid(&gd); wchar_t b[64];
    swprintf_s(b, L"%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x@wincal", gd.Data1, gd.Data2, gd.Data3,
        gd.Data4[0], gd.Data4[1], gd.Data4[2], gd.Data4[3], gd.Data4[4], gd.Data4[5], gd.Data4[6], gd.Data4[7]);
    return b;
}
Event* FindEvent(int id) { for (auto& e : g.events) if (e.id == id) return &e; return nullptr; }
Event* FindEventByUid(const std::wstring& uid) { for (auto& e : g.events) if (e.uid == uid) return &e; return nullptr; }
Calendar* FindCalendar(int id) { for (auto& c : g.calendars) if (c.id == id) return &c; return nullptr; }
Task* FindTask(int id) { for (auto& t : g.tasks) if (t.id == id) return &t; return nullptr; }
Account* FindAccount(int id) { for (auto& a : g.accounts) if (a.id == id) return &a; return nullptr; }
const Category* FindCategory(const std::wstring& n) {
    if (n.empty()) return nullptr;
    for (auto& c : g.categories) if (_wcsicmp(c.name.c_str(), n.c_str()) == 0) return &c;
    return nullptr;
}
COLORREF EventColor(const Event& e) {
    if (const Category* c = FindCategory(e.category)) return c->color;
    if (Calendar* c = FindCalendar(e.calendarId)) return c->color;
    return RGB(0, 120, 215);
}
bool CalendarReadOnly(const Calendar* c) {
    if (!c) return false;
    if (c->accountId) return true;
    if (c->sharedPath.empty() || c->members.empty()) return false;
    std::wstring me = Lower(g.set.userEmail);
    if (c->owner.empty() || Lower(c->owner) == me) return false;
    for (auto& m : c->members) if (Lower(m.email) == me) return m.perm == 0;
    return true;
}
bool EventReadOnly(const Event& e) { return CalendarReadOnly(FindCalendar(e.calendarId)); }
int DefaultCalendarId() {
    Calendar* c = FindCalendar(g.set.defaultCalendar);
    if (c && !CalendarReadOnly(c)) return c->id;
    for (auto& k : g.calendars) if (!CalendarReadOnly(&k)) return k.id;
    return g.calendars.empty() ? 0 : g.calendars[0].id;
}

MIN_T EventToDisplay(const Event& e, MIN_T wall) {
    if (e.allDay || e.tz == TZ_FLOATING || e.tz.empty()) return wall;
    if (_wcsicmp(e.tz.c_str(), DisplayTzKey().c_str()) == 0) return wall;
    return UtcToZone(ZoneToUtc(wall, e.tz), L"");
}
MIN_T EventToUtc(const Event& e, MIN_T wall) {
    if (e.allDay || e.tz == TZ_FLOATING || e.tz.empty()) return ZoneToUtc(wall, L"");
    return ZoneToUtc(wall, e.tz);
}

// ============================================================ recurrence
static bool OccursRaw(const Event& e, int day) {
    int sd = DayOf(e.start);
    if (day < sd) return false;
    const Recurrence& r = e.rec;
    if (r.freq == 0) return day == sd;
    int iv = std::max(1, r.interval);
    int y, m, d, y0, m0, d0; DayYMD(day, y, m, d); DayYMD(sd, y0, m0, d0);
    switch (r.freq) {
    case 1: return (day - sd) % iv == 0;
    case 2: {
        int mask = r.weekMask ? r.weekMask : (1 << Weekday(sd));
        if (!(mask & (1 << Weekday(day)))) return false;
        int w = ((day - Weekday(day)) - (sd - Weekday(sd))) / 7;
        return w % iv == 0;
    }
    case 3: {
        int md = (y * 12 + m) - (y0 * 12 + m0);
        if (md % iv) return false;
        if (r.monthMode == 0) return d == d0;
        if (Weekday(day) != r.nthDay) return false;
        if (r.nth == 5) return d + 7 > DaysInMonth(y, m);
        return (d - 1) / 7 + 1 == r.nth;
    }
    case 4: return m == m0 && d == d0 && (y - y0) % iv == 0;
    }
    return false;
}
int EffectiveUntilDay(const Event& e) {
    const Recurrence& r = e.rec; int sd = DayOf(e.start);
    if (!r.freq) return sd;
    if (r.endMode == 1) return DayOf(r.until);
    if (r.endMode == 2) {
        int c = 0;
        for (int d = sd; d < sd + 366 * 100; ++d) if (OccursRaw(e, d) && ++c >= std::max(1, r.count)) return d;
    }
    return INT_MAX;
}
bool OccursOnDay(const Event& e, int day, int untilDay) {
    if (day > untilDay || !OccursRaw(e, day)) return false;
    return std::find(e.exdates.begin(), e.exdates.end(), day) == e.exdates.end();
}
void ExpandEvent(const Event& e, MIN_T from, MIN_T to, std::vector<Occ>& out) {
    int dur = std::max(0, e.duration);
    int tod = TimeOf(e.start);
    int d0 = DayOf(from) - 2 - dur / 1440, d1 = DayOf(to) + 1;
    int sd = DayOf(e.start);
    if (d0 < sd) d0 = sd;
    if (!e.rec.freq) d1 = std::min(d1, sd);
    int until = EffectiveUntilDay(e);
    if (d1 > until) d1 = until;
    for (int d = d0; d <= d1; ++d) {
        if (!OccursOnDay(e, d, until)) continue;
        Occ o; o.id = e.id; o.wall = (MIN_T)d * DAYMIN + tod; o.allDay = e.allDay;
        o.start = EventToDisplay(e, o.wall); o.end = o.start + dur; o.utc = EventToUtc(e, o.wall);
        bool hit = dur == 0 ? (o.start >= from && o.start < to) : (o.end > from && o.start < to);
        if (hit) out.push_back(o);
    }
}
std::vector<Occ> Expand(MIN_T from, MIN_T to, bool onlyVisible) {
    std::vector<Occ> out;
    for (auto& e : g.events) {
        Calendar* c = FindCalendar(e.calendarId);
        if (onlyVisible && c && !c->visible) continue;
        ExpandEvent(e, from, to, out);
    }
    std::sort(out.begin(), out.end(), [](const Occ& a, const Occ& b) {
        if (a.allDay != b.allDay) return a.allDay;
        if (a.start != b.start) return a.start < b.start;
        return (a.end - a.start) > (b.end - b.start); });
    return out;
}
bool FindFocusConflict(const Event& ev, std::wstring* blockTitle) {
    if (!g.set.autoDecline) return false;
    std::vector<Occ> occs;
    MIN_T s = EventToDisplay(ev, ev.start);
    ExpandEvent(ev, s - DAYMIN, s + DAYMIN * 366, occs);
    if (occs.size() > 30) occs.resize(30);
    for (auto& f : g.events) {
        if (f.id == ev.id || f.uid == ev.uid || !f.autoDecline || (f.busy != BUSY_FOCUS && f.busy != BUSY_OOO)) continue;
        for (auto& o : occs) {
            std::vector<Occ> hit; ExpandEvent(f, o.start, std::max(o.end, o.start + 1), hit);
            if (!hit.empty()) { if (blockTitle) *blockTitle = f.title; return true; }
        }
    }
    return false;
}
std::wstring RecurrenceText(const Recurrence& r, MIN_T start) {
    if (!r.freq) return L"Does not repeat";
    static const wchar_t* units[] = { L"", L"day", L"week", L"month", L"year" };
    static const wchar_t* days[] = { L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat" };
    static const wchar_t* longDays[] = { L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday" };
    static const wchar_t* nths[] = { L"", L"first", L"second", L"third", L"fourth", L"last" };
    std::wstring s = L"Every ";
    if (r.interval > 1) s += std::to_wstring(r.interval) + L" " + units[r.freq] + L"s";
    else s += units[r.freq];
    if (r.freq == 2) {
        int mask = r.weekMask ? r.weekMask : (1 << Weekday(DayOf(start)));
        if (mask == 0x3E) s += L" on weekdays";
        else { s += L" on "; bool first = true; for (int i = 0; i < 7; i++) if (mask & (1 << i)) { if (!first) s += L", "; s += days[i]; first = false; } }
    } else if (r.freq == 3) {
        if (r.monthMode == 0) { int y, m, d; DayYMD(DayOf(start), y, m, d); s += L" on day " + std::to_wstring(d); }
        else s += std::wstring(L" on the ") + nths[std::clamp(r.nth, 1, 5)] + L" " + longDays[r.nthDay % 7];
    } else if (r.freq == 4) s += L" on " + FmtDate(start, L"MMMM d");
    if (r.endMode == 1) s += L", until " + FmtDate(r.until);
    else if (r.endMode == 2) s += L", " + std::to_wstring(r.count) + L" times";
    return s;
}
Event NewEventTemplate(MIN_T displayStart, bool allDay) {
    Event e; e.id = 0; e.uid = NewUid(); e.calendarId = DefaultCalendarId();
    e.start = allDay ? (MIN_T)DayOf(displayStart) * DAYMIN : displayStart;
    e.allDay = allDay; e.duration = allDay ? 1440 : g.set.defaultDuration;
    e.tz = DisplayTzKey();
    if (g.set.defaultReminder >= 0) e.reminders.push_back({ g.set.defaultReminder, g.set.remindEmail ? REM_BOTH : REM_POPUP });
    return e;
}

// ============================================================ strings / formatting
std::wstring ToW(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); return w;
}
std::string ToU8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr); return s;
}
std::wstring Lower(std::wstring s) { if (!s.empty()) CharLowerBuffW(&s[0], (DWORD)s.size()); return s; }
std::wstring Trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n"); if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n"); return s.substr(a, b - a + 1);
}
std::wstring FmtDate(MIN_T m, const wchar_t* fmt) {
    SYSTEMTIME st = MinToSt(m); wchar_t b[128] = L"";
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, fmt ? 0 : DATE_SHORTDATE, &st, fmt, b, 128, nullptr); return b;
}
std::wstring FmtTime(MIN_T m) {
    SYSTEMTIME st = MinToSt(m); wchar_t b[64] = L"";
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, b, 64); return b;
}
std::wstring FmtDuration(int mins) {
    if (mins % 1440 == 0 && mins >= 1440) return std::to_wstring(mins / 1440) + (mins == 1440 ? L" day" : L" days");
    std::wstring s; if (mins >= 60) s += std::to_wstring(mins / 60) + L" h";
    if (mins % 60) { if (!s.empty()) s += L" "; s += std::to_wstring(mins % 60) + L" min"; }
    return s.empty() ? L"0 min" : s;
}
std::wstring FmtReminder(int m) {
    if (m == 0) return L"At start time";
    if (m % 10080 == 0) return std::to_wstring(m / 10080) + (m == 10080 ? L" week before" : L" weeks before");
    if (m % 1440 == 0) return std::to_wstring(m / 1440) + (m == 1440 ? L" day before" : L" days before");
    if (m % 60 == 0) return std::to_wstring(m / 60) + (m == 60 ? L" hour before" : L" hours before");
    return std::to_wstring(m) + L" minutes before";
}
const wchar_t* kConfNames[CONF_COUNT] = { L"(none)", L"Microsoft Teams", L"Zoom", L"Google Meet", L"Webex", L"Jitsi Meet", L"Custom link" };
const wchar_t* kProviderNames[PROV_COUNT] = { L"Local (this PC)", L"Google Workspace / Gmail", L"Apple iCloud", L"Microsoft Exchange / Microsoft 365", L"Outlook.com", L"Other ICS / webcal feed" };
const wchar_t* kRsvpNames[5] = { L"No response", L"Accepted", L"Declined", L"Tentative", L"Proposed new time" };
const wchar_t* kBusyNames[5] = { L"Free", L"Busy", L"Tentative", L"Out of office", L"Focus time" };

static std::wstring RandLetters(int n) {
    std::wstring s; for (int i = 0; i < n; i++) { unsigned v; rand_s(&v); s += (wchar_t)(L'a' + v % 26); } return s;
}
static std::wstring RandDigits(int n) {
    std::wstring s; for (int i = 0; i < n; i++) { unsigned v; rand_s(&v); s += (wchar_t)(L'0' + v % 10); } return s;
}
std::wstring GenerateConfLink(int p) {
    if (p > 0 && p < CONF_COUNT && !Trim(g.set.confRoom[p]).empty()) return Trim(g.set.confRoom[p]);
    switch (p) {
    case CONF_TEAMS: return L"https://teams.microsoft.com/l/meetup-join/19%3ameeting_" + RandLetters(24) + L"%40thread.v2/0";
    case CONF_ZOOM: return L"https://zoom.us/j/" + RandDigits(11) + L"?pwd=" + RandLetters(16);
    case CONF_MEET: return L"https://meet.google.com/" + RandLetters(3) + L"-" + RandLetters(4) + L"-" + RandLetters(3);
    case CONF_WEBEX: return L"https://meet.webex.com/meet/j.php?MTID=m" + RandLetters(20);
    case CONF_JITSI: return L"https://meet.jit.si/WinCalendar-" + RandLetters(10);
    }
    return L"";
}
int DetectConfProvider(const std::wstring& link) {
    std::wstring l = Lower(link);
    if (l.find(L"teams.microsoft.com") != std::wstring::npos || l.find(L"teams.live.com") != std::wstring::npos) return CONF_TEAMS;
    if (l.find(L"zoom.us") != std::wstring::npos) return CONF_ZOOM;
    if (l.find(L"meet.google.com") != std::wstring::npos) return CONF_MEET;
    if (l.find(L"webex.com") != std::wstring::npos) return CONF_WEBEX;
    if (l.find(L"meet.jit.si") != std::wstring::npos) return CONF_JITSI;
    return l.empty() ? CONF_NONE : CONF_CUSTOM;
}

// ============================================================ persistence
static std::wstring Esc(const std::wstring& v) {
    std::wstring o;
    for (wchar_t c : v) {
        if (c < 0x20 || c == L'%' || c == L'\t' || c == L'|' || c == L';' || c == L'=') { wchar_t b[8]; swprintf_s(b, L"%%%02X", (unsigned)c); o += b; }
        else o += c;
    }
    return o;
}
static std::wstring Unesc(const std::wstring& v) {
    std::wstring o;
    for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == L'%' && i + 2 < v.size()) { o += (wchar_t)wcstol(v.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
        else o += v[i];
    }
    return o;
}
static std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep) {
    std::vector<std::wstring> r; size_t p = 0;
    if (s.empty()) return r;
    for (;;) { size_t q = s.find(sep, p); r.push_back(s.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p)); if (q == std::wstring::npos) break; p = q + 1; }
    return r;
}
static std::wstring JoinList(const std::vector<std::vector<std::wstring>>& rows) {
    std::wstring o;
    for (size_t i = 0; i < rows.size(); i++) {
        if (i) o += L';';
        for (size_t j = 0; j < rows[i].size(); j++) { if (j) o += L'|'; o += Esc(rows[i][j]); }
    }
    return o;
}
static std::vector<std::vector<std::wstring>> SplitList(const std::wstring& s) {
    std::vector<std::vector<std::wstring>> r;
    for (auto& row : Split(s, L';')) { std::vector<std::wstring> f; for (auto& x : Split(row, L'|')) f.push_back(Unesc(x)); r.push_back(f); }
    return r;
}
struct RecW {
    std::wstring s;
    explicit RecW(const wchar_t* t) : s(t) {}
    RecW& operator()(const wchar_t* k, const std::wstring& v) { s += L'\t'; s += k; s += L'='; s += Esc(v); return *this; }
    RecW& n(const wchar_t* k, long long v) { return (*this)(k, std::to_wstring(v)); }
};
typedef std::map<std::wstring, std::wstring> RecR;
static std::wstring F(const RecR& r, const wchar_t* k, const wchar_t* def = L"") { auto it = r.find(k); return it == r.end() ? def : it->second; }
static long long N(const RecR& r, const wchar_t* k, long long def = 0) { auto it = r.find(k); return it == r.end() || it->second.empty() ? def : _wtoi64(it->second.c_str()); }
static bool ParseLine(const std::wstring& line, std::wstring& type, RecR& r) {
    auto parts = Split(line, L'\t'); if (parts.empty()) return false;
    type = parts[0]; r.clear();
    for (size_t i = 1; i < parts.size(); i++) { size_t e = parts[i].find(L'='); if (e != std::wstring::npos) r[parts[i].substr(0, e)] = Unesc(parts[i].substr(e + 1)); }
    return true;
}
static std::wstring EventRec(const Event& e) {
    std::vector<std::vector<std::wstring>> att, rem; std::wstring ex;
    for (auto& a : e.attendees) att.push_back({ a.name, a.email, std::to_wstring(a.status), std::to_wstring(a.proposed), a.optional ? L"1" : L"0" });
    for (auto& r : e.reminders) rem.push_back({ std::to_wstring(r.minutes), std::to_wstring(r.method) });
    for (size_t i = 0; i < e.exdates.size(); i++) { if (i) ex += L","; ex += std::to_wstring(e.exdates[i]); }
    RecW w(L"EVENT");
    w.n(L"id", e.id)(L"uid", e.uid).n(L"cal", e.calendarId)(L"title", e.title)(L"loc", e.location)(L"desc", e.description)(L"cat", e.category)
        .n(L"start", e.start).n(L"dur", e.duration).n(L"allday", e.allDay)(L"tz", e.tz)
        .n(L"rf", e.rec.freq).n(L"ri", e.rec.interval).n(L"rm", e.rec.weekMask).n(L"rmm", e.rec.monthMode).n(L"rn", e.rec.nth).n(L"rnd", e.rec.nthDay)
        .n(L"re", e.rec.endMode).n(L"ru", e.rec.until).n(L"rc", e.rec.count)(L"ex", ex)
        .n(L"busy", e.busy).n(L"adec", e.autoDecline)(L"dmsg", e.declineMsg)(L"org", e.organizer)(L"orgn", e.organizerName).n(L"myr", e.myResponse)
        .n(L"conf", e.confProvider)(L"link", e.confLink)(L"att", JoinList(att))(L"rem", JoinList(rem)).n(L"mod", e.modified);
    return w.s;
}
static Event EventFromRec(const RecR& r) {
    Event e;
    e.id = (int)N(r, L"id"); e.uid = F(r, L"uid"); e.calendarId = (int)N(r, L"cal");
    e.title = F(r, L"title"); e.location = F(r, L"loc"); e.description = F(r, L"desc"); e.category = F(r, L"cat");
    e.start = N(r, L"start"); e.duration = (int)N(r, L"dur", 60); e.allDay = N(r, L"allday") != 0; e.tz = F(r, L"tz");
    e.rec.freq = (int)N(r, L"rf"); e.rec.interval = (int)N(r, L"ri", 1); e.rec.weekMask = (int)N(r, L"rm"); e.rec.monthMode = (int)N(r, L"rmm");
    e.rec.nth = (int)N(r, L"rn", 1); e.rec.nthDay = (int)N(r, L"rnd", 1); e.rec.endMode = (int)N(r, L"re"); e.rec.until = N(r, L"ru"); e.rec.count = (int)N(r, L"rc", 10);
    for (auto& x : Split(F(r, L"ex"), L',')) if (!x.empty()) e.exdates.push_back(_wtoi(x.c_str()));
    e.busy = (int)N(r, L"busy", 1); e.autoDecline = N(r, L"adec") != 0; e.declineMsg = F(r, L"dmsg");
    e.organizer = F(r, L"org"); e.organizerName = F(r, L"orgn"); e.myResponse = (int)N(r, L"myr", -1);
    e.confProvider = (int)N(r, L"conf"); e.confLink = F(r, L"link"); e.modified = N(r, L"mod");
    for (auto& a : SplitList(F(r, L"att"))) if (a.size() >= 5) { Attendee x; x.name = a[0]; x.email = a[1]; x.status = _wtoi(a[2].c_str()); x.proposed = _wtoi64(a[3].c_str()); x.optional = a[4] == L"1"; e.attendees.push_back(x); }
    for (auto& a : SplitList(F(r, L"rem"))) if (a.size() >= 2) e.reminders.push_back({ _wtoi(a[0].c_str()), _wtoi(a[1].c_str()) });
    if (e.uid.empty()) e.uid = NewUid();
    return e;
}
static std::wstring MembersStr(const std::vector<Member>& m) {
    std::vector<std::vector<std::wstring>> rows; for (auto& x : m) rows.push_back({ x.email, std::to_wstring(x.perm) }); return JoinList(rows);
}
static std::vector<Member> MembersFrom(const std::wstring& s) {
    std::vector<Member> r; for (auto& a : SplitList(s)) if (a.size() >= 2) r.push_back({ a[0], _wtoi(a[1].c_str()) }); return r;
}

std::wstring DataFolder() {
    PWSTR p = nullptr; std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p))) { dir = p; CoTaskMemFree(p); }
    dir += L"\\WinCalendar"; CreateDirectoryW(dir.c_str(), nullptr); return dir;
}
static std::wstring DataFile() { return DataFolder() + L"\\calendar.dat"; }

static bool ReadLines(const std::wstring& path, std::vector<std::wstring>& lines) {
    std::string raw; if (!ReadFileBytes(path, raw)) return false;
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF) raw = raw.substr(3);
    std::wstring w = ToW(raw); lines.clear();
    for (auto& l : Split(w, L'\n')) { std::wstring x = l; if (!x.empty() && x.back() == L'\r') x.pop_back(); if (!x.empty()) lines.push_back(x); }
    return true;
}
static bool WriteLinesAtomic(const std::wstring& path, const std::vector<std::wstring>& lines) {
    std::wstring all; for (auto& l : lines) { all += l; all += L"\r\n"; }
    std::wstring tmp = path + L".tmp";
    if (!WriteFileBytes(tmp, ToU8(all))) return false;
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED) != 0;
}
static FILETIME FileStamp(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA d{}; FILETIME z{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &d)) return z;
    return d.ftLastWriteTime;
}

static void SaveSharedCalendar(Calendar& c) {
    std::vector<std::wstring> lines;
    lines.push_back(L"WINCAL-SHARED\t1");
    lines.push_back(RecW(L"CALENDAR")(L"name", c.name).n(L"color", c.color)(L"owner", c.owner)(L"members", MembersStr(c.members)).s);
    for (auto& e : g.events) if (e.calendarId == c.id) lines.push_back(EventRec(e));
    if (WriteLinesAtomic(c.sharedPath, lines)) c.sharedStamp = FileStamp(c.sharedPath);
}
bool LoadSharedCalendarFile(Calendar& c) {
    std::vector<std::wstring> lines;
    if (!ReadLines(c.sharedPath, lines) || lines.empty() || lines[0].rfind(L"WINCAL-SHARED", 0) != 0) return false;
    g.events.erase(std::remove_if(g.events.begin(), g.events.end(), [&](const Event& e) { return e.calendarId == c.id; }), g.events.end());
    std::wstring type; RecR r;
    for (size_t i = 1; i < lines.size(); i++) {
        if (!ParseLine(lines[i], type, r)) continue;
        if (type == L"CALENDAR") { c.owner = F(r, L"owner"); c.members = MembersFrom(F(r, L"members")); if (c.name.empty()) c.name = F(r, L"name"); if (r.count(L"color")) c.color = (COLORREF)N(r, L"color"); }
        else if (type == L"EVENT") { Event e = EventFromRec(r); e.id = NewId(); e.calendarId = c.id; g.events.push_back(e); }
    }
    c.sharedStamp = FileStamp(c.sharedPath);
    return true;
}
bool ReloadSharedCalendars(bool force) {
    bool changed = false;
    for (auto& c : g.calendars) {
        if (c.sharedPath.empty()) continue;
        FILETIME ft = FileStamp(c.sharedPath);
        if (ft.dwLowDateTime == 0 && ft.dwHighDateTime == 0) continue;
        if (force || CompareFileTime(&ft, &c.sharedStamp) != 0) if (LoadSharedCalendarFile(c)) changed = true;
    }
    return changed;
}

void SaveModel() {
    std::vector<std::wstring> L;
    L.push_back(L"WINCAL\t1");
    const Settings& s = g.set;
    RecW w(L"SET");
    w.n(L"next", g.nextId)(L"uname", s.userName)(L"uemail", s.userEmail).n(L"autotz", s.autoTz)(L"tz", s.tzKey).n(L"show2", s.showSecondTz)(L"tz2", s.secondTz)
        .n(L"wstart", s.weekStart).n(L"work0", s.workStart).n(L"work1", s.workEnd).n(L"ddur", s.defaultDuration).n(L"dcal", s.defaultCalendar)
        .n(L"drem", s.defaultReminder).n(L"rpop", s.remindPopup).n(L"rsnd", s.remindSound).n(L"rmail", s.remindEmail)
        .n(L"dconf", s.defaultConf).n(L"autoconf", s.autoAddConf).n(L"adec", s.autoDecline)(L"dmsg", s.declineMsg).n(L"flen", s.focusLength)
        .n(L"view", s.view).n(L"showdone", s.showCompleted);
    for (int i = 1; i < CONF_COUNT; i++) w((L"room" + std::to_wstring(i)).c_str(), s.confRoom[i]);
    L.push_back(w.s);
    for (auto& c : g.categories) L.push_back(RecW(L"CAT")(L"name", c.name).n(L"color", c.color).s);
    for (auto& c : g.calendars)
        L.push_back(RecW(L"CALENDAR").n(L"id", c.id)(L"name", c.name).n(L"color", c.color).n(L"vis", c.visible).n(L"acct", c.accountId)
            (L"shared", c.sharedPath)(L"owner", c.owner)(L"members", MembersStr(c.members)).s);
    for (auto& a : g.accounts)
        L.push_back(RecW(L"ACCOUNT").n(L"id", a.id)(L"name", a.name).n(L"prov", a.provider)(L"email", a.email)(L"url", a.url).n(L"cal", a.calendarId)
            .n(L"last", a.lastSync).n(L"auto", a.autoSync)(L"status", a.status).s);
    for (auto& t : g.tasks)
        L.push_back(RecW(L"TASK").n(L"id", t.id)(L"title", t.title)(L"notes", t.notes).n(L"due", t.dueDay).n(L"hasdue", t.hasDue).n(L"done", t.done).n(L"prio", t.priority).s);
    for (auto& e : g.events) {
        Calendar* c = FindCalendar(e.calendarId);
        if (c && !c->sharedPath.empty()) continue;
        L.push_back(EventRec(e));
    }
    MIN_T cutoff = NowUtc() - DAYMIN * 14;
    for (auto& f : g.fired) {
        size_t p = f.rfind(L'|'); MIN_T t = p == std::wstring::npos ? 0 : _wtoi64(f.c_str() + p + 1);
        if (t > cutoff) L.push_back(RecW(L"FIRED")(L"k", f).s);
    }
    WriteLinesAtomic(DataFile(), L);
    for (auto& c : g.calendars) if (!c.sharedPath.empty() && !CalendarReadOnly(&c)) SaveSharedCalendar(c);
}

static void CreateSampleData();

void LoadModel() {
    std::vector<std::wstring> lines;
    if (!ReadLines(DataFile(), lines) || lines.empty() || lines[0].rfind(L"WINCAL", 0) != 0) { CreateSampleData(); SaveModel(); return; }
    std::wstring type; RecR r;
    for (size_t i = 1; i < lines.size(); i++) {
        if (!ParseLine(lines[i], type, r)) continue;
        if (type == L"SET") {
            Settings& s = g.set;
            g.nextId = (int)N(r, L"next", 1); s.userName = F(r, L"uname", L"Me"); s.userEmail = F(r, L"uemail"); s.autoTz = N(r, L"autotz", 1) != 0;
            s.tzKey = F(r, L"tz"); s.showSecondTz = N(r, L"show2") != 0; s.secondTz = F(r, L"tz2"); s.weekStart = (int)N(r, L"wstart");
            s.workStart = (int)N(r, L"work0", 8); s.workEnd = (int)N(r, L"work1", 17); s.defaultDuration = (int)N(r, L"ddur", 60);
            s.defaultCalendar = (int)N(r, L"dcal"); s.defaultReminder = (int)N(r, L"drem", 15); s.remindPopup = N(r, L"rpop", 1) != 0;
            s.remindSound = N(r, L"rsnd", 1) != 0; s.remindEmail = N(r, L"rmail") != 0; s.defaultConf = (int)N(r, L"dconf", 1);
            s.autoAddConf = N(r, L"autoconf", 1) != 0; s.autoDecline = N(r, L"adec", 1) != 0; s.declineMsg = F(r, L"dmsg", s.declineMsg.c_str());
            s.focusLength = (int)N(r, L"flen", 120); s.view = (int)N(r, L"view", 1); s.showCompleted = N(r, L"showdone") != 0;
            for (int k = 1; k < CONF_COUNT; k++) s.confRoom[k] = F(r, (L"room" + std::to_wstring(k)).c_str());
        } else if (type == L"CAT") g.categories.push_back({ F(r, L"name"), (COLORREF)N(r, L"color") });
        else if (type == L"CALENDAR") {
            Calendar c; c.id = (int)N(r, L"id"); c.name = F(r, L"name"); c.color = (COLORREF)N(r, L"color"); c.visible = N(r, L"vis", 1) != 0;
            c.accountId = (int)N(r, L"acct"); c.sharedPath = F(r, L"shared"); c.owner = F(r, L"owner"); c.members = MembersFrom(F(r, L"members"));
            g.calendars.push_back(c);
        } else if (type == L"ACCOUNT") {
            Account a; a.id = (int)N(r, L"id"); a.name = F(r, L"name"); a.provider = (int)N(r, L"prov"); a.email = F(r, L"email"); a.url = F(r, L"url");
            a.calendarId = (int)N(r, L"cal"); a.lastSync = N(r, L"last"); a.autoSync = N(r, L"auto", 1) != 0; a.status = F(r, L"status");
            g.accounts.push_back(a);
        } else if (type == L"TASK") {
            Task t; t.id = (int)N(r, L"id"); t.title = F(r, L"title"); t.notes = F(r, L"notes"); t.dueDay = (int)N(r, L"due"); t.hasDue = N(r, L"hasdue") != 0;
            t.done = N(r, L"done") != 0; t.priority = (int)N(r, L"prio", 1); g.tasks.push_back(t);
        } else if (type == L"EVENT") g.events.push_back(EventFromRec(r));
        else if (type == L"FIRED") g.fired.insert(F(r, L"k"));
    }
    int maxId = 0;
    for (auto& c : g.calendars) maxId = std::max(maxId, c.id);
    for (auto& a : g.accounts) maxId = std::max(maxId, a.id);
    for (auto& t : g.tasks) maxId = std::max(maxId, t.id);
    for (auto& e : g.events) maxId = std::max(maxId, e.id);
    if (g.nextId <= maxId) g.nextId = maxId + 1;
    for (auto& c : g.calendars) if (!c.sharedPath.empty()) LoadSharedCalendarFile(c);
    if (g.calendars.empty()) { Calendar c; c.id = NewId(); c.name = L"Personal"; g.calendars.push_back(c); }
}

static Event& AddSample(int cal, const wchar_t* title, MIN_T start, int dur, const wchar_t* loc = L"", const wchar_t* cat = L"") {
    Event e; e.id = NewId(); e.uid = NewUid(); e.calendarId = cal; e.title = title; e.start = start; e.duration = dur;
    e.location = loc; e.category = cat; e.tz = DisplayTzKey(); e.reminders.push_back({ 15, REM_POPUP });
    g.events.push_back(e); return g.events.back();
}
static void CreateSampleData() {
    g = Model();
    g.set.userName = L"Alex Morgan"; g.set.userEmail = L"alex@example.com";
    g.categories = { { L"Work", RGB(0, 120, 215) }, { L"Personal", RGB(16, 137, 62) }, { L"Travel", RGB(202, 80, 16) },
                     { L"Important", RGB(209, 52, 56) }, { L"Health", RGB(136, 23, 152) } };
    Calendar personal; personal.id = NewId(); personal.name = L"Personal"; personal.color = RGB(16, 137, 62);
    Calendar work; work.id = NewId(); work.name = L"Work"; work.color = RGB(0, 120, 215);
    Calendar family; family.id = NewId(); family.name = L"Family Chores"; family.color = RGB(202, 80, 16);
    family.owner = g.set.userEmail; family.members = { { L"sam@example.com", 1 }, { L"jordan@example.com", 1 }, { L"grandma@example.com", 0 } };
    Calendar proj; proj.id = NewId(); proj.name = L"Project Apollo"; proj.color = RGB(135, 100, 184);
    proj.owner = g.set.userEmail; proj.members = { { L"priya@example.com", 1 }, { L"li@example.com", 0 } };
    g.calendars = { personal, work, family, proj };
    g.set.defaultCalendar = work.id;

    int today = TodayDisplay(); int wd = Weekday(today); int monday = today - ((wd + 6) % 7);
    MIN_T T = (MIN_T)today * DAYMIN;

    Event& standup = AddSample(work.id, L"Team stand-up", (MIN_T)monday * DAYMIN + 9 * 60 + 30, 15, L"Online", L"Work");
    standup.rec.freq = 2; standup.rec.weekMask = 0x3E; standup.confProvider = CONF_TEAMS; standup.confLink = GenerateConfLink(CONF_TEAMS);
    standup.attendees = { { L"Priya Shah", L"priya@example.com", RSVP_ACCEPTED }, { L"Li Wei", L"li@example.com", RSVP_ACCEPTED }, { L"Chris Park", L"chris@example.com", RSVP_TENTATIVE } };

    Event& focus = AddSample(work.id, L"Focus: deep work", (MIN_T)monday * DAYMIN + 13 * 60, 120, L"", L"");
    focus.rec.freq = 2; focus.rec.weekMask = (1 << 2) | (1 << 4); focus.busy = BUSY_FOCUS; focus.autoDecline = true; focus.declineMsg = g.set.declineMsg;
    focus.reminders.clear();

    Event& review = AddSample(proj.id, L"Apollo monthly review", MakeMin(0, 1, 1), 60, L"Conference room B", L"Work");
    { int y, m, d; DayYMD(today, y, m, d); int first = MakeDay(y, m, 1); int mon = first + (8 - Weekday(first)) % 7; review.start = (MIN_T)mon * DAYMIN + 15 * 60; }
    review.rec.freq = 3; review.rec.monthMode = 1; review.rec.nth = 1; review.rec.nthDay = 1;
    review.confProvider = CONF_ZOOM; review.confLink = GenerateConfLink(CONF_ZOOM);
    review.attendees = { { L"Priya Shah", L"priya@example.com", RSVP_ACCEPTED }, { L"Li Wei", L"li@example.com", RSVP_PROPOSED, T + DAYMIN * 2 + 16 * 60 }, { L"Dana Fox", L"dana@fabrikam.com", RSVP_DECLINED } };

    Event& trash = AddSample(family.id, L"Take out recycling", (MIN_T)monday * DAYMIN + 3 * DAYMIN + 19 * 60, 15, L"Home", L"");
    trash.rec.freq = 2; trash.rec.weekMask = 1 << 4;
    AddSample(family.id, L"Grocery run", T + DAYMIN * 5 + 10 * 60, 60, L"Market Street", L"Personal");
    AddSample(personal.id, L"Lunch with Sam", T + 12 * 60, 60, L"Caf\u00E9 Luna", L"Personal");
    Event& gym = AddSample(personal.id, L"Gym", (MIN_T)monday * DAYMIN + 18 * 60, 60, L"Fitness Center", L"Health");
    gym.rec.freq = 2; gym.rec.weekMask = (1 << 1) | (1 << 3) | (1 << 5);
    Event& dentist = AddSample(personal.id, L"Dentist appointment", T + DAYMIN * 3 + 8 * 60 + 30, 45, L"Smile Dental, 12 Oak Ave", L"Health");
    dentist.reminders = { { 1440, REM_EMAIL }, { 60, REM_POPUP } };

    Event& flight = AddSample(work.id, L"Flight NH 7 to Tokyo (local departure)", T + DAYMIN * 9 + 11 * 60, 11 * 60 + 30, L"SFO Terminal I", L"Travel");
    flight.tz = L"Pacific Standard Time"; if (!FindTz(flight.tz)) flight.tz = DisplayTzKey();
    Event& tokyo = AddSample(work.id, L"Customer workshop (Tokyo time)", T + DAYMIN * 11 + 10 * 60, 180, L"Contoso Japan, Shibuya", L"Travel");
    tokyo.tz = L"Tokyo Standard Time"; if (!FindTz(tokyo.tz)) tokyo.tz = DisplayTzKey();
    Event ooo = NewEventTemplate(T + DAYMIN * 14, true);
    ooo.id = NewId(); ooo.calendarId = work.id; ooo.title = L"Out of office - vacation"; ooo.duration = 1440 * 3; ooo.busy = BUSY_OOO;
    ooo.autoDecline = true; ooo.declineMsg = L"I'm on vacation with no access to email."; ooo.reminders.clear();
    g.events.push_back(ooo);

    Event& inv = AddSample(work.id, L"Quarterly planning (invitation)", T + DAYMIN * 2 + 14 * 60, 90, L"Building 4 / Teams", L"Work");
    inv.organizer = L"dana@fabrikam.com"; inv.organizerName = L"Dana Fox"; inv.myResponse = RSVP_NONE; inv.busy = BUSY_TENTATIVE;
    inv.attendees = { { g.set.userName, g.set.userEmail, RSVP_NONE }, { L"Priya Shah", L"priya@example.com", RSVP_ACCEPTED } };
    inv.confProvider = CONF_MEET; inv.confLink = GenerateConfLink(CONF_MEET);

    Event& bday = AddSample(family.id, L"Grandma's birthday", (MIN_T)(today + 6) * DAYMIN, 1440, L"", L"Important");
    bday.allDay = true; bday.rec.freq = 4; bday.reminders = { { 1440 * 2, REM_POPUP } };

    auto addTask = [](const wchar_t* t, int due, bool hasDue, int prio, bool done) { Task k; k.id = NewId(); k.title = t; k.dueDay = due; k.hasDue = hasDue; k.priority = prio; k.done = done; g.tasks.push_back(k); };
    addTask(L"Prepare Apollo status slides", today + 1, true, 2, false);
    addTask(L"Book hotel in Tokyo", today, true, 2, false);
    addTask(L"Renew passport", today - 1, true, 1, true);
    addTask(L"Buy birthday gift for Grandma", today + 4, true, 1, false);
    addTask(L"Read architecture RFC", 0, false, 0, false);
}
