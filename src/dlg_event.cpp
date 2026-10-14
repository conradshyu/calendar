// WinCalendar - tabbed Event dialog (General, Recurrence, Time Zone, Attendees, Online Meeting, Reminders)
#include "dlgutil.h"
#include <prsht.h>

bool g_evSendInvites = false;

struct EvState {
    Event ev; bool isNew = false, ro = false, applied = false;
    MIN_T lastStart = 0;       // general page: start before the user edited it (to shift end)
    std::wstring tzPrev;       // tz page: zone currently selected (for "keep same moment")
};

static bool IsActivePage(HWND d) { return PropSheet_GetCurrentPageHwnd(GetParent(d)) == d; }
static std::wstring EffTz(const Event& e) { return e.tz.empty() ? DisplayTzKey() : e.tz; }

// Common notify handling: returns true when handled. load/save callbacks.
template <class L, class Sv>
static INT_PTR PageNotify(HWND d, LPARAM lp, EvState* s, L load, Sv save) {
    NMHDR* nh = (NMHDR*)lp;
    switch (nh->code) {
    case PSN_SETACTIVE: load(); SetMsgResult(d, 0); return TRUE;
    case PSN_KILLACTIVE: {
        bool ok = s->ro ? true : save();
        SetMsgResult(d, ok ? FALSE : TRUE); return TRUE;
    }
    case PSN_APPLY: {
        bool ok = true;
        if (!s->ro && IsActivePage(d)) ok = save();
        if (ok) s->applied = true;
        SetMsgResult(d, ok ? PSNRET_NOERROR : PSNRET_INVALID_NOCHANGEPAGE); return TRUE;
    }
    }
    return FALSE;
}

// ================================================================== General
static void GenUpdateSummary(HWND d, EvState* s) {
    bool ad = IsChecked(d, IDC_ALLDAY);
    MIN_T st = (MIN_T)DayOf(GetDtp(d, IDC_STARTDATE)) * DAYMIN + (ad ? 0 : TimeOf(GetDtp(d, IDC_STARTTIME)));
    MIN_T en = (MIN_T)DayOf(GetDtp(d, IDC_ENDDATE)) * DAYMIN + (ad ? 0 : TimeOf(GetDtp(d, IDC_ENDTIME)));
    std::wstring t;
    if (ad) { int days = DayOf(en) - DayOf(st) + 1; t = days >= 1 ? std::to_wstring(days) + (days == 1 ? L" day" : L" days") : L"End is before start"; }
    else t = en >= st ? L"Duration: " + FmtDuration((int)(en - st)) : L"End is before start";
    SetText(d, IDC_SUMMARY, t);
    Enable(d, IDC_STARTTIME, !ad && !s->ro); Enable(d, IDC_ENDTIME, !ad && !s->ro);
    std::wstring note;
    if (s->ev.tz == TZ_FLOATING) note = L"Floating time";
    else if (ad) note = L"All-day (not shifted)";
    else {
        std::wstring k = EffTz(s->ev);
        note = TzDisplayName(k);
        if (_wcsicmp(k.c_str(), DisplayTzKey().c_str()) != 0) note = L"Event zone: " + note;
    }
    SetText(d, IDC_TZNOTE, note);
}
static void GenLoad(HWND d, EvState* s) {
    Event& e = s->ev;
    SetText(d, IDC_TITLE, e.title); SetText(d, IDC_LOCATION, e.location); SetText(d, IDC_DESC, e.description);
    CbSelData(d, IDC_CALENDAR, e.calendarId);
    SetText(d, IDC_CATEGORY, e.category);
    CbSelData(d, IDC_BUSY, e.busy);
    Check(d, IDC_ALLDAY, e.allDay);
    SetDtp(d, IDC_STARTDATE, e.start); SetDtp(d, IDC_STARTTIME, e.start);
    MIN_T end = e.allDay ? e.start + std::max(DAYMIN, (MIN_T)e.duration) - DAYMIN : e.start + e.duration;
    SetDtp(d, IDC_ENDDATE, end); SetDtp(d, IDC_ENDTIME, end);
    s->lastStart = e.start;
    GenUpdateSummary(d, s);
}
static MIN_T GenStart(HWND d) {
    return (MIN_T)DayOf(GetDtp(d, IDC_STARTDATE)) * DAYMIN + (IsChecked(d, IDC_ALLDAY) ? 0 : TimeOf(GetDtp(d, IDC_STARTTIME)));
}
static bool GenSave(HWND d, EvState* s) {
    Event& e = s->ev;
    std::wstring title = Trim(DlgText(d, IDC_TITLE));
    bool ad = IsChecked(d, IDC_ALLDAY);
    MIN_T st = GenStart(d);
    MIN_T en = (MIN_T)DayOf(GetDtp(d, IDC_ENDDATE)) * DAYMIN + (ad ? 0 : TimeOf(GetDtp(d, IDC_ENDTIME)));
    if (en < st) { MessageBoxW(d, L"The end of the event must not be before its start.", L"Event", MB_ICONWARNING); return false; }
    e.title = title.empty() ? L"(No title)" : title;
    e.location = DlgText(d, IDC_LOCATION); e.description = DlgText(d, IDC_DESC);
    e.calendarId = (int)CbData(d, IDC_CALENDAR, e.calendarId);
    e.category = Trim(DlgText(d, IDC_CATEGORY));
    e.busy = (int)CbData(d, IDC_BUSY, e.busy);
    e.allDay = ad; e.start = st;
    e.duration = ad ? (int)((DayOf(en) - DayOf(st) + 1) * DAYMIN) : (int)(en - st);
    return true;
}
static INT_PTR CALLBACK GeneralProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    EvState* s = PageData<EvState>(d);
    switch (msg) {
    case WM_INITDIALOG: {
        SetPageData(d, lp); s = PageData<EvState>(d);
        for (auto& c : g.calendars)
            if (!CalendarReadOnly(&c) || c.id == s->ev.calendarId) CbAdd(d, IDC_CALENDAR, c.name, c.id);
        CbAdd(d, IDC_CATEGORY, L"", 0);
        for (auto& c : g.categories) CbAdd(d, IDC_CATEGORY, c.name, 0);
        for (int i = 0; i < 5; i++) CbAdd(d, IDC_BUSY, kBusyNames[i], i);
        SendDlgItemMessageW(d, IDC_TITLE, EM_SETCUEBANNER, TRUE, (LPARAM)L"Add a title");
        SendDlgItemMessageW(d, IDC_LOCATION, EM_SETCUEBANNER, TRUE, (LPARAM)L"Room, address or meeting link");
        GenLoad(d, s);
        if (s->ro) {
            DisableEditing(d);
            const Calendar* c = FindCalendar(s->ev.calendarId);
            SetText(d, IDC_READONLY, std::wstring(L"Read-only: ") + (c && c->accountId ? L"this event is synced from an account." : L"you have view-only access to this shared calendar."));
        } else if (s->ev.myResponse >= 0) SetText(d, IDC_READONLY, L"Invitation from " + (s->ev.organizerName.empty() ? s->ev.organizer : s->ev.organizerName) + L". Respond on the Attendees tab.");
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->code == DTN_DATETIMECHANGE) {
            if (nh->idFrom == IDC_STARTDATE || nh->idFrom == IDC_STARTTIME) {
                // keep the duration: move the end together with the start
                MIN_T ns = GenStart(d), delta = ns - s->lastStart;
                bool ad = IsChecked(d, IDC_ALLDAY);
                MIN_T en = (MIN_T)DayOf(GetDtp(d, IDC_ENDDATE)) * DAYMIN + (ad ? 0 : TimeOf(GetDtp(d, IDC_ENDTIME))) + delta;
                SetDtp(d, IDC_ENDDATE, en); SetDtp(d, IDC_ENDTIME, en);
                s->lastStart = ns;
            }
            GenUpdateSummary(d, s);
            return TRUE;
        }
        return PageNotify(d, lp, s, [&] { GenLoad(d, s); }, [&] { return GenSave(d, s); });
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_ALLDAY) {
            s->lastStart = GenStart(d);
            if (!IsChecked(d, IDC_ALLDAY) && TimeOf(GetDtp(d, IDC_STARTTIME)) == 0) {
                MIN_T st = (MIN_T)DayOf(GetDtp(d, IDC_STARTDATE)) * DAYMIN + 9 * 60;
                SetDtp(d, IDC_STARTTIME, st); SetDtp(d, IDC_ENDDATE, st + g.set.defaultDuration); SetDtp(d, IDC_ENDTIME, st + g.set.defaultDuration);
                s->lastStart = st;
            }
            GenUpdateSummary(d, s);
        }
        return TRUE;
    }
    return FALSE;
}

// ================================================================== Recurrence
static const wchar_t* kDayNames[7] = { L"Sunday", L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday" };
static void RecReadInto(HWND d, Recurrence& r) {
    r.freq = (int)CbData(d, IDC_FREQ, 0);
    r.interval = std::clamp((int)GetDlgItemInt(d, IDC_INTERVAL, nullptr, FALSE), 1, 999);
    r.weekMask = 0; for (int i = 0; i < 7; i++) if (IsChecked(d, IDC_WD0 + i)) r.weekMask |= 1 << i;
    r.monthMode = IsChecked(d, IDC_MONTH_NTH) ? 1 : 0;
    r.nth = (int)CbData(d, IDC_NTH, 1); r.nthDay = (int)CbData(d, IDC_NTHDAY, 1);
    r.endMode = IsChecked(d, IDC_END_UNTIL) ? 1 : IsChecked(d, IDC_END_COUNT) ? 2 : 0;
    r.until = (MIN_T)DayOf(GetDtp(d, IDC_UNTILDATE)) * DAYMIN;
    r.count = std::clamp((int)GetDlgItemInt(d, IDC_COUNT, nullptr, FALSE), 1, 9999);
}
static void RecUpdate(HWND d, EvState* s) {
    Recurrence r; RecReadInto(d, r);
    static const wchar_t* units[] = { L"", L"day(s)", L"week(s)", L"month(s)", L"year(s)" };
    SetText(d, IDC_UNITS, units[std::clamp(r.freq, 0, 4)]);
    bool en = !s->ro, any = r.freq != 0;
    Enable(d, IDC_INTERVAL, en && any); Enable(d, IDC_INTERVAL_SPIN, en && any);
    for (int i = 0; i < 7; i++) Enable(d, IDC_WD0 + i, en && r.freq == 2);
    Enable(d, IDC_FIRSTMON, en && r.freq == 2);
    bool m = r.freq == 3 || r.freq == 4;
    Enable(d, IDC_MONTH_DAY, en && m); Enable(d, IDC_MONTH_NTH, en && m);
    Enable(d, IDC_NTH, en && m && r.monthMode == 1); Enable(d, IDC_NTHDAY, en && m && r.monthMode == 1);
    for (int id : { IDC_END_NEVER, IDC_END_UNTIL, IDC_END_COUNT }) Enable(d, id, en && any);
    Enable(d, IDC_UNTILDATE, en && any && r.endMode == 1);
    Enable(d, IDC_COUNT, en && any && r.endMode == 2); Enable(d, IDC_COUNT_SPIN, en && any && r.endMode == 2);
    SetText(d, IDC_SUMMARY, any ? RecurrenceText(r, s->ev.start) : L"This event does not repeat.");
}
static void RecLoad(HWND d, EvState* s) {
    Recurrence r = s->ev.rec;
    int day = DayOf(s->ev.start), y, mo, dd; DayYMD(day, y, mo, dd);
    if (!r.weekMask) r.weekMask = 1 << Weekday(day);
    if (r.freq != 3 && r.freq != 4) { r.nth = std::min(5, (dd - 1) / 7 + 1); r.nthDay = Weekday(day); }
    if (!r.until) r.until = (MIN_T)AddMonths(day, 3) * DAYMIN;
    CbSelData(d, IDC_FREQ, r.freq);
    SetDlgItemInt(d, IDC_INTERVAL, std::max(1, r.interval), FALSE);
    for (int i = 0; i < 7; i++) Check(d, IDC_WD0 + i, (r.weekMask >> i) & 1);
    CheckRadioButton(d, IDC_MONTH_DAY, IDC_MONTH_NTH, r.monthMode ? IDC_MONTH_NTH : IDC_MONTH_DAY);
    SetText(d, IDC_MONTH_DAY, L"On day " + std::to_wstring(dd) + L" of the month");
    CbSelData(d, IDC_NTH, r.nth); CbSelData(d, IDC_NTHDAY, r.nthDay);
    CheckRadioButton(d, IDC_END_NEVER, IDC_END_COUNT, r.endMode == 1 ? IDC_END_UNTIL : r.endMode == 2 ? IDC_END_COUNT : IDC_END_NEVER);
    SetDtp(d, IDC_UNTILDATE, r.until); SetDlgItemInt(d, IDC_COUNT, std::max(1, r.count), FALSE);
    RecUpdate(d, s);
}
static bool RecSave(HWND d, EvState* s) {
    Recurrence r; RecReadInto(d, r);
    if (r.freq == 2 && !r.weekMask) r.weekMask = 1 << Weekday(DayOf(s->ev.start));
    if (r.freq && r.endMode == 1 && DayOf(r.until) < DayOf(s->ev.start)) {
        MessageBoxW(d, L"The end date of the series is before the first occurrence.", L"Recurrence", MB_ICONWARNING); return false;
    }
    if (!r.freq) { s->ev.rec = Recurrence(); return true; }
    s->ev.rec = r;
    return true;
}
static INT_PTR CALLBACK RecurProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    EvState* s = PageData<EvState>(d);
    switch (msg) {
    case WM_INITDIALOG: {
        SetPageData(d, lp); s = PageData<EvState>(d);
        const wchar_t* f[] = { L"Does not repeat", L"Daily", L"Weekly", L"Monthly", L"Yearly" };
        for (int i = 0; i < 5; i++) CbAdd(d, IDC_FREQ, f[i], i);
        const wchar_t* n[] = { L"first", L"second", L"third", L"fourth", L"last" };
        for (int i = 0; i < 5; i++) CbAdd(d, IDC_NTH, n[i], i + 1);
        for (int i = 0; i < 7; i++) CbAdd(d, IDC_NTHDAY, kDayNames[i], i);
        SendDlgItemMessageW(d, IDC_INTERVAL_SPIN, UDM_SETRANGE32, 1, 999);
        SendDlgItemMessageW(d, IDC_COUNT_SPIN, UDM_SETRANGE32, 1, 9999);
        RecLoad(d, s);
        if (s->ro) DisableEditing(d);
        return TRUE;
    }
    case WM_NOTIFY:
        if (((NMHDR*)lp)->code == DTN_DATETIMECHANGE) { RecUpdate(d, s); return TRUE; }
        return PageNotify(d, lp, s, [&] { RecLoad(d, s); }, [&] { return RecSave(d, s); });
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_FIRSTMON) { for (int i = 0; i < 7; i++) Check(d, IDC_WD0 + i, i >= 1 && i <= 5); }
        if (LOWORD(wp) == IDC_FREQ && HIWORD(wp) == CBN_SELCHANGE) {
            int f = (int)CbData(d, IDC_FREQ, 0);
            if (f == 3 || f == 4) {
                int day = DayOf(s->ev.start), y, mo, dd; DayYMD(day, y, mo, dd);
                if (CbData(d, IDC_NTH, -1) < 0) CbSelData(d, IDC_NTH, std::min(5, (dd - 1) / 7 + 1));
            }
        }
        if (s && (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == CBN_SELCHANGE || HIWORD(wp) == EN_CHANGE)) RecUpdate(d, s);
        return TRUE;
    }
    return FALSE;
}

// ================================================================== Time zone
static void TzPreview(HWND d, EvState* s) {
    bool floating = IsChecked(d, IDC_TZ_FLOATING);
    Enable(d, IDC_TZCOMBO, !floating && !s->ro); Enable(d, IDC_TZMINE, !floating && !s->ro); Enable(d, IDC_TZCONVERT, !floating && !s->ro);
    Event e = s->ev; e.tz = floating ? TZ_FLOATING : TzComboKey(d, IDC_TZCOMBO);
    std::wstring t;
    if (e.allDay) t = L"All-day events are date-based and appear on the same date in every time zone.\r\n\r\n";
    std::wstring when = FmtDate(e.start, L"ddd, MMM d") + L" " + FmtTime(e.start);
    if (floating) {
        t += L"Starts at " + FmtTime(e.start) + L" local time wherever you are.\r\nOn your calendar now: " + when + L" (" + TzDisplayName(DisplayTzKey()) + L")";
    } else {
        MIN_T utc = EventToUtc(e, e.start);
        t += L"Event time:  " + when + L"  " + TzDisplayName(e.tz) + L"\r\n";
        MIN_T disp = UtcToZone(utc, DisplayTzKey());
        t += L"Your calendar:  " + FmtDate(disp, L"ddd, MMM d") + L" " + FmtTime(disp) + L"  " + TzDisplayName(DisplayTzKey()) + L"\r\n";
        if (g.set.showSecondTz && !g.set.secondTz.empty()) {
            MIN_T sec = UtcToZone(utc, g.set.secondTz);
            t += L"Second zone:  " + FmtDate(sec, L"ddd, MMM d") + L" " + FmtTime(sec) + L"  " + TzDisplayName(g.set.secondTz) + L"\r\n";
        }
        t += L"UTC:  " + FmtDate(utc, L"ddd, MMM d") + L" " + FmtTime(utc);
    }
    SetText(d, IDC_TZPREVIEW, t);
}
static void TzLoad(HWND d, EvState* s) {
    bool floating = s->ev.tz == TZ_FLOATING;
    s->tzPrev = floating ? DisplayTzKey() : EffTz(s->ev);
    FillTzCombo(d, IDC_TZCOMBO, s->tzPrev);
    CheckRadioButton(d, IDC_TZ_PINNED, IDC_TZ_FLOATING, floating ? IDC_TZ_FLOATING : IDC_TZ_PINNED);
    TzPreview(d, s);
}
static bool TzSave(HWND d, EvState* s) {
    s->ev.tz = IsChecked(d, IDC_TZ_FLOATING) ? TZ_FLOATING : TzComboKey(d, IDC_TZCOMBO);
    if (s->ev.tz.empty()) s->ev.tz = DisplayTzKey();
    return true;
}
static void TzChanged(HWND d, EvState* s) {
    std::wstring nk = TzComboKey(d, IDC_TZCOMBO);
    if (nk.empty() || _wcsicmp(nk.c_str(), s->tzPrev.c_str()) == 0) { TzPreview(d, s); return; }
    if (IsChecked(d, IDC_TZCONVERT) && !s->ev.allDay)
        s->ev.start = UtcToZone(ZoneToUtc(s->ev.start, s->tzPrev), nk);
    s->tzPrev = nk;
    TzPreview(d, s);
}
static INT_PTR CALLBACK TzProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    EvState* s = PageData<EvState>(d);
    switch (msg) {
    case WM_INITDIALOG:
        SetPageData(d, lp); s = PageData<EvState>(d);
        TzLoad(d, s);
        if (s->ro) DisableEditing(d);
        return TRUE;
    case WM_NOTIFY: return PageNotify(d, lp, s, [&] { TzLoad(d, s); }, [&] { return TzSave(d, s); });
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_TZCOMBO: if (HIWORD(wp) == CBN_SELCHANGE) TzChanged(d, s); break;
        case IDC_TZMINE: {
            const auto& all = AllTimeZones(); std::wstring k = SystemTzKey();
            for (size_t i = 0; i < all.size(); i++) if (_wcsicmp(all[i].key.c_str(), k.c_str()) == 0) SendDlgItemMessageW(d, IDC_TZCOMBO, CB_SETCURSEL, i, 0);
            TzChanged(d, s); break;
        }
        case IDC_TZ_PINNED: case IDC_TZ_FLOATING: TzPreview(d, s); break;
        }
        return TRUE;
    }
    return FALSE;
}

// ================================================================== Attendees
static bool IsOrganizer(const EvState* s) { return s->ev.myResponse < 0; }
static void AttFill(HWND d, EvState* s, int sel) {
    HWND lv = GetDlgItem(d, IDC_ATTLIST);
    ListView_DeleteAllItems(lv);
    for (size_t i = 0; i < s->ev.attendees.size(); i++) {
        auto& a = s->ev.attendees[i];
        std::wstring prop = a.status == RSVP_PROPOSED && a.proposed ? FmtDate(a.proposed) + L" " + FmtTime(a.proposed) : L"";
        LvAdd(lv, { a.name, a.email, kRsvpNames[std::clamp(a.status, 0, 4)], a.optional ? L"Optional" : L"Required", prop }, (LPARAM)i);
    }
    LvSelect(lv, sel);
}
static void AttLoad(HWND d, EvState* s) {
    Event& e = s->ev;
    if (IsOrganizer(s)) SetText(d, IDC_ORGANIZER, L"Organizer: " + g.set.userName + L" <" + g.set.userEmail + L"> (you)");
    else SetText(d, IDC_ORGANIZER, L"Organizer: " + (e.organizerName.empty() ? e.organizer : e.organizerName + L" <" + e.organizer + L">"));
    AttFill(d, s, -1);
    CbSelData(d, IDC_MYRESPONSE, IsOrganizer(s) ? -1 : e.myResponse);
}
static INT_PTR CALLBACK AttProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    EvState* s = PageData<EvState>(d);
    HWND lv = GetDlgItem(d, IDC_ATTLIST);
    switch (msg) {
    case WM_INITDIALOG: {
        SetPageData(d, lp); s = PageData<EvState>(d);
        LvCols(lv, { { L"Name", 110 }, { L"E-mail", 150 }, { L"Response", 80 }, { L"Type", 60 }, { L"Proposed", 110 } });
        for (int i = 0; i < 5; i++) CbAdd(d, IDC_ATTSTATUS, kRsvpNames[i], i);
        SendDlgItemMessageW(d, IDC_ATTSTATUS, CB_SETCURSEL, 0, 0);
        if (IsOrganizer(s)) CbAdd(d, IDC_MYRESPONSE, L"Organizer", -1);
        else for (int i = 0; i < 4; i++) CbAdd(d, IDC_MYRESPONSE, kRsvpNames[i], i);
        SendDlgItemMessageW(d, IDC_ATTEMAIL, EM_SETCUEBANNER, TRUE, (LPARAM)L"name@example.com");
        g_evSendInvites = IsOrganizer(s) && !s->ro && (s->isNew || !s->ev.attendees.empty());
        Check(d, IDC_SENDINVITES, g_evSendInvites);
        AttLoad(d, s);
        if (s->ro) DisableEditing(d);
        else if (!IsOrganizer(s)) {
            for (int id : { IDC_ATTNAME, IDC_ATTEMAIL, IDC_ATTOPTIONAL, IDC_ATTADD, IDC_ATTREMOVE, IDC_ATTSTATUS, IDC_ATTSETSTATUS, IDC_SENDINVITES }) Enable(d, id, false);
        } else Enable(d, IDC_MYRESPONSE, false);
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->idFrom == IDC_ATTLIST && nh->code == LVN_ITEMCHANGED) {
            int i = LvSel(lv);
            if (i >= 0) { auto& a = s->ev.attendees[LvData(lv, i)]; CbSelData(d, IDC_ATTSTATUS, a.status); }
            return TRUE;
        }
        return PageNotify(d, lp, s, [&] { AttLoad(d, s); }, [&] {
            g_evSendInvites = IsOrganizer(s) && IsChecked(d, IDC_SENDINVITES) && !s->ev.attendees.empty();
            if (!IsOrganizer(s)) { LPARAM r = CbData(d, IDC_MYRESPONSE, s->ev.myResponse); if (r >= 0) s->ev.myResponse = (int)r; }
            if (IsOrganizer(s)) {
                std::wstring em = Trim(DlgText(d, IDC_ATTEMAIL));
                if (!em.empty()) {
                    int r = MessageBoxW(d, (L"Add " + em + L" to the attendees?").c_str(), L"Attendees", MB_YESNOCANCEL | MB_ICONQUESTION);
                    if (r == IDCANCEL) return false;
                    if (r == IDYES) SendMessageW(d, WM_COMMAND, IDC_ATTADD, 0);
                }
            }
            return true; });
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_ATTADD: {
            std::wstring em = Trim(DlgText(d, IDC_ATTEMAIL)), nm = Trim(DlgText(d, IDC_ATTNAME));
            if (em.find(L'@') == std::wstring::npos) { MessageBoxW(d, L"Please enter a valid e-mail address.", L"Attendees", MB_ICONWARNING); SetFocus(GetDlgItem(d, IDC_ATTEMAIL)); break; }
            for (auto& a : s->ev.attendees) if (_wcsicmp(a.email.c_str(), em.c_str()) == 0) { MessageBoxW(d, L"This person is already invited.", L"Attendees", MB_ICONINFORMATION); return TRUE; }
            Attendee a; a.email = em; a.name = nm.empty() ? em.substr(0, em.find(L'@')) : nm; a.optional = IsChecked(d, IDC_ATTOPTIONAL);
            s->ev.attendees.push_back(a);
            SetText(d, IDC_ATTEMAIL, L""); SetText(d, IDC_ATTNAME, L""); Check(d, IDC_ATTOPTIONAL, false);
            Check(d, IDC_SENDINVITES, true);
            AttFill(d, s, (int)s->ev.attendees.size() - 1);
            SetFocus(GetDlgItem(d, IDC_ATTNAME));
            break;
        }
        case IDC_ATTREMOVE: {
            int i = LvSel(lv); if (i < 0) break;
            s->ev.attendees.erase(s->ev.attendees.begin() + LvData(lv, i));
            AttFill(d, s, std::min(i, (int)s->ev.attendees.size() - 1));
            break;
        }
        case IDC_ATTSETSTATUS: {
            int i = LvSel(lv); if (i < 0) break;
            s->ev.attendees[LvData(lv, i)].status = (int)CbData(d, IDC_ATTSTATUS, 0);
            AttFill(d, s, i); break;
        }
        }
        return TRUE;
    }
    return FALSE;
}

// ================================================================== Online meeting
static void ConfHint(HWND d) {
    int p = (int)CbData(d, IDC_CONFPROVIDER, 0);
    static const wchar_t* hints[CONF_COUNT] = {
        L"No online meeting. Choose a provider and click Generate link, or paste any meeting URL.",
        L"Microsoft Teams: set your personal Teams meeting link in Options > Online Meetings to reuse it; otherwise a Teams meet-now link is generated.",
        L"Zoom: set your Personal Meeting Room link in Options > Online Meetings. Otherwise a Zoom-style link is generated (paste the real one from the Zoom app for external guests).",
        L"Google Meet: a new meeting code is generated. For a guaranteed room, create it at meet.google.com/new and paste it here or into Options.",
        L"Cisco Webex: uses your personal room from Options, or generates a meeting link.",
        L"Jitsi Meet: free, no account required. The generated link works immediately for everyone.",
        L"Custom: paste any meeting URL (GoToMeeting, Whereby, Slack huddle...)." };
    SetText(d, IDC_CONFHINT, hints[std::clamp(p, 0, CONF_COUNT - 1)]);
    bool has = !Trim(DlgText(d, IDC_CONFLINK)).empty();
    Enable(d, IDC_CONFJOIN, has); Enable(d, IDC_CONFCOPY, has);
}
static void ConfLoad(HWND d, EvState* s) {
    CbSelData(d, IDC_CONFPROVIDER, s->ev.confProvider); SetText(d, IDC_CONFLINK, s->ev.confLink);
    Check(d, IDC_CONFINLOC, !s->ev.confLink.empty() && s->ev.location.find(s->ev.confLink) != std::wstring::npos);
    ConfHint(d);
}
static bool ConfSave(HWND d, EvState* s) {
    std::wstring link = Trim(DlgText(d, IDC_CONFLINK));
    int prov = (int)CbData(d, IDC_CONFPROVIDER, 0);
    if (!link.empty() && prov == CONF_NONE) prov = DetectConfProvider(link);
    if (link.empty()) prov = CONF_NONE;
    // remove an old link from the location if it changed
    if (!s->ev.confLink.empty() && s->ev.confLink != link) {
        size_t p = s->ev.location.find(s->ev.confLink);
        if (p != std::wstring::npos) { s->ev.location.erase(p, s->ev.confLink.size()); s->ev.location = Trim(s->ev.location); }
    }
    s->ev.confLink = link; s->ev.confProvider = prov;
    if (!link.empty() && IsChecked(d, IDC_CONFINLOC) && s->ev.location.find(link) == std::wstring::npos)
        s->ev.location = s->ev.location.empty() ? link : s->ev.location + L" / " + link;
    return true;
}
static INT_PTR CALLBACK ConfProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    EvState* s = PageData<EvState>(d);
    switch (msg) {
    case WM_INITDIALOG:
        SetPageData(d, lp); s = PageData<EvState>(d);
        for (int i = 0; i < CONF_COUNT; i++) CbAdd(d, IDC_CONFPROVIDER, kConfNames[i], i);
        ConfLoad(d, s);
        if (s->ro) { DisableEditing(d); Enable(d, IDC_CONFJOIN, !s->ev.confLink.empty()); Enable(d, IDC_CONFCOPY, !s->ev.confLink.empty()); }
        return TRUE;
    case WM_NOTIFY: return PageNotify(d, lp, s, [&] { ConfLoad(d, s); }, [&] { return ConfSave(d, s); });
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CONFPROVIDER:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int p = (int)CbData(d, IDC_CONFPROVIDER, 0);
                if (p == CONF_NONE) SetText(d, IDC_CONFLINK, L"");
                else if (p != CONF_CUSTOM && (Trim(DlgText(d, IDC_CONFLINK)).empty() || DetectConfProvider(DlgText(d, IDC_CONFLINK)) != p))
                    SetText(d, IDC_CONFLINK, GenerateConfLink(p));
                ConfHint(d);
            }
            break;
        case IDC_CONFGENERATE: {
            int p = (int)CbData(d, IDC_CONFPROVIDER, 0);
            if (p == CONF_NONE || p == CONF_CUSTOM) { p = g.set.defaultConf ? g.set.defaultConf : CONF_JITSI; CbSelData(d, IDC_CONFPROVIDER, p); }
            SetText(d, IDC_CONFLINK, GenerateConfLink(p)); ConfHint(d); break;
        }
        case IDC_CONFLINK:
            if (HIWORD(wp) == EN_CHANGE) ConfHint(d);
            else if (HIWORD(wp) == EN_KILLFOCUS) {
                std::wstring l = Trim(DlgText(d, IDC_CONFLINK));
                if (!l.empty()) { int p = DetectConfProvider(l); CbSelData(d, IDC_CONFPROVIDER, p ? p : CONF_CUSTOM); ConfHint(d); }
            }
            break;
        case IDC_CONFJOIN: {
            std::wstring l = Trim(DlgText(d, IDC_CONFLINK));
            if (!l.empty()) ShellExecuteW(d, L"open", l.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        }
        case IDC_CONFCOPY: CopyToClipboard(d, Trim(DlgText(d, IDC_CONFLINK))); App_SetStatus(L"Meeting link copied to the clipboard."); break;
        }
        return TRUE;
    }
    return FALSE;
}

// ================================================================== Reminders & focus
static const wchar_t* kRemMethods[3] = { L"Pop-up", L"E-mail", L"Pop-up and e-mail" };
static void RemFill(HWND d, EvState* s, int sel) {
    std::sort(s->ev.reminders.begin(), s->ev.reminders.end(), [](const Reminder& a, const Reminder& b) { return a.minutes < b.minutes; });
    SendDlgItemMessageW(d, IDC_REMLIST, LB_RESETCONTENT, 0, 0);
    for (auto& r : s->ev.reminders) {
        std::wstring t = FmtReminder(r.minutes) + L"  -  " + kRemMethods[std::clamp(r.method, 0, 2)];
        SendDlgItemMessageW(d, IDC_REMLIST, LB_ADDSTRING, 0, (LPARAM)t.c_str());
    }
    SendDlgItemMessageW(d, IDC_REMLIST, LB_SETCURSEL, sel, 0);
    Enable(d, IDC_REMREMOVE, !s->ro && !s->ev.reminders.empty());
}
static void RemLoad(HWND d, EvState* s) {
    RemFill(d, s, -1);
    Check(d, IDC_AUTODECLINE, s->ev.autoDecline);
    SetText(d, IDC_DECLINEMSG, s->ev.declineMsg.empty() ? g.set.declineMsg : s->ev.declineMsg);
    Enable(d, IDC_DECLINEMSG, s->ev.autoDecline && !s->ro);
}
static bool RemSave(HWND d, EvState* s) {
    s->ev.autoDecline = IsChecked(d, IDC_AUTODECLINE);
    s->ev.declineMsg = DlgText(d, IDC_DECLINEMSG);
    if (s->ev.declineMsg == g.set.declineMsg) s->ev.declineMsg.clear();
    if (s->ev.autoDecline && s->ev.busy != BUSY_OOO && s->ev.busy != BUSY_FOCUS) s->ev.busy = BUSY_FOCUS;
    return true;
}
static INT_PTR CALLBACK RemProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    EvState* s = PageData<EvState>(d);
    switch (msg) {
    case WM_INITDIALOG:
        SetPageData(d, lp); s = PageData<EvState>(d);
        FillReminderPresets(d, IDC_REMTIME, false);
        CbSelData(d, IDC_REMTIME, 15);
        for (int i = 0; i < 3; i++) CbAdd(d, IDC_REMMETHOD, kRemMethods[i], i);
        CbSelData(d, IDC_REMMETHOD, g.set.remindEmail ? REM_POPUP : REM_POPUP);
        RemLoad(d, s);
        if (s->ro) DisableEditing(d);
        return TRUE;
    case WM_NOTIFY: return PageNotify(d, lp, s, [&] { RemLoad(d, s); }, [&] { return RemSave(d, s); });
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_REMADD: {
            int m;
            LPARAM sel = CbData(d, IDC_REMTIME, -2);
            std::wstring txt = DlgText(d, IDC_REMTIME);
            m = ParseReminderText(txt);
            if (m < 0 && sel >= 0) m = (int)sel;
            if (m < 0 || m > 60 * 24 * 60) { MessageBoxW(d, L"Enter the reminder time in minutes (e.g. 45), hours (2h), days (1d) or weeks (1w).", L"Reminders", MB_ICONWARNING); break; }
            int method = (int)CbData(d, IDC_REMMETHOD, 0);
            for (auto& r : s->ev.reminders) if (r.minutes == m) { r.method = method; RemFill(d, s, -1); return TRUE; }
            Reminder r; r.minutes = m; r.method = method; s->ev.reminders.push_back(r);
            RemFill(d, s, -1);
            break;
        }
        case IDC_REMREMOVE: {
            int i = (int)SendDlgItemMessageW(d, IDC_REMLIST, LB_GETCURSEL, 0, 0);
            if (i < 0 || i >= (int)s->ev.reminders.size()) break;
            s->ev.reminders.erase(s->ev.reminders.begin() + i);
            RemFill(d, s, std::min(i, (int)s->ev.reminders.size() - 1));
            break;
        }
        case IDC_AUTODECLINE: Enable(d, IDC_DECLINEMSG, IsChecked(d, IDC_AUTODECLINE)); break;
        }
        return TRUE;
    }
    return FALSE;
}

// ================================================================== sheet
bool EventDialog(HWND owner, Event& ev, bool isNew, int startPage) {
    EvState s; s.ev = ev; s.isNew = isNew; s.ro = !isNew && EventReadOnly(ev);
    g_evSendInvites = false;
    std::wstring cap = isNew ? (ev.busy == BUSY_FOCUS ? L"New Focus Block" : ev.busy == BUSY_OOO ? L"New Out of Office" : L"New Event")
                             : (ev.title.empty() ? L"Event" : ev.title);
    if (s.ro) cap += L" (read-only)";
    PropSheet(owner, cap.c_str(), {
        { IDD_EV_GENERAL, GeneralProc }, { IDD_EV_RECUR, RecurProc }, { IDD_EV_TZ, TzProc },
        { IDD_EV_ATTENDEES, AttProc }, { IDD_EV_ONLINE, ConfProc }, { IDD_EV_REMIND, RemProc } },
        (LPARAM)&s, startPage, s.ro);
    if (!s.applied || s.ro) { g_evSendInvites = false; return false; }
    ev = s.ev;
    ev.modified = NowUtc();
    return true;
}
