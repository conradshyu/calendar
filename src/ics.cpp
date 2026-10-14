// WinCalendar - iCalendar (RFC 5545) import/export, WinHTTP sync, MAPI e-mail, invitations & RSVP
#include "app.h"
#include <winhttp.h>
#include <mapi.h>
#pragma comment(lib, "winhttp.lib")

// ============================================================ file helpers
bool ReadFileBytes(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
    out.resize((size_t)sz.QuadPart); DWORD rd = 0;
    BOOL ok = sz.QuadPart == 0 || ReadFile(h, &out[0], (DWORD)sz.QuadPart, &rd, nullptr);
    CloseHandle(h); out.resize(rd); return ok != 0;
}
bool WriteFileBytes(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wr = 0; BOOL ok = data.empty() || WriteFile(h, data.data(), (DWORD)data.size(), &wr, nullptr);
    CloseHandle(h); return ok && wr == data.size();
}

// ============================================================ ICS writing
static std::string IcsEsc(const std::wstring& s) {
    std::string o;
    for (wchar_t c : s) {
        if (c == L'\\') o += "\\\\"; else if (c == L';') o += "\\;"; else if (c == L',') o += "\\,";
        else if (c == L'\n') o += "\\n"; else if (c == L'\r') {}
        else o += ToU8(std::wstring(1, c));
    }
    return o;
}
static void Line(std::string& out, const std::string& l) {
    size_t i = 0, n = 0;   // fold at 75 octets, never split a UTF-8 sequence
    std::string cur;
    for (i = 0; i < l.size(); i++) {
        unsigned char c = (unsigned char)l[i];
        if (n >= 74 && (c & 0xC0) != 0x80) { out += cur + "\r\n "; cur.clear(); n = 1; }
        cur += l[i]; n++;
    }
    out += cur + "\r\n";
}
static std::string DtStr(MIN_T m, bool utc) {
    SYSTEMTIME s = MinToSt(m); char b[32];
    sprintf_s(b, "%04d%02d%02dT%02d%02d00%s", s.wYear, s.wMonth, s.wDay, s.wHour, s.wMinute, utc ? "Z" : "");
    return b;
}
static std::string DateStr(int day) { int y, m, d; DayYMD(day, y, m, d); char b[16]; sprintf_s(b, "%04d%02d%02d", y, m, d); return b; }
static std::string TzidFor(const std::wstring& key) { std::wstring i = IanaFromWindows(key); return ToU8(i.empty() ? key : i); }
static std::string OffStr(int minutes) { char b[16]; sprintf_s(b, "%c%02d%02d", minutes < 0 ? '-' : '+', abs(minutes) / 60, abs(minutes) % 60); return b; }
static std::string RuleDay(const SYSTEMTIME& s) {
    static const char* dn[] = { "SU","MO","TU","WE","TH","FR","SA" };
    char b[64]; sprintf_s(b, "FREQ=YEARLY;BYMONTH=%d;BYDAY=%d%s", s.wMonth, s.wDay == 5 ? -1 : s.wDay, dn[s.wDayOfWeek % 7]); return b;
}
static void WriteVTimezone(std::string& o, const std::wstring& key) {
    const TzInfo* t = FindTz(key); if (!t) return;
    SYSTEMTIME now; GetSystemTime(&now);
    TIME_ZONE_INFORMATION tzi; DYNAMIC_TIME_ZONE_INFORMATION d = t->dtzi;
    if (!GetTimeZoneInformationForYear(now.wYear, &d, &tzi)) return;
    int stdOff = -(tzi.Bias + tzi.StandardBias), dstOff = -(tzi.Bias + tzi.DaylightBias);
    Line(o, "BEGIN:VTIMEZONE"); Line(o, "TZID:" + TzidFor(key));
    if (tzi.StandardDate.wMonth == 0) {
        Line(o, "BEGIN:STANDARD"); Line(o, "DTSTART:16010101T000000");
        Line(o, "TZOFFSETFROM:" + OffStr(stdOff)); Line(o, "TZOFFSETTO:" + OffStr(stdOff)); Line(o, "END:STANDARD");
    } else {
        char b[40];
        Line(o, "BEGIN:STANDARD"); sprintf_s(b, "DTSTART:16010101T%02d%02d00", tzi.StandardDate.wHour, tzi.StandardDate.wMinute); Line(o, b);
        Line(o, "RRULE:" + RuleDay(tzi.StandardDate)); Line(o, "TZOFFSETFROM:" + OffStr(dstOff)); Line(o, "TZOFFSETTO:" + OffStr(stdOff)); Line(o, "END:STANDARD");
        Line(o, "BEGIN:DAYLIGHT"); sprintf_s(b, "DTSTART:16010101T%02d%02d00", tzi.DaylightDate.wHour, tzi.DaylightDate.wMinute); Line(o, b);
        Line(o, "RRULE:" + RuleDay(tzi.DaylightDate)); Line(o, "TZOFFSETFROM:" + OffStr(stdOff)); Line(o, "TZOFFSETTO:" + OffStr(dstOff)); Line(o, "END:DAYLIGHT");
    }
    Line(o, "END:VTIMEZONE");
}
static const char* PartStat(int s) {
    switch (s) { case RSVP_ACCEPTED: return "ACCEPTED"; case RSVP_DECLINED: return "DECLINED"; case RSVP_TENTATIVE: return "TENTATIVE"; }
    return "NEEDS-ACTION";
}
static std::string Mailto(const std::wstring& name, const std::wstring& email) {
    std::string s; if (!name.empty()) s += ";CN=\"" + ToU8(name) + "\"";
    return s + ":mailto:" + ToU8(email);
}
static std::string TriggerStr(int minutes) {
    char b[32];
    if (minutes % 1440 == 0 && minutes) sprintf_s(b, "-P%dD", minutes / 1440);
    else sprintf_s(b, "-PT%dM", minutes);
    return b;
}

enum TimeMode { TM_DATE, TM_FLOAT, TM_TZID, TM_UTC };
static TimeMode ModeFor(const Event& e) {
    if (e.allDay) return TM_DATE;
    if (e.tz == TZ_FLOATING) return TM_FLOAT;
    if (e.rec.freq && FindTz(e.tz)) return TM_TZID;
    return TM_UTC;
}
static std::string TimeProp(const char* name, const Event& e, MIN_T wall, TimeMode tm) {
    switch (tm) {
    case TM_DATE: return std::string(name) + ";VALUE=DATE:" + DateStr(DayOf(wall));
    case TM_FLOAT: return std::string(name) + ":" + DtStr(wall, false);
    case TM_TZID: return std::string(name) + ";TZID=" + TzidFor(e.tz) + ":" + DtStr(wall, false);
    default: return std::string(name) + ":" + DtStr(EventToUtc(e, wall), true);
    }
}

std::string BuildIcs(const std::vector<const Event*>& evs, const char* method, const Attendee* replyAs, const std::wstring& comment, MIN_T counterStart) {
    std::string o;
    Line(o, "BEGIN:VCALENDAR"); Line(o, "VERSION:2.0"); Line(o, "PRODID:-//WinCalendar//Win32 Ribbon Calendar//EN");
    Line(o, "CALSCALE:GREGORIAN"); Line(o, std::string("METHOD:") + method);
    std::set<std::wstring> tzs;
    for (auto e : evs) if (ModeFor(*e) == TM_TZID) tzs.insert(e->tz);
    for (auto& k : tzs) WriteVTimezone(o, k);
    std::string stamp = DtStr(NowUtc(), true);
    bool reply = strcmp(method, "REPLY") == 0 || strcmp(method, "COUNTER") == 0;
    for (auto pe : evs) {
        const Event& e = *pe; TimeMode tm = ModeFor(e);
        Line(o, "BEGIN:VEVENT");
        Line(o, "UID:" + ToU8(e.uid)); Line(o, "DTSTAMP:" + stamp);
        Line(o, "SEQUENCE:" + std::to_string(std::max<long long>(0, e.modified - 223000000)));
        MIN_T st = e.start;
        if (counterStart) { st = counterStart; if (tm == TM_TZID || tm == TM_UTC) tm = TM_FLOAT; }
        Line(o, TimeProp("DTSTART", e, st, tm));
        Line(o, TimeProp("DTEND", e, st + std::max(e.duration, e.allDay ? 1440 : 0), tm));
        if (counterStart && tm == TM_FLOAT) { /* proposed time given in display zone -> send as UTC */
            o.erase(o.rfind("DTSTART")); Line(o, "DTSTART:" + DtStr(ZoneToUtc(st, L""), true)); Line(o, "DTEND:" + DtStr(ZoneToUtc(st + e.duration, L""), true));
        }
        Line(o, "SUMMARY:" + IcsEsc(e.title));
        if (!e.location.empty()) Line(o, "LOCATION:" + IcsEsc(e.location));
        std::wstring desc = e.description;
        if (!e.confLink.empty()) { if (!desc.empty()) desc += L"\n\n"; desc += L"Join " + std::wstring(kConfNames[std::clamp(e.confProvider, 0, CONF_COUNT - 1)]) + L" meeting: " + e.confLink; }
        if (!desc.empty()) Line(o, "DESCRIPTION:" + IcsEsc(desc));
        if (!e.category.empty()) Line(o, "CATEGORIES:" + IcsEsc(e.category));
        if (e.rec.freq && !reply) {
            static const char* fq[] = { "", "DAILY", "WEEKLY", "MONTHLY", "YEARLY" };
            static const char* dn[] = { "SU","MO","TU","WE","TH","FR","SA" };
            std::string r = std::string("RRULE:FREQ=") + fq[e.rec.freq];
            if (e.rec.interval > 1) r += ";INTERVAL=" + std::to_string(e.rec.interval);
            if (e.rec.freq == 2) {
                int mask = e.rec.weekMask ? e.rec.weekMask : 1 << Weekday(DayOf(e.start)); std::string d;
                for (int i = 0; i < 7; i++) if (mask & (1 << i)) { if (!d.empty()) d += ","; d += dn[i]; }
                r += ";BYDAY=" + d;
            } else if (e.rec.freq == 3) {
                if (e.rec.monthMode == 1) r += ";BYDAY=" + std::to_string(e.rec.nth == 5 ? -1 : e.rec.nth) + dn[e.rec.nthDay % 7];
                else { int y, m, d; DayYMD(DayOf(e.start), y, m, d); r += ";BYMONTHDAY=" + std::to_string(d); }
            }
            if (e.rec.endMode == 1) {
                int ud = DayOf(e.rec.until);
                if (tm == TM_DATE || tm == TM_FLOAT) r += ";UNTIL=" + DateStr(ud);
                else r += ";UNTIL=" + DtStr(EventToUtc(e, (MIN_T)ud * DAYMIN + 1439), true);
            } else if (e.rec.endMode == 2) r += ";COUNT=" + std::to_string(e.rec.count);
            Line(o, r);
            for (int x : e.exdates) Line(o, TimeProp("EXDATE", e, (MIN_T)x * DAYMIN + TimeOf(e.start), tm));
        }
        std::wstring orgEmail = e.organizer.empty() ? g.set.userEmail : e.organizer;
        std::wstring orgName = e.organizer.empty() ? g.set.userName : e.organizerName;
        if (!e.attendees.empty() || reply) Line(o, "ORGANIZER" + Mailto(orgName, orgEmail));
        if (reply && replyAs) {
            Line(o, std::string("ATTENDEE;PARTSTAT=") + PartStat(replyAs->status) + Mailto(replyAs->name, replyAs->email));
            if (!comment.empty()) Line(o, "COMMENT:" + IcsEsc(comment));
        } else {
            for (auto& a : e.attendees)
                Line(o, std::string("ATTENDEE;ROLE=") + (a.optional ? "OPT-PARTICIPANT" : "REQ-PARTICIPANT") + ";PARTSTAT=" + PartStat(a.status) + ";RSVP=TRUE" + Mailto(a.name, a.email));
        }
        if (strcmp(method, "CANCEL") == 0) Line(o, "STATUS:CANCELLED");
        Line(o, e.busy == BUSY_FREE ? "TRANSP:TRANSPARENT" : "TRANSP:OPAQUE");
        static const char* cdo[] = { "FREE", "BUSY", "TENTATIVE", "OOF", "BUSY" };
        Line(o, std::string("X-MICROSOFT-CDO-BUSYSTATUS:") + cdo[std::clamp(e.busy, 0, 4)]);
        if (e.busy == BUSY_FOCUS) Line(o, "X-WINCAL-FOCUS:TRUE");
        if (e.autoDecline) Line(o, "X-WINCAL-AUTODECLINE:TRUE");
        if (!e.confLink.empty()) {
            Line(o, "CONFERENCE;VALUE=URI;FEATURE=VIDEO;LABEL=" + ToU8(kConfNames[std::clamp(e.confProvider, 0, CONF_COUNT - 1)]) + ":" + ToU8(e.confLink));
            Line(o, "X-WINCAL-CONFERENCE:" + ToU8(e.confLink));
            if (e.confProvider == CONF_TEAMS) Line(o, "X-MICROSOFT-SKYPETEAMSMEETINGURL:" + ToU8(e.confLink));
            if (e.confProvider == CONF_MEET) Line(o, "X-GOOGLE-CONFERENCE:" + ToU8(e.confLink));
        }
        if (!reply && strcmp(method, "CANCEL") != 0)
            for (auto& r : e.reminders) {
                Line(o, "BEGIN:VALARM"); Line(o, r.method == REM_EMAIL ? "ACTION:EMAIL" : "ACTION:DISPLAY");
                Line(o, "DESCRIPTION:" + IcsEsc(e.title)); Line(o, "TRIGGER:" + TriggerStr(r.minutes)); Line(o, "END:VALARM");
            }
        Line(o, "END:VEVENT");
    }
    Line(o, "END:VCALENDAR");
    return o;
}

// ============================================================ ICS parsing
namespace { struct Prop { std::wstring name; std::map<std::wstring, std::wstring> params; std::wstring value; }; }
static std::wstring IcsUnesc(const std::wstring& s) {
    std::wstring o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == L'\\' && i + 1 < s.size()) { wchar_t c = s[++i]; o += (c == L'n' || c == L'N') ? L'\n' : c; }
        else o += s[i];
    }
    return o;
}
static bool ParseProp(const std::wstring& line, Prop& p) {
    bool q = false; size_t colon = std::wstring::npos;
    for (size_t i = 0; i < line.size(); i++) { if (line[i] == L'"') q = !q; else if (line[i] == L':' && !q) { colon = i; break; } }
    if (colon == std::wstring::npos) return false;
    std::wstring head = line.substr(0, colon); p.value = line.substr(colon + 1); p.params.clear();
    std::vector<std::wstring> parts; std::wstring cur; q = false;
    for (wchar_t c : head) { if (c == L'"') { q = !q; continue; } if (c == L';' && !q) { parts.push_back(cur); cur.clear(); } else cur += c; }
    parts.push_back(cur);
    p.name = parts[0]; CharUpperBuffW(&p.name[0], (DWORD)p.name.size());
    for (size_t i = 1; i < parts.size(); i++) {
        size_t e = parts[i].find(L'='); if (e == std::wstring::npos) continue;
        std::wstring k = parts[i].substr(0, e); CharUpperBuffW(&k[0], (DWORD)k.size()); p.params[k] = parts[i].substr(e + 1);
    }
    return true;
}
// Returns wall time in the zone it resolved to (tz), or "*" for floating / date
static bool ParseDt(const std::wstring& v, const std::map<std::wstring, std::wstring>& params, MIN_T& wall, std::wstring& tz, bool& isDate) {
    if (v.size() < 8) return false;
    int y = _wtoi(v.substr(0, 4).c_str()), m = _wtoi(v.substr(4, 2).c_str()), d = _wtoi(v.substr(6, 2).c_str());
    if (y < 1601 || m < 1 || m > 12 || d < 1 || d > 31) return false;
    isDate = v.size() < 15 || (params.count(L"VALUE") && params.at(L"VALUE") == L"DATE");
    int hh = 0, mi = 0;
    if (!isDate) { hh = _wtoi(v.substr(9, 2).c_str()); mi = _wtoi(v.substr(11, 2).c_str()); }
    wall = MakeMin(y, m, d, hh, mi);
    if (isDate) { tz = TZ_FLOATING; return true; }
    if (v.back() == L'Z') { tz = DisplayTzKey(); wall = UtcToZone(wall, tz); return true; }
    auto it = params.find(L"TZID");
    if (it != params.end()) {
        std::wstring id = it->second; if (!id.empty() && id[0] == L'/') id = id.substr(1);
        if (FindTz(id)) { tz = FindTz(id)->key; return true; }
        std::wstring w = WindowsFromIana(id);
        if (!w.empty() && FindTz(w)) { tz = w; return true; }
        tz = DisplayTzKey(); return true;
    }
    tz = TZ_FLOATING; return true;
}
static int ParseDurationMin(const std::wstring& v) {
    int sign = 1, total = 0, num = 0; bool t = false;
    for (wchar_t c : v) {
        if (c == L'-') sign = -1; else if (c >= L'0' && c <= L'9') num = num * 10 + (c - L'0');
        else if (c == L'T') t = true;
        else if (c == L'W') { total += num * 10080; num = 0; } else if (c == L'D') { total += num * 1440; num = 0; }
        else if (c == L'H') { total += num * 60; num = 0; } else if (c == L'M' && t) { total += num; num = 0; } else if (c == L'S') { num = 0; }
    }
    return sign * total;
}
static int RsvpFrom(const std::wstring& ps) {
    if (ps == L"ACCEPTED") return RSVP_ACCEPTED; if (ps == L"DECLINED") return RSVP_DECLINED; if (ps == L"TENTATIVE") return RSVP_TENTATIVE;
    return RSVP_NONE;
}
static std::wstring StripMailto(const std::wstring& v) { return _wcsnicmp(v.c_str(), L"mailto:", 7) == 0 ? v.substr(7) : v; }
static std::wstring FindUrl(const std::wstring& text) {
    static const wchar_t* hosts[] = { L"teams.microsoft.com", L"teams.live.com", L"zoom.us/", L"meet.google.com", L"webex.com", L"meet.jit.si" };
    std::wstring l = Lower(text);
    for (auto h : hosts) {
        size_t p = l.find(h); if (p == std::wstring::npos) continue;
        size_t s = l.rfind(L"https://", p); if (s == std::wstring::npos || p - s > 20) continue;
        size_t e = s; while (e < text.size() && !iswspace(text[e]) && text[e] != L'>' && text[e] != L'"' && text[e] != L'<') e++;
        return text.substr(s, e - s);
    }
    return L"";
}

bool ParseIcs(const std::string& text, IcsResult& out) {
    std::wstring w = ToW(text.size() >= 3 && (unsigned char)text[0] == 0xEF ? text.substr(3) : text);
    std::vector<std::wstring> lines; std::wstring cur;
    size_t p = 0;
    while (p <= w.size()) {
        size_t q = w.find(L'\n', p); std::wstring l = w.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p);
        if (!l.empty() && l.back() == L'\r') l.pop_back();
        if (!l.empty() && (l[0] == L' ' || l[0] == L'\t')) cur += l.substr(1);
        else { if (!cur.empty()) lines.push_back(cur); cur = l; }
        if (q == std::wstring::npos) break; p = q + 1;
    }
    if (!cur.empty()) lines.push_back(cur);
    bool inEv = false, inAlarm = false, any = false; int depthOther = 0;
    Event e; Reminder rem; bool remOk = false; MIN_T endWall = 0; std::wstring endTz; bool hasEnd = false, endIsDate = false; int durMin = -1;
    std::wstring recurId; std::wstring busyCdo, transp;
    struct Pending { Event ev; std::wstring recurId; };
    std::vector<Pending> list;
    for (auto& l : lines) {
        Prop pr; if (!ParseProp(l, pr)) continue;
        if (pr.name == L"BEGIN") {
            std::wstring v = pr.value; CharUpperBuffW(&v[0], (DWORD)v.size());
            if (v == L"VCALENDAR") { any = true; continue; }
            if (v == L"VEVENT" && !inEv) { inEv = true; e = Event(); e.uid.clear(); e.reminders.clear(); hasEnd = false; durMin = -1; recurId.clear(); busyCdo.clear(); transp.clear(); e.tz = TZ_FLOATING; e.busy = BUSY_BUSY; continue; }
            if (v == L"VALARM" && inEv) { inAlarm = true; rem = Reminder(); remOk = false; continue; }
            depthOther++; continue;
        }
        if (pr.name == L"END") {
            std::wstring v = pr.value; CharUpperBuffW(&v[0], (DWORD)v.size());
            if (v == L"VALARM" && inAlarm) { inAlarm = false; if (remOk) e.reminders.push_back(rem); continue; }
            if (v == L"VEVENT" && inEv) {
                inEv = false;
                if (hasEnd) {
                    if (e.allDay) e.duration = (DayOf(endWall) - DayOf(e.start)) * 1440;
                    else {
                        MIN_T us = (e.tz == TZ_FLOATING) ? e.start : ZoneToUtc(e.start, e.tz);
                        MIN_T ue = (endTz == TZ_FLOATING) ? endWall : ZoneToUtc(endWall, endTz);
                        if (e.tz == TZ_FLOATING && endTz != TZ_FLOATING) us = ZoneToUtc(e.start, L"");
                        e.duration = (int)(ue - us);
                    }
                } else if (durMin >= 0) e.duration = durMin;
                else e.duration = e.allDay ? 1440 : 0;
                if (e.allDay && e.duration < 1440) e.duration = 1440;
                if (e.duration < 0) e.duration = 0;
                if (transp == L"TRANSPARENT") e.busy = BUSY_FREE;
                if (busyCdo == L"OOF") e.busy = BUSY_OOO; else if (busyCdo == L"TENTATIVE") e.busy = BUSY_TENTATIVE; else if (busyCdo == L"FREE") e.busy = BUSY_FREE;
                if (e.confLink.empty()) e.confLink = FindUrl(e.location + L" " + e.description);
                if (!e.confLink.empty()) e.confProvider = DetectConfProvider(e.confLink);
                if (e.uid.empty()) e.uid = NewUid();
                list.push_back({ e, recurId });
                continue;
            }
            if (depthOther) depthOther--; continue;
        }
        if (!inEv || depthOther) continue;
        if (inAlarm) {
            if (pr.name == L"TRIGGER") { int m = ParseDurationMin(pr.value); if (pr.value.find(L'P') != std::wstring::npos) { rem.minutes = std::max(0, -m); remOk = true; } }
            else if (pr.name == L"ACTION") rem.method = _wcsicmp(pr.value.c_str(), L"EMAIL") == 0 ? REM_EMAIL : REM_POPUP;
            continue;
        }
        const std::wstring& v = pr.value;
        if (pr.name == L"UID") e.uid = v;
        else if (pr.name == L"SUMMARY") e.title = IcsUnesc(v);
        else if (pr.name == L"LOCATION") e.location = IcsUnesc(v);
        else if (pr.name == L"DESCRIPTION") e.description = IcsUnesc(v);
        else if (pr.name == L"CATEGORIES") { std::wstring c = IcsUnesc(v); size_t k = c.find(L','); e.category = c.substr(0, k); }
        else if (pr.name == L"DTSTART") { bool isDate; if (ParseDt(v, pr.params, e.start, e.tz, isDate)) e.allDay = isDate; }
        else if (pr.name == L"DTEND") { if (ParseDt(v, pr.params, endWall, endTz, endIsDate)) hasEnd = true; }
        else if (pr.name == L"DURATION") durMin = ParseDurationMin(v);
        else if (pr.name == L"RECURRENCE-ID") recurId = v;
        else if (pr.name == L"TRANSP") transp = v;
        else if (pr.name == L"X-MICROSOFT-CDO-BUSYSTATUS") busyCdo = v;
        else if (pr.name == L"X-WINCAL-FOCUS") e.busy = BUSY_FOCUS;
        else if (pr.name == L"X-WINCAL-AUTODECLINE") e.autoDecline = true;
        else if (pr.name == L"COMMENT") e.declineMsg = IcsUnesc(v);
        else if (pr.name == L"URL" || pr.name == L"CONFERENCE" || pr.name == L"X-WINCAL-CONFERENCE" || pr.name == L"X-GOOGLE-CONFERENCE" || pr.name == L"X-MICROSOFT-SKYPETEAMSMEETINGURL") {
            if (e.confLink.empty() && v.rfind(L"http", 0) == 0) e.confLink = v;
        } else if (pr.name == L"ORGANIZER") {
            e.organizer = StripMailto(v); auto it = pr.params.find(L"CN"); if (it != pr.params.end()) e.organizerName = it->second;
        } else if (pr.name == L"ATTENDEE") {
            Attendee a; a.email = StripMailto(v);
            auto it = pr.params.find(L"CN"); if (it != pr.params.end()) a.name = it->second;
            it = pr.params.find(L"PARTSTAT"); if (it != pr.params.end()) a.status = RsvpFrom(it->second);
            it = pr.params.find(L"ROLE"); a.optional = it != pr.params.end() && it->second == L"OPT-PARTICIPANT";
            e.attendees.push_back(a);
        } else if (pr.name == L"EXDATE") {
            size_t s = 0;
            while (s <= v.size()) {
                size_t c = v.find(L',', s); std::wstring one = v.substr(s, c == std::wstring::npos ? std::wstring::npos : c - s);
                MIN_T xw; std::wstring xtz; bool xd;
                if (ParseDt(one, pr.params, xw, xtz, xd)) {
                    if (xtz != TZ_FLOATING && e.tz != TZ_FLOATING && xtz != e.tz) xw = UtcToZone(ZoneToUtc(xw, xtz), e.tz);
                    e.exdates.push_back(DayOf(xw));
                }
                if (c == std::wstring::npos) break; s = c + 1;
            }
        } else if (pr.name == L"RRULE") {
            Recurrence r; r.freq = 0;
            for (auto& part : [&] { std::vector<std::wstring> x; size_t s = 0; while (true) { size_t c = v.find(L';', s); x.push_back(v.substr(s, c == std::wstring::npos ? std::wstring::npos : c - s)); if (c == std::wstring::npos) break; s = c + 1; } return x; }()) {
                size_t eq = part.find(L'='); if (eq == std::wstring::npos) continue;
                std::wstring k = part.substr(0, eq), val = part.substr(eq + 1);
                if (k == L"FREQ") r.freq = val == L"DAILY" ? 1 : val == L"WEEKLY" ? 2 : val == L"MONTHLY" ? 3 : val == L"YEARLY" ? 4 : 0;
                else if (k == L"INTERVAL") r.interval = std::max(1, _wtoi(val.c_str()));
                else if (k == L"COUNT") { r.endMode = 2; r.count = _wtoi(val.c_str()); }
                else if (k == L"UNTIL") { MIN_T u; std::wstring utz; bool ud; std::map<std::wstring, std::wstring> np; if (ParseDt(val, np, u, utz, ud)) { r.endMode = 1; r.until = u; } }
                else if (k == L"BYDAY") {
                    static const wchar_t* dn[] = { L"SU",L"MO",L"TU",L"WE",L"TH",L"FR",L"SA" };
                    size_t s = 0;
                    while (true) {
                        size_t c = val.find(L',', s); std::wstring d = val.substr(s, c == std::wstring::npos ? std::wstring::npos : c - s);
                        if (d.size() >= 2) {
                            std::wstring code = d.substr(d.size() - 2); int n = d.size() > 2 ? _wtoi(d.substr(0, d.size() - 2).c_str()) : 0;
                            for (int i = 0; i < 7; i++) if (code == dn[i]) { r.weekMask |= 1 << i; if (n) { r.monthMode = 1; r.nth = n < 0 ? 5 : std::min(n, 4); r.nthDay = i; } }
                        }
                        if (c == std::wstring::npos) break; s = c + 1;
                    }
                } else if (k == L"BYMONTHDAY") r.monthMode = 0;
            }
            if (r.freq == 3 && r.monthMode == 1) r.weekMask = 0;
            if (r.freq == 1 && r.weekMask) { r.freq = 2; }   // DAILY;BYDAY=MO..FR -> weekly mask
            e.rec = r;
        }
    }
    // overridden instances (RECURRENCE-ID): exclude from master, keep as single events
    for (auto& pnd : list) {
        if (pnd.recurId.empty()) continue;
        MIN_T rw; std::wstring rtz; bool rd; std::map<std::wstring, std::wstring> np;
        if (!ParseDt(pnd.recurId, np, rw, rtz, rd)) continue;
        for (auto& m : list) if (m.recurId.empty() && m.ev.uid == pnd.ev.uid) m.ev.exdates.push_back(DayOf(rw));
        pnd.ev.uid += L"#" + std::to_wstring(DayOf(rw)); pnd.ev.rec = Recurrence();
    }
    for (auto& pnd : list) out.events.push_back(pnd.ev);
    for (auto& l : lines) if (_wcsnicmp(l.c_str(), L"METHOD:", 7) == 0) { out.method = Trim(l.substr(7)); CharUpperBuffW(&out.method[0], (DWORD)out.method.size()); break; }
    return any;
}

// ============================================================ HTTP (WinHTTP)
bool HttpGet(const std::wstring& urlIn, std::string& body, std::wstring& err) {
    std::wstring url = Trim(urlIn);
    if (_wcsnicmp(url.c_str(), L"webcal://", 9) == 0) url = L"https://" + url.substr(9);
    if (_wcsnicmp(url.c_str(), L"file:///", 8) == 0) url = url.substr(8);
    if (url.size() > 2 && (url[1] == L':' || url.rfind(L"\\\\", 0) == 0)) { if (ReadFileBytes(url, body)) return true; err = L"Cannot read file"; return false; }
    URL_COMPONENTS uc{ sizeof(uc) };
    wchar_t host[256] = L"", path[4096] = L"";
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 4096;
    wchar_t extra[4096] = L""; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 4096;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) { err = L"Invalid URL"; return false; }
    std::wstring fullPath = std::wstring(path) + extra;
    HINTERNET s = WinHttpOpen(L"WinCalendar/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { err = L"WinHttpOpen failed"; return false; }
    WinHttpSetTimeouts(s, 10000, 10000, 20000, 30000);
    bool ok = false;
    HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
    if (c) {
        HINTERNET r = WinHttpOpenRequest(c, L"GET", fullPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
        if (r) {
            DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
            WinHttpSetOption(r, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
            if (WinHttpSendRequest(r, L"Accept: text/calendar, */*\r\n", (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
                DWORD code = 0, sz = sizeof(code);
                WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(r, &avail) && avail) {
                    std::string buf(avail, 0); DWORD rd = 0;
                    if (!WinHttpReadData(r, &buf[0], avail, &rd)) break;
                    body.append(buf.data(), rd);
                }
                if (code == 200) ok = true; else err = L"HTTP status " + std::to_wstring(code);
            } else err = L"Request failed (error " + std::to_wstring(GetLastError()) + L")";
            WinHttpCloseHandle(r);
        }
        WinHttpCloseHandle(c);
    } else err = L"Cannot connect to host";
    WinHttpCloseHandle(s);
    return ok;
}

// ============================================================ e-mail (Simple MAPI, mailto fallback)
typedef ULONG(FAR PASCAL* PFN_MAPISENDMAILW)(LHANDLE, ULONG_PTR, lpMapiMessageW, FLAGS, ULONG);
static std::wstring UrlEnc(const std::wstring& s) {
    std::string u = ToU8(s); std::wstring o;
    for (unsigned char c : u) { if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '@') o += (wchar_t)c; else { wchar_t b[8]; swprintf_s(b, L"%%%02X", c); o += b; } }
    return o;
}
static std::wstring OutboxFolder() {
    std::wstring d = DataFolder() + L"\\Outbox"; CreateDirectoryW(d.c_str(), nullptr); return d;
}
bool SendMail(const std::vector<std::pair<std::wstring, std::wstring>>& to, const std::wstring& subject, const std::wstring& body,
              const std::string* ics, const std::wstring& icsName, bool dialog, HWND owner) {
    std::wstring icsPath;
    if (ics) {
        std::wstring safe = icsName; for (auto& c : safe) if (wcschr(L"\\/:*?\"<>|", c)) c = L'_';
        icsPath = OutboxFolder() + L"\\" + safe;
        WriteFileBytes(icsPath, *ics);
    }
    if (to.empty()) return false;
    ULONG rc = MAPI_E_FAILURE;
    HMODULE h = LoadLibraryW(L"mapi32.dll");
    if (h) {
        auto fn = (PFN_MAPISENDMAILW)GetProcAddress(h, "MAPISendMailW");
        if (fn) {
            std::vector<MapiRecipDescW> rec(to.size()); std::vector<std::wstring> addrs(to.size()), names(to.size());
            for (size_t i = 0; i < to.size(); i++) {
                names[i] = to[i].first.empty() ? to[i].second : to[i].first; addrs[i] = L"SMTP:" + to[i].second;
                rec[i] = {}; rec[i].ulRecipClass = MAPI_TO; rec[i].lpszName = &names[i][0]; rec[i].lpszAddress = &addrs[i][0];
            }
            MapiFileDescW file{}; std::wstring fname = icsName;
            file.nPosition = (ULONG)-1; file.lpszPathName = icsPath.empty() ? nullptr : &icsPath[0]; file.lpszFileName = &fname[0];
            std::wstring subj = subject, txt = body;
            MapiMessageW msg{}; msg.lpszSubject = &subj[0]; msg.lpszNoteText = &txt[0];
            msg.nRecipCount = (ULONG)rec.size(); msg.lpRecips = rec.data();
            if (ics) { msg.nFileCount = 1; msg.lpFiles = &file; }
            rc = fn(0, (ULONG_PTR)owner, &msg, MAPI_LOGON_UI | (dialog ? MAPI_DIALOG : 0), 0);
        }
        FreeLibrary(h);
    }
    if (rc == SUCCESS_SUCCESS) return true;
    if (rc == MAPI_USER_ABORT) return false;
    if (!dialog) { App_SetStatus(L"No MAPI mail client available - message saved to Outbox: " + icsPath); return false; }
    // Fallback: default mail handler via mailto:, and reveal the .ics for manual attachment
    std::wstring addr; for (auto& t : to) { if (!addr.empty()) addr += L","; addr += t.second; }
    std::wstring b = body; if (b.size() > 1200) b = b.substr(0, 1200) + L"...";
    if (!icsPath.empty()) b += L"\n\n(Attach the calendar file: " + icsPath + L")";
    std::wstring url = L"mailto:" + addr + L"?subject=" + UrlEnc(subject) + L"&body=" + UrlEnc(b);
    ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (!icsPath.empty()) {
        PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(icsPath.c_str());
        if (pidl) { SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0); ILFree(pidl); }
    }
    App_SetStatus(L"Opened your e-mail app; invitation file saved to " + icsPath);
    return true;
}

static std::wstring EventSummaryText(const Event& e) {
    MIN_T ds = EventToDisplay(e, e.start);
    std::wstring s = L"Title: " + e.title + L"\n";
    s += L"When: " + FmtDate(ds, L"dddd, MMMM d, yyyy") + (e.allDay ? L" (all day)" : L" " + FmtTime(ds) + L" - " + FmtTime(ds + e.duration)) + L"\n";
    if (!e.allDay && e.tz != TZ_FLOATING) s += L"Time zone: " + TzDisplayName(e.tz.empty() ? DisplayTzKey() : e.tz) + L"\n";
    if (e.rec.freq) s += L"Repeats: " + RecurrenceText(e.rec, e.start) + L"\n";
    if (!e.location.empty()) s += L"Where: " + e.location + L"\n";
    if (!e.confLink.empty()) s += L"\nJoin " + std::wstring(kConfNames[std::clamp(e.confProvider, 0, CONF_COUNT - 1)]) + L" meeting (one click):\n" + e.confLink + L"\n";
    if (!e.description.empty()) s += L"\n" + e.description + L"\n";
    return s;
}
void SendInvitations(const Event& e, HWND owner, bool cancel) {
    std::vector<std::pair<std::wstring, std::wstring>> to;
    for (auto& a : e.attendees) if (!a.email.empty() && Lower(a.email) != Lower(g.set.userEmail)) to.push_back({ a.name, a.email });
    if (to.empty()) { MessageBoxW(owner, L"This event has no attendees with e-mail addresses.", L"Send invitations", MB_ICONINFORMATION); return; }
    std::string ics = BuildIcs({ &e }, cancel ? "CANCEL" : "REQUEST");
    std::wstring subj = (cancel ? L"Canceled: " : L"Invitation: ") + e.title + L" @ " + FmtDate(EventToDisplay(e, e.start));
    std::wstring body = (cancel ? L"This meeting has been canceled.\n\n" : L"You have been invited to the following event. Open the attached invite.ics to add it to your calendar and respond.\n\n") + EventSummaryText(e);
    if (SendMail(to, subj, body, &ics, cancel ? L"cancel.ics" : L"invite.ics", true, owner)) App_SetStatus(L"Invitations sent for \"" + e.title + L"\"");
}
void SendResponse(Event& e, int response, HWND owner, MIN_T proposeStart, const std::wstring& comment, bool silent) {
    Attendee me; me.name = g.set.userName; me.email = g.set.userEmail; me.status = response == RSVP_PROPOSED ? RSVP_TENTATIVE : response;
    if (response != RSVP_PROPOSED) e.myResponse = response;
    for (auto& a : e.attendees) if (Lower(a.email) == Lower(g.set.userEmail)) a.status = me.status;
    if (response == RSVP_ACCEPTED) e.busy = BUSY_BUSY; else if (response == RSVP_TENTATIVE) e.busy = BUSY_TENTATIVE; else if (response == RSVP_DECLINED) e.busy = BUSY_FREE;
    if (e.organizer.empty() || Lower(e.organizer) == Lower(g.set.userEmail)) return;
    const char* method = response == RSVP_PROPOSED ? "COUNTER" : "REPLY";
    std::string ics = BuildIcs({ &e }, method, &me, comment, proposeStart);
    static const wchar_t* verbs[] = { L"", L"Accepted", L"Declined", L"Tentative", L"New time proposed" };
    std::wstring subj = std::wstring(verbs[response]) + L": " + e.title;
    std::wstring body = g.set.userName + L" has " + (response == RSVP_PROPOSED ? L"proposed a new time" : Lower(verbs[response])) + L" for \"" + e.title + L"\".\n";
    if (proposeStart) body += L"Proposed: " + FmtDate(proposeStart, L"dddd, MMMM d, yyyy") + L" " + FmtTime(proposeStart) + L"\n";
    if (!comment.empty()) body += L"\n" + comment + L"\n";
    SendMail({ { e.organizerName, e.organizer } }, subj, body, &ics, L"response.ics", !silent, owner);
}

// ============================================================ import / export / sync
int ImportIcsFile(const std::wstring& path, HWND owner, int targetCal) {
    std::string data; std::wstring err;
    if (!HttpGet(path, data, err)) { MessageBoxW(owner, (L"Cannot read " + path + L"\n" + err).c_str(), L"Import", MB_ICONERROR); return -1; }
    IcsResult r;
    if (!ParseIcs(data, r)) { MessageBoxW(owner, L"The file is not a valid iCalendar (.ics) file.", L"Import", MB_ICONERROR); return -1; }
    int n = 0; std::wstring report;
    std::wstring me = Lower(g.set.userEmail);
    if (r.method == L"REPLY" || r.method == L"COUNTER") {
        for (auto& ev : r.events) {
            Event* ex = FindEventByUid(ev.uid); if (!ex) continue;
            for (auto& a : ev.attendees) for (auto& b : ex->attendees) if (Lower(a.email) == Lower(b.email)) {
                if (r.method == L"COUNTER") { b.status = RSVP_PROPOSED; b.proposed = EventToDisplay(ev, ev.start); }
                else b.status = a.status;
                report += (b.name.empty() ? b.email : b.name) + L": " + kRsvpNames[b.status] + L"\n"; n++;
            }
        }
        MessageBoxW(owner, n ? (L"Updated responses:\n\n" + report).c_str() : L"No matching meeting or attendee found.", L"RSVP tracking", MB_ICONINFORMATION);
        return n;
    }
    if (r.method == L"CANCEL") {
        for (auto& ev : r.events) {
            size_t before = g.events.size();
            g.events.erase(std::remove_if(g.events.begin(), g.events.end(), [&](const Event& x) { return x.uid == ev.uid; }), g.events.end());
            n += (int)(before - g.events.size());
        }
        MessageBoxW(owner, (std::to_wstring(n) + L" canceled meeting(s) removed from your calendar.").c_str(), L"Import", MB_ICONINFORMATION);
        return n;
    }
    for (auto& ev : r.events) {
        bool invite = r.method == L"REQUEST" && !ev.organizer.empty() && Lower(ev.organizer) != me;
        Event* ex = FindEventByUid(ev.uid);
        int keepResp = ex ? ex->myResponse : RSVP_NONE;
        Event ne = ev;
        ne.calendarId = targetCal; ne.modified = NowUtc();
        if (ex) { ne.id = ex->id; ne.calendarId = ex->calendarId; }
        else ne.id = NewId();
        if (ne.reminders.empty() && g.set.defaultReminder >= 0) ne.reminders.push_back({ g.set.defaultReminder, REM_POPUP });
        ne.myResponse = invite ? keepResp : -1;
        if (invite && ne.myResponse == RSVP_NONE) ne.busy = BUSY_TENTATIVE;
        if (ex) *ex = ne; else g.events.push_back(ne);
        n++;
        if (invite && ne.myResponse == RSVP_NONE) {
            std::wstring block;
            if (FindFocusConflict(ne, &block)) {
                Event* stored = FindEvent(ne.id);
                std::wstring msg = g.set.declineMsg;
                for (auto& f : g.events) if (f.title == block && !f.declineMsg.empty()) msg = f.declineMsg;
                SendResponse(*stored, RSVP_DECLINED, owner, 0, msg, true);
                report += L"Auto-declined \"" + ne.title + L"\" (conflicts with \"" + block + L"\")\n";
                App_Notify(L"Invitation auto-declined", ne.title + L" conflicts with " + block);
            } else report += L"New invitation: \"" + ne.title + L"\" - respond from the Meetings tab\n";
        }
    }
    std::wstring msg = std::to_wstring(n) + L" event(s) imported.";
    if (!report.empty()) msg += L"\n\n" + report;
    MessageBoxW(owner, msg.c_str(), L"Import", MB_ICONINFORMATION);
    return n;
}
bool ExportIcsFile(const std::wstring& path, int calendarId) {
    std::vector<const Event*> evs;
    for (auto& e : g.events) if (calendarId < 0 || e.calendarId == calendarId) evs.push_back(&e);
    return WriteFileBytes(path, BuildIcs(evs, "PUBLISH"));
}
bool SyncAccount(Account& a, std::wstring& msg) {
    Calendar* c = FindCalendar(a.calendarId);
    if (!c) {
        static const COLORREF pc[] = { RGB(0,120,215), RGB(66,133,244), RGB(142,142,147), RGB(0,99,177), RGB(0,120,212), RGB(96,94,92) };
        Calendar nc; nc.id = NewId(); nc.name = a.name.empty() ? kProviderNames[a.provider] : a.name; nc.color = pc[a.provider % PROV_COUNT]; nc.accountId = a.id;
        g.calendars.push_back(nc); a.calendarId = nc.id; c = &g.calendars.back();
    }
    if (Trim(a.url).empty()) { msg = L"No calendar URL configured"; a.status = msg; return false; }
    std::string body; std::wstring err;
    if (!HttpGet(a.url, body, err)) { msg = err; a.status = L"Error: " + err; return false; }
    IcsResult r;
    if (!ParseIcs(body, r)) { msg = L"Response is not an iCalendar feed"; a.status = L"Error: " + msg; return false; }
    int cid = c->id;
    g.events.erase(std::remove_if(g.events.begin(), g.events.end(), [&](const Event& e) { return e.calendarId == cid; }), g.events.end());
    std::wstring me = Lower(a.email.empty() ? g.set.userEmail : a.email);
    for (auto& ev : r.events) {
        Event e = ev; e.id = NewId(); e.calendarId = cid;
        e.myResponse = -1;
        if (!e.organizer.empty() && Lower(e.organizer) != me) for (auto& at : e.attendees) if (Lower(at.email) == me) e.myResponse = at.status;
        g.events.push_back(e);
    }
    a.lastSync = NowUtc();
    a.status = L"OK - " + std::to_wstring(r.events.size()) + L" events";
    msg = a.status;
    return true;
}
