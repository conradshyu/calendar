// WinCalendar - tabbed Options dialog (General, Time Zones, Notifications, Online Meetings, Focus & Out of Office)
#include "dlgutil.h"
#include <prsht.h>

struct OptState { Settings s; bool applied = false; };

template <class L, class Sv>
static INT_PTR OptNotify(HWND d, LPARAM lp, OptState* st, L load, Sv save) {
    switch (((NMHDR*)lp)->code) {
    case PSN_SETACTIVE: load(); SetMsgResult(d, 0); return TRUE;
    case PSN_KILLACTIVE: SetMsgResult(d, save() ? FALSE : TRUE); return TRUE;
    case PSN_APPLY: { bool ok = save(); if (ok) st->applied = true; SetMsgResult(d, ok ? PSNRET_NOERROR : PSNRET_INVALID_NOCHANGEPAGE); return TRUE; }
    }
    return FALSE;
}
static std::wstring HourText(int h) { return FmtTime((MIN_T)h * 60); }

// ------------------------------------------------------------------ General
static INT_PTR CALLBACK OptGeneralProc(HWND d, UINT msg, WPARAM, LPARAM lp) {
    OptState* st = PageData<OptState>(d);
    auto load = [&] {
        Settings& s = st->s;
        SetText(d, IDC_USERNAME, s.userName); SetText(d, IDC_USEREMAIL, s.userEmail);
        CbSelData(d, IDC_WEEKSTART, s.weekStart); CbSelData(d, IDC_WORKSTART, s.workStart); CbSelData(d, IDC_WORKEND, s.workEnd);
        CbSelData(d, IDC_DEFDURATION, s.defaultDuration); CbSelData(d, IDC_DEFCALENDAR, s.defaultCalendar ? s.defaultCalendar : DefaultCalendarId());
        CbSelData(d, IDC_DEFVIEW, s.view);
    };
    auto save = [&] {
        Settings& s = st->s;
        std::wstring em = Trim(DlgText(d, IDC_USEREMAIL));
        if (em.find(L'@') == std::wstring::npos) { MessageBoxW(d, L"Please enter your e-mail address. It is used as the organizer of your meetings.", L"Options", MB_ICONWARNING); return false; }
        int ws = (int)CbData(d, IDC_WORKSTART, 8), we = (int)CbData(d, IDC_WORKEND, 17);
        if (we <= ws) { MessageBoxW(d, L"Work hours must end after they start.", L"Options", MB_ICONWARNING); return false; }
        s.userName = Trim(DlgText(d, IDC_USERNAME)); if (s.userName.empty()) s.userName = em;
        s.userEmail = em; s.workStart = ws; s.workEnd = we;
        s.weekStart = (int)CbData(d, IDC_WEEKSTART, 0); s.defaultDuration = (int)CbData(d, IDC_DEFDURATION, 60);
        s.defaultCalendar = (int)CbData(d, IDC_DEFCALENDAR, 0); s.view = (int)CbData(d, IDC_DEFVIEW, 1);
        return true;
    };
    switch (msg) {
    case WM_INITDIALOG:
        SetPageData(d, lp); st = PageData<OptState>(d);
        CbAdd(d, IDC_WEEKSTART, L"Sunday", 0); CbAdd(d, IDC_WEEKSTART, L"Monday", 1); CbAdd(d, IDC_WEEKSTART, L"Saturday", 6);
        for (int h = 0; h < 24; h++) { CbAdd(d, IDC_WORKSTART, HourText(h), h); CbAdd(d, IDC_WORKEND, HourText(h + 1 == 24 ? 0 : h + 1) + (h + 1 == 24 ? L" (midnight)" : L""), h + 1); }
        for (int m : { 15, 30, 45, 60, 90, 120 }) CbAdd(d, IDC_DEFDURATION, FmtDuration(m), m);
        for (auto& c : g.calendars) if (!CalendarReadOnly(&c)) CbAdd(d, IDC_DEFCALENDAR, c.name, c.id);
        { const wchar_t* v[] = { L"Day", L"Week", L"Month", L"Year", L"Agenda" }; for (int i = 0; i < 5; i++) CbAdd(d, IDC_DEFVIEW, v[i], i); }
        load();
        return TRUE;
    case WM_NOTIFY: return OptNotify(d, lp, st, load, save);
    }
    return FALSE;
}

// ------------------------------------------------------------------ Time zones
static void OptTzPreview(HWND d) {
    bool autoTz = IsChecked(d, IDC_AUTOTZ);
    Enable(d, IDC_TZCOMBO, !autoTz); Enable(d, IDC_TZ2, IsChecked(d, IDC_SHOW2TZ));
    std::wstring disp = autoTz ? SystemTzKey() : TzComboKey(d, IDC_TZCOMBO);
    MIN_T utc = NowUtc();
    std::wstring t;
    auto line = [&](const wchar_t* label, const std::wstring& key) {
        MIN_T w = UtcToZone(utc, key); int off = TzOffsetMinutes(key, utc);
        wchar_t o[32]; swprintf_s(o, L"UTC%c%02d:%02d", off < 0 ? L'-' : L'+', abs(off) / 60, abs(off) % 60);
        t += std::wstring(label) + FmtDate(w, L"ddd, MMM d") + L"  " + FmtTime(w) + L"   (" + o + L")  " + TzDisplayName(key) + L"\r\n";
    };
    line(L"Calendar:  ", disp);
    if (IsChecked(d, IDC_SHOW2TZ)) { std::wstring k2 = TzComboKey(d, IDC_TZ2); if (!k2.empty()) line(L"Second:  ", k2); }
    t += L"UTC:  " + FmtDate(utc, L"ddd, MMM d") + L"  " + FmtTime(utc);
    SetText(d, IDC_TZPREVIEW, t);
}
static INT_PTR CALLBACK OptTzProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    OptState* st = PageData<OptState>(d);
    auto load = [&] {
        Check(d, IDC_AUTOTZ, st->s.autoTz); Check(d, IDC_SHOW2TZ, st->s.showSecondTz);
        OptTzPreview(d);
    };
    auto save = [&] {
        st->s.autoTz = IsChecked(d, IDC_AUTOTZ);
        std::wstring k = TzComboKey(d, IDC_TZCOMBO); if (!k.empty()) st->s.tzKey = k;
        st->s.showSecondTz = IsChecked(d, IDC_SHOW2TZ);
        std::wstring k2 = TzComboKey(d, IDC_TZ2); if (!k2.empty()) st->s.secondTz = k2;
        if (st->s.showSecondTz && st->s.secondTz.empty()) st->s.showSecondTz = false;
        return true;
    };
    switch (msg) {
    case WM_INITDIALOG:
        SetPageData(d, lp); st = PageData<OptState>(d);
        SetText(d, IDC_SYSTZ, L"Windows reports: " + TzDisplayName(SystemTzKey()));
        FillTzCombo(d, IDC_TZCOMBO, st->s.tzKey.empty() ? SystemTzKey() : st->s.tzKey);
        FillTzCombo(d, IDC_TZ2, st->s.secondTz.empty() ? L"UTC" : st->s.secondTz);
        load();
        SetTimer(d, 1, 15000, nullptr);
        return TRUE;
    case WM_TIMER: OptTzPreview(d); return TRUE;
    case WM_DESTROY: KillTimer(d, 1); return FALSE;
    case WM_NOTIFY: return OptNotify(d, lp, st, load, save);
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == CBN_SELCHANGE) OptTzPreview(d);
        return TRUE;
    }
    return FALSE;
}

// ------------------------------------------------------------------ Notifications
static INT_PTR CALLBACK OptNotifyProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    OptState* st = PageData<OptState>(d);
    auto load = [&] {
        CbSelData(d, IDC_DEFREMINDER, st->s.defaultReminder);
        Check(d, IDC_REMPOPUP, st->s.remindPopup); Check(d, IDC_REMSOUND, st->s.remindSound); Check(d, IDC_REMEMAIL, st->s.remindEmail);
    };
    auto save = [&] {
        st->s.defaultReminder = (int)CbData(d, IDC_DEFREMINDER, -1);
        st->s.remindPopup = IsChecked(d, IDC_REMPOPUP); st->s.remindSound = IsChecked(d, IDC_REMSOUND); st->s.remindEmail = IsChecked(d, IDC_REMEMAIL);
        return true;
    };
    switch (msg) {
    case WM_INITDIALOG:
        SetPageData(d, lp); st = PageData<OptState>(d);
        FillReminderPresets(d, IDC_DEFREMINDER, true);
        load();
        return TRUE;
    case WM_NOTIFY: return OptNotify(d, lp, st, load, save);
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_TESTNOTIFY) {
            if (IsChecked(d, IDC_REMSOUND)) MessageBeep(MB_ICONASTERISK);
            App_Notify(L"WinCalendar", L"This is how reminders appear.");
        }
        return TRUE;
    }
    return FALSE;
}

// ------------------------------------------------------------------ Online meetings
static INT_PTR CALLBACK OptOnlineProc(HWND d, UINT msg, WPARAM, LPARAM lp) {
    OptState* st = PageData<OptState>(d);
    auto load = [&] {
        CbSelData(d, IDC_DEFCONF, st->s.defaultConf); Check(d, IDC_AUTOCONF, st->s.autoAddConf);
        for (int i = 0; i < 5; i++) SetText(d, IDC_ROOM1 + i, st->s.confRoom[CONF_TEAMS + i]);
    };
    auto save = [&] {
        st->s.defaultConf = (int)CbData(d, IDC_DEFCONF, CONF_TEAMS); st->s.autoAddConf = IsChecked(d, IDC_AUTOCONF);
        for (int i = 0; i < 5; i++) {
            std::wstring r = Trim(DlgText(d, IDC_ROOM1 + i));
            if (!r.empty() && r.find(L"://") == std::wstring::npos) r = L"https://" + r;
            st->s.confRoom[CONF_TEAMS + i] = r;
        }
        return true;
    };
    switch (msg) {
    case WM_INITDIALOG:
        SetPageData(d, lp); st = PageData<OptState>(d);
        for (int i = CONF_TEAMS; i <= CONF_JITSI; i++) CbAdd(d, IDC_DEFCONF, kConfNames[i], i);
        for (int i = 0; i < 5; i++) SendDlgItemMessageW(d, IDC_ROOM1 + i, EM_SETCUEBANNER, TRUE, (LPARAM)L"https://...  (leave empty to generate links)");
        load();
        return TRUE;
    case WM_NOTIFY: return OptNotify(d, lp, st, load, save);
    }
    return FALSE;
}

// ------------------------------------------------------------------ Focus & OOO
static void FillBlocks(HWND d) {
    HWND lv = GetDlgItem(d, IDC_BLOCKLIST);
    ListView_DeleteAllItems(lv);
    MIN_T now = NowDisplay();
    std::vector<Occ> occ = Expand(now - DAYMIN, now + DAYMIN * 120, false);
    std::sort(occ.begin(), occ.end(), [](const Occ& a, const Occ& b) { return a.start < b.start; });
    int n = 0;
    for (auto& o : occ) {
        const Event* e = FindEvent(o.id);
        if (!e || (e->busy != BUSY_FOCUS && e->busy != BUSY_OOO) || o.end < now) continue;
        std::wstring when = FmtDate(o.start, L"ddd, MMM d") + (o.allDay ? L" (all day)" : L" " + FmtTime(o.start) + L" - " + FmtTime(o.end));
        int i = LvAdd(lv, { e->title, kBusyNames[e->busy], when, e->autoDecline ? L"Yes" : L"No" }, o.id);
        (void)i;
        if (++n >= 200) break;
    }
}
static INT_PTR CALLBACK OptFocusProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    OptState* st = PageData<OptState>(d);
    HWND lv = GetDlgItem(d, IDC_BLOCKLIST);
    auto load = [&] {
        Check(d, IDC_AUTODECLINE, st->s.autoDecline); SetText(d, IDC_DECLINEMSG, st->s.declineMsg);
        CbSelData(d, IDC_FOCUSLEN, st->s.focusLength);
        FillBlocks(d);
    };
    auto save = [&] {
        st->s.autoDecline = IsChecked(d, IDC_AUTODECLINE); st->s.declineMsg = DlgText(d, IDC_DECLINEMSG);
        st->s.focusLength = (int)CbData(d, IDC_FOCUSLEN, 120);
        return true;
    };
    switch (msg) {
    case WM_INITDIALOG:
        SetPageData(d, lp); st = PageData<OptState>(d);
        for (int m : { 30, 60, 90, 120, 180, 240 }) CbAdd(d, IDC_FOCUSLEN, FmtDuration(m), m);
        LvCols(lv, { { L"Title", 130 }, { L"Type", 80 }, { L"When", 170 }, { L"Auto-decline", 70 } });
        load();
        return TRUE;
    case WM_NOTIFY:
        if (((NMHDR*)lp)->idFrom == IDC_BLOCKLIST) {
            if (((NMHDR*)lp)->code == NM_DBLCLK) SendMessageW(d, WM_COMMAND, IDC_OPENBLOCK, 0);
            return TRUE;
        }
        return OptNotify(d, lp, st, load, save);
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_NEWFOCUS: case IDC_NEWOOO: {
            // apply the length / message being edited so the new block uses them
            g.set.focusLength = (int)CbData(d, IDC_FOCUSLEN, 120);
            App_NewEvent(GetParent(d), LOWORD(wp) == IDC_NEWFOCUS ? NEW_FOCUS : NEW_OOO);
            FillBlocks(d); break;
        }
        case IDC_OPENBLOCK: {
            int id = (int)LvData(lv, LvSel(lv));
            if (id) { App_EditEvent(GetParent(d), id, 0, 5); FillBlocks(d); }
            break;
        }
        }
        return TRUE;
    }
    return FALSE;
}

bool OptionsDialog(HWND owner, int startPage) {
    OptState st; st.s = g.set;
    PropSheet(owner, L"Options", {
        { IDD_OPT_GENERAL, OptGeneralProc }, { IDD_OPT_TZ, OptTzProc }, { IDD_OPT_NOTIFY, OptNotifyProc },
        { IDD_OPT_ONLINE, OptOnlineProc }, { IDD_OPT_FOCUS, OptFocusProc } }, (LPARAM)&st, startPage, false);
    if (!st.applied) return false;
    // keep fields possibly changed elsewhere while the dialog was open
    st.s.showCompleted = g.set.showCompleted;
    bool tzChanged = st.s.autoTz != g.set.autoTz || st.s.tzKey != g.set.tzKey;
    g.set = st.s;
    if (tzChanged) RefreshTimeZoneCache();
    App_ModelChanged();
    return true;
}
