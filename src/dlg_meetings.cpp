// WinCalendar - Meetings (RSVP tracking / Invitations), Smart Search and the Reminders window
#include "dlgutil.h"
#include <prsht.h>

static std::wstring NextWhen(const Event& e, MIN_T* wall = nullptr) {
    MIN_T w = 0; MIN_T ds = NextOccurrence(e, &w);
    if (wall) *wall = w;
    return FmtWhen(e, ds) + (e.rec.freq ? L" (recurring)" : L"");
}
static bool IsMyMeeting(const Event& e) { return e.myResponse < 0 && !e.attendees.empty(); }
static bool IsInvitation(const Event& e) { return e.myResponse >= 0 && !e.organizer.empty() && Lower(e.organizer) != Lower(g.set.userEmail); }
static std::vector<Event*> SortedBy(bool (*pred)(const Event&)) {
    std::vector<Event*> v; for (auto& e : g.events) if (pred(e)) v.push_back(&e);
    std::vector<std::pair<MIN_T, Event*>> k; for (auto* e : v) k.push_back({ NextOccurrence(*e), e });
    std::sort(k.begin(), k.end(), [](auto& a, auto& b) { return a.first < b.first; });
    v.clear(); for (auto& p : k) v.push_back(p.second);
    return v;
}
static int ImportReplies(HWND d) {
    std::wstring p;
    if (!OpenFileDlg(d, p, L"iCalendar replies (*.ics)\0*.ics\0All files\0*.*\0", L"ics", false)) return 0;
    int n = ImportIcsFile(p, d, DefaultCalendarId());
    App_ModelChanged();
    return n;
}

// ================================================================== Sent (RSVP tracking)
static void SentFillAttendees(HWND d) {
    HWND lv = GetDlgItem(d, IDC_LIST), lv2 = GetDlgItem(d, IDC_LIST2);
    ListView_DeleteAllItems(lv2);
    Event* e = FindEvent((int)LvData(lv, LvSel(lv)));
    bool anyProp = false;
    if (e) for (size_t i = 0; i < e->attendees.size(); i++) {
        auto& a = e->attendees[i];
        std::wstring prop = a.status == RSVP_PROPOSED && a.proposed ? FmtDate(a.proposed, L"ddd, MMM d") + L" " + FmtTime(a.proposed) : L"";
        if (!prop.empty()) anyProp = true;
        LvAdd(lv2, { a.name, a.email, kRsvpNames[std::clamp(a.status, 0, 4)], prop }, (LPARAM)i);
    }
    Enable(d, IDC_ACCEPTPROP, anyProp);
    for (int id : { IDC_OPEN, IDC_RESEND, IDC_ATTSETSTATUS }) Enable(d, id, e != nullptr);
}
static void SentFill(HWND d, int selId) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    ListView_DeleteAllItems(lv);
    for (Event* e : SortedBy(IsMyMeeting)) {
        int cnt[5] = {};
        for (auto& a : e->attendees) cnt[std::clamp(a.status, 0, 4)]++;
        int i = LvAdd(lv, { e->title, NextWhen(*e), std::to_wstring(cnt[RSVP_ACCEPTED]), std::to_wstring(cnt[RSVP_TENTATIVE]),
                            std::to_wstring(cnt[RSVP_DECLINED]), std::to_wstring(cnt[RSVP_PROPOSED]), std::to_wstring(cnt[RSVP_NONE]) }, e->id);
        if (e->id == selId) LvSelect(lv, i);
    }
    if (LvSel(lv) < 0) LvSelect(lv, 0);
    SentFillAttendees(d);
}
static INT_PTR CALLBACK SentProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    HWND lv = GetDlgItem(d, IDC_LIST), lv2 = GetDlgItem(d, IDC_LIST2);
    switch (msg) {
    case WM_INITDIALOG:
        LvCols(lv, { { L"Meeting", 150 }, { L"Next", 190 }, { L"Yes", 36 }, { L"Maybe", 44 }, { L"No", 34 }, { L"New time", 56 }, { L"No reply", 54 } });
        LvCols(lv2, { { L"Name", 120 }, { L"E-mail", 170 }, { L"Response", 90 }, { L"Proposed time", 150 } });
        for (int i = 0; i < 5; i++) CbAdd(d, IDC_ATTSTATUS, kRsvpNames[i], i);
        CbSelData(d, IDC_ATTSTATUS, RSVP_ACCEPTED);
        SentFill(d, g_selEvent);
        return TRUE;
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->code == PSN_SETACTIVE) { SentFill(d, (int)LvData(lv, LvSel(lv))); return TRUE; }
        if (nh->idFrom == IDC_LIST && nh->code == LVN_ITEMCHANGED) SentFillAttendees(d);
        if (nh->idFrom == IDC_LIST && nh->code == NM_DBLCLK) SendMessageW(d, WM_COMMAND, IDC_OPEN, 0);
        if (nh->idFrom == IDC_LIST2 && nh->code == LVN_ITEMCHANGED) {
            Event* e = FindEvent((int)LvData(lv, LvSel(lv))); int i = LvSel(lv2);
            if (e && i >= 0) CbSelData(d, IDC_ATTSTATUS, e->attendees[LvData(lv2, i)].status);
        }
        break;
    }
    case WM_COMMAND: {
        Event* e = FindEvent((int)LvData(lv, LvSel(lv)));
        switch (LOWORD(wp)) {
        case IDC_ATTSETSTATUS: {
            int i = LvSel(lv2); if (!e || i < 0) break;
            e->attendees[LvData(lv2, i)].status = (int)CbData(d, IDC_ATTSTATUS, 0);
            App_ModelChanged(); SentFill(d, e->id); LvSelect(lv2, i); break;
        }
        case IDC_ACCEPTPROP: {
            if (!e) break;
            int i = LvSel(lv2);
            Attendee* a = i >= 0 ? &e->attendees[LvData(lv2, i)] : nullptr;
            if (!a || a->status != RSVP_PROPOSED) for (auto& x : e->attendees) if (x.status == RSVP_PROPOSED && x.proposed) { a = &x; break; }
            if (!a || !a->proposed) break;
            std::wstring q = L"Move \"" + e->title + L"\" to " + FmtDate(a->proposed, L"dddd, MMMM d") + L" " + FmtTime(a->proposed) +
                             L" as proposed by " + (a->name.empty() ? a->email : a->name) + L"?\n\nUpdated invitations will be sent to all attendees.";
            if (MessageBoxW(d, q.c_str(), L"Accept proposed time", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            MIN_T ns = a->proposed;   // display time
            if (e->tz != TZ_FLOATING && !e->allDay) {
                std::wstring k = e->tz.empty() ? DisplayTzKey() : e->tz;
                ns = UtcToZone(ZoneToUtc(ns, DisplayTzKey()), k);
            }
            e->start = e->allDay ? (MIN_T)DayOf(ns) * DAYMIN : ns;
            for (auto& x : e->attendees) { if (&x == a) { x.status = RSVP_ACCEPTED; x.proposed = 0; } else if (x.status != RSVP_DECLINED) { x.status = RSVP_NONE; x.proposed = 0; } }
            e->modified = NowUtc();
            App_ModelChanged();
            SendInvitations(*e, d);
            SentFill(d, e->id); break;
        }
        case IDC_OPEN: if (e) { App_EditEvent(GetParent(d), e->id, 0, 3); SentFill(d, e->id); } break;
        case IDC_RESEND: if (e) SendInvitations(*e, d); break;
        case IDC_IMPORTREPLY: ImportReplies(d); SentFill(d, e ? e->id : 0); break;
        }
        return TRUE;
    }
    }
    return FALSE;
}

// ================================================================== Received invitations
static void RecvFill(HWND d, int selId) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    ListView_DeleteAllItems(lv);
    for (Event* e : SortedBy(IsInvitation)) {
        std::wstring org = e->organizerName.empty() ? e->organizer : e->organizerName;
        int i = LvAdd(lv, { e->title, NextWhen(*e), org, kRsvpNames[std::clamp(e->myResponse, 0, 4)] }, e->id);
        if (e->id == selId) LvSelect(lv, i);
    }
    if (LvSel(lv) < 0) LvSelect(lv, 0);
}
static INT_PTR CALLBACK RecvProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    switch (msg) {
    case WM_INITDIALOG:
        LvCols(lv, { { L"Meeting", 150 }, { L"When", 200 }, { L"Organizer", 120 }, { L"My response", 80 } });
        SetText(d, IDC_HINT, L"Open invitation e-mail attachments (.ics) with Import. Invitations that overlap a focus block or out-of-office time are declined automatically.");
        RecvFill(d, g_selEvent);
        return TRUE;
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->code == PSN_SETACTIVE) { RecvFill(d, (int)LvData(lv, LvSel(lv))); return TRUE; }
        if (nh->idFrom == IDC_LIST && nh->code == NM_DBLCLK) SendMessageW(d, WM_COMMAND, IDC_OPEN, 0);
        break;
    }
    case WM_COMMAND: {
        Event* e = FindEvent((int)LvData(lv, LvSel(lv)));
        int id = e ? e->id : 0;
        switch (LOWORD(wp)) {
        case IDC_ACCEPT: case IDC_TENTATIVE: case IDC_DECLINE: {
            if (!e) break;
            int r = LOWORD(wp) == IDC_ACCEPT ? RSVP_ACCEPTED : LOWORD(wp) == IDC_TENTATIVE ? RSVP_TENTATIVE : RSVP_DECLINED;
            if (r == RSVP_ACCEPTED) {
                std::wstring block;
                if (FindFocusConflict(*e, &block) &&
                    MessageBoxW(d, (L"This meeting overlaps your focus/out-of-office block \"" + block + L"\". Accept anyway?").c_str(), L"Accept", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            }
            SendResponse(*e, r, d);
            App_ModelChanged(); RecvFill(d, id); break;
        }
        case IDC_PROPOSE: {
            if (!e) break;
            MIN_T st = NextOccurrence(*e); std::wstring comment;
            if (!ProposeTimeDialog(d, st, e->duration, &comment)) break;
            SendResponse(*e, RSVP_PROPOSED, d, st, comment);
            App_ModelChanged(); RecvFill(d, id); break;
        }
        case IDC_OPEN: if (e) { App_EditEvent(GetParent(d), id, 0, 0); RecvFill(d, id); } break;
        case IDC_IMPORTREPLY: ImportReplies(d); RecvFill(d, id); break;
        }
        return TRUE;
    }
    }
    return FALSE;
}

void MeetingsDialog(HWND owner, int startPage) {
    PropSheet(owner, L"Meetings", { { IDD_MTG_SENT, SentProc }, { IDD_MTG_RECEIVED, RecvProc } }, 0, startPage, true);
    App_ModelChanged();
}

// ================================================================== Smart search
namespace { struct Hit { int id; MIN_T wall, when; bool future; }; }
static std::vector<Hit> s_hits;
static std::wstring s_lastQuery;

static bool OccInRange(const Event& e, MIN_T from, MIN_T to, bool latest, MIN_T& when, MIN_T& wall) {
    if (to <= from) return false;
    std::vector<Occ> v; ExpandEvent(e, from, to, v);
    if (v.empty()) return false;
    auto it = latest ? std::max_element(v.begin(), v.end(), [](const Occ& a, const Occ& b) { return a.start < b.start; })
                     : std::min_element(v.begin(), v.end(), [](const Occ& a, const Occ& b) { return a.start < b.start; });
    when = it->start; wall = it->wall; return true;
}
static void RunSearch(HWND d) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    std::wstring q = Lower(Trim(DlgText(d, IDC_KEYWORD)));
    s_lastQuery = DlgText(d, IDC_KEYWORD);
    std::vector<std::wstring> toks; { size_t p = 0; while (p < q.size()) { size_t e = q.find(L' ', p); if (e == std::wstring::npos) e = q.size(); if (e > p) toks.push_back(q.substr(p, e - p)); p = e + 1; } }
    bool sT = IsChecked(d, IDC_SCOPE_TITLE), sL = IsChecked(d, IDC_SCOPE_LOC), sA = IsChecked(d, IDC_SCOPE_ATT);
    int range = (int)CbData(d, IDC_RANGE, 0), cal = (int)CbData(d, IDC_CALENDAR, 0);
    MIN_T now = NowDisplay();
    s_hits.clear();
    for (auto& e : g.events) {
        if (cal && e.calendarId != cal) continue;
        std::wstring hay;
        if (sT) hay += e.title + L"\n" + e.description + L"\n" + e.category + L"\n";
        if (sL) hay += e.location + L"\n" + e.confLink + L"\n";
        if (sA) { hay += e.organizer + L"\n" + e.organizerName + L"\n"; for (auto& a : e.attendees) hay += a.name + L"\n" + a.email + L"\n"; }
        hay = Lower(hay);
        bool ok = true; for (auto& t : toks) if (hay.find(t) == std::wstring::npos) { ok = false; break; }
        if (!ok) continue;
        MIN_T first = EventToDisplay(e, e.start), when = 0, wall = 0;
        MIN_T pastFrom = std::max(first - DAYMIN, now - DAYMIN * 365 * 5);
        Hit h{ e.id, 0, 0, false };
        switch (range) {
        case 0:   // all: next upcoming, else most recent
            if (OccInRange(e, now, now + DAYMIN * 800, false, when, wall)) h.future = true;
            else if (!OccInRange(e, pastFrom, now, true, when, wall)) { when = first; wall = e.start; h.future = first >= now; }
            break;
        case 1: if (!OccInRange(e, now, now + DAYMIN * 800, false, when, wall)) continue; h.future = true; break;
        case 2: if (!OccInRange(e, pastFrom, now, true, when, wall)) continue; break;
        case 3: if (!OccInRange(e, now, now + DAYMIN * 30, false, when, wall)) continue; h.future = true; break;
        case 4: if (!OccInRange(e, now - DAYMIN * 30, now, true, when, wall)) continue; break;
        }
        h.when = when; h.wall = wall;
        s_hits.push_back(h);
    }
    std::sort(s_hits.begin(), s_hits.end(), [](const Hit& a, const Hit& b) {
        if (a.future != b.future) return a.future;
        return a.future ? a.when < b.when : a.when > b.when; });
    SendMessageW(lv, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(lv);
    for (size_t i = 0; i < s_hits.size() && i < 2000; i++) {
        Event* e = FindEvent(s_hits[i].id);
        Calendar* c = FindCalendar(e->calendarId);
        std::wstring who;
        for (auto& a : e->attendees) { if (!who.empty()) who += L", "; who += a.name.empty() ? a.email : a.name; }
        LvAdd(lv, { e->title, FmtWhen(*e, s_hits[i].when) + (e->rec.freq ? L" \u21BB" : L""), e->location, c ? c->name : L"", who }, (LPARAM)i);
    }
    SendMessageW(lv, WM_SETREDRAW, TRUE, 0);
    LvSelect(lv, 0);
    std::wstring cnt = std::to_wstring(s_hits.size()) + (s_hits.size() == 1 ? L" result" : L" results");
    if (s_hits.size() > 2000) cnt += L" (first 2000 shown)";
    SetText(d, IDC_RESULTCOUNT, cnt);
    Enable(d, IDC_OPEN, !s_hits.empty()); Enable(d, IDC_GOTO, !s_hits.empty());
}
static INT_PTR CALLBACK SearchProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    auto sel = [&]() -> Hit* { int i = LvSel(lv); return i < 0 ? nullptr : &s_hits[LvData(lv, i)]; };
    switch (msg) {
    case WM_INITDIALOG: {
        SendMessageW(d, WM_SETICON, ICON_SMALL, (LPARAM)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0));
        LvCols(lv, { { L"Title", 160 }, { L"When", 210 }, { L"Location", 120 }, { L"Calendar", 90 }, { L"Attendees", 160 } });
        Check(d, IDC_SCOPE_TITLE, true); Check(d, IDC_SCOPE_LOC, true); Check(d, IDC_SCOPE_ATT, true);
        const wchar_t* r[] = { L"Any time", L"Upcoming", L"Past", L"Next 30 days", L"Past 30 days" };
        for (int i = 0; i < 5; i++) CbAdd(d, IDC_RANGE, r[i], i);
        CbSelData(d, IDC_RANGE, 0);
        CbAdd(d, IDC_CALENDAR, L"All calendars", 0);
        for (auto& c : g.calendars) CbAdd(d, IDC_CALENDAR, c.name, c.id);
        CbSelData(d, IDC_CALENDAR, 0);
        SendDlgItemMessageW(d, IDC_KEYWORD, EM_SETCUEBANNER, TRUE, (LPARAM)L"Keyword, attendee name or e-mail, location...");
        SetText(d, IDC_KEYWORD, *(std::wstring*)lp);
        RunSearch(d);
        SetFocus(GetDlgItem(d, IDC_KEYWORD));
        SendDlgItemMessageW(d, IDC_KEYWORD, EM_SETSEL, 0, -1);
        return FALSE;
    }
    case WM_TIMER: KillTimer(d, 1); RunSearch(d); return TRUE;
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->idFrom == IDC_LIST && nh->code == NM_DBLCLK) SendMessageW(d, WM_COMMAND, IDC_OPEN, 0);
        if (nh->idFrom == IDC_LIST && nh->code == LVN_KEYDOWN && ((NMLVKEYDOWN*)lp)->wVKey == VK_RETURN) SendMessageW(d, WM_COMMAND, IDC_OPEN, 0);
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_KEYWORD: if (HIWORD(wp) == EN_CHANGE) SetTimer(d, 1, 250, nullptr); break;
        case IDC_SCOPE_TITLE: case IDC_SCOPE_LOC: case IDC_SCOPE_ATT: RunSearch(d); break;
        case IDC_RANGE: case IDC_CALENDAR: if (HIWORD(wp) == CBN_SELCHANGE) RunSearch(d); break;
        case IDOK: case IDC_SEARCHBTN:
            if (GetFocus() == lv) SendMessageW(d, WM_COMMAND, IDC_OPEN, 0); else RunSearch(d);
            break;
        case IDC_OPEN: if (Hit* h = sel()) { Hit c = *h; App_EditEvent(d, c.id, FindEvent(c.id) && FindEvent(c.id)->rec.freq ? c.wall : 0, 0); RunSearch(d); } break;
        case IDC_GOTO: if (Hit* h = sel()) { g_selEvent = h->id; g_selWall = h->wall; App_GoToDay(DayOf(h->when), -1); EndDialog(d, IDOK); } break;
        case IDCANCEL: EndDialog(d, IDCANCEL); break;
        }
        return TRUE;
    }
    return FALSE;
}
void SearchDialog(HWND owner, const std::wstring& initial) {
    std::wstring init = initial.empty() ? L"" : initial;
    DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_SEARCH), owner, SearchProc, (LPARAM)&init);
}

// ================================================================== Reminders window
namespace { struct RemItem { int id; MIN_T wall; MIN_T start; MIN_T snoozeUntil; }; }   // start: display time; snooze: UTC
static std::vector<RemItem> s_rem;
static HWND s_remWnd;

static std::wstring DueText(MIN_T start) {
    long long diff = start - NowDisplay();
    if (diff > 0) return L"in " + FmtDuration((int)diff);
    if (diff > -1) return L"now";
    return FmtDuration((int)-diff) + L" overdue";
}
static void RemRefresh() {
    if (!s_remWnd) return;
    HWND lv = GetDlgItem(s_remWnd, IDC_LIST);
    std::set<std::pair<int, MIN_T>> selected;
    for (int i = -1; (i = ListView_GetNextItem(lv, i, LVNI_SELECTED)) >= 0;) { size_t k = (size_t)LvData(lv, i); if (k < s_rem.size()) selected.insert({ s_rem[k].id, s_rem[k].wall }); }
    s_rem.erase(std::remove_if(s_rem.begin(), s_rem.end(), [](const RemItem& r) { return !FindEvent(r.id); }), s_rem.end());
    ListView_DeleteAllItems(lv);
    MIN_T nowU = NowUtc(); int shown = 0;
    for (size_t i = 0; i < s_rem.size(); i++) {
        if (s_rem[i].snoozeUntil > nowU) continue;
        Event* e = FindEvent(s_rem[i].id);
        std::wstring when = FmtWhen(*e, s_rem[i].start);
        int it = LvAdd(lv, { e->title, DueText(s_rem[i].start), when }, (LPARAM)i);
        if (selected.count({ s_rem[i].id, s_rem[i].wall })) LvSelect(lv, it);
        shown++;
    }
    if (shown && ListView_GetSelectedCount(lv) == 0) LvSelect(lv, 0);
    std::wstring cap = shown == 1 ? L"1 Reminder" : std::to_wstring(shown) + L" Reminders";
    SetWindowTextW(s_remWnd, cap.c_str());
    if (!shown) ShowWindow(s_remWnd, SW_HIDE);
}
static void RemForSelected(bool (*fn)(RemItem&)) {
    HWND lv = GetDlgItem(s_remWnd, IDC_LIST);
    std::vector<size_t> idx;
    for (int i = -1; (i = ListView_GetNextItem(lv, i, LVNI_SELECTED)) >= 0;) idx.push_back((size_t)LvData(lv, i));
    std::sort(idx.rbegin(), idx.rend());
    for (size_t k : idx) if (k < s_rem.size() && fn(s_rem[k])) s_rem.erase(s_rem.begin() + k);
    RemRefresh();
}
static MIN_T s_snoozeMin;
static INT_PTR CALLBACK RemProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    switch (msg) {
    case WM_INITDIALOG: {
        SendMessageW(d, WM_SETICON, ICON_SMALL, (LPARAM)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 16, 16, 0));
        SendMessageW(d, WM_SETICON, ICON_BIG, (LPARAM)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, 32, 32, 0));
        LvCols(lv, { { L"Subject", 170 }, { L"Due", 100 }, { L"When", 180 } });
        for (int m : { 5, 10, 15, 30, 60, 120, 240, 1440 }) CbAdd(d, IDC_SNOOZETIME, FmtDuration(m), m);
        CbAdd(d, IDC_SNOOZETIME, L"Until start", 0);
        CbSelData(d, IDC_SNOOZETIME, 5);
        // bottom-right of the work area
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        RECT r; GetWindowRect(d, &r);
        SetWindowPos(d, HWND_TOPMOST, wa.right - (r.right - r.left) - S(16), wa.bottom - (r.bottom - r.top) - S(16), 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        return TRUE;
    }
    case WM_NOTIFY:
        if (((NMHDR*)lp)->idFrom == IDC_LIST && ((NMHDR*)lp)->code == NM_DBLCLK) SendMessageW(d, WM_COMMAND, IDC_OPEN, 0);
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_OPEN: {
            int i = LvSel(lv); if (i < 0) break;
            RemItem r = s_rem[LvData(lv, i)];
            Event* e = FindEvent(r.id);
            App_EditEvent(g_hwnd, r.id, e && e->rec.freq ? r.wall : 0, 0);
            break;
        }
        case IDC_DISMISS: RemForSelected([](RemItem&) { return true; }); break;
        case IDC_DISMISSALL: {
            MIN_T nowU = NowUtc();
            s_rem.erase(std::remove_if(s_rem.begin(), s_rem.end(), [&](const RemItem& r) { return r.snoozeUntil <= nowU; }), s_rem.end());
            RemRefresh(); break;
        }
        case IDC_SNOOZE: {
            s_snoozeMin = CbData(d, IDC_SNOOZETIME, 5);
            RemForSelected([](RemItem& r) {
                MIN_T nowU = NowUtc();
                if (s_snoozeMin == 0) {
                    Event* e = FindEvent(r.id);
                    MIN_T startU = e ? EventToUtc(*e, r.wall) : nowU;
                    r.snoozeUntil = std::max(startU, nowU + 1);
                } else r.snoozeUntil = nowU + s_snoozeMin;
                return false; });
            break;
        }
        case IDCANCEL: ShowWindow(d, SW_HIDE); break;
        }
        return TRUE;
    case WM_CLOSE: ShowWindow(d, SW_HIDE); return TRUE;
    }
    return FALSE;
}
static void RemEnsure() {
    if (!s_remWnd) s_remWnd = CreateDialogParamW(g_hinst, MAKEINTRESOURCEW(IDD_REMINDER), nullptr, RemProc, 0);
}
HWND Reminders_Window() { return s_remWnd; }
void Reminders_Add(const Occ& o, int) {
    RemEnsure();
    bool found = false;
    for (auto& r : s_rem) if (r.id == o.id && r.wall == o.wall) { r.snoozeUntil = 0; r.start = o.start; found = true; }
    if (!found) s_rem.push_back({ o.id, o.wall, o.start, 0 });
    RemRefresh();
    if (!IsWindowVisible(s_remWnd)) ShowWindow(s_remWnd, SW_SHOWNOACTIVATE);
    SetWindowPos(s_remWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    FlashWindow(s_remWnd, TRUE);
}
void Reminders_Tick() {
    if (!s_remWnd || s_rem.empty()) return;
    MIN_T nowU = NowUtc(); bool wake = false;
    for (auto& r : s_rem) if (r.snoozeUntil && r.snoozeUntil <= nowU) { r.snoozeUntil = 0; wake = true; if (Event* e = FindEvent(r.id)) App_Notify(L"Reminder: " + e->title, FmtWhen(*e, r.start)); }
    RemRefresh();
    if (wake && !IsWindowVisible(s_remWnd)) ShowWindow(s_remWnd, SW_SHOWNOACTIVATE);
}
void Reminders_Show(HWND) {
    RemEnsure(); RemRefresh();
    ShowWindow(s_remWnd, SW_SHOW); SetForegroundWindow(s_remWnd);
}
