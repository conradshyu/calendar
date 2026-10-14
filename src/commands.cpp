// WinCalendar - command handling and App_* services used by the dialogs and views
#include "app.h"
#include "dlgutil.h"
#include <mmsystem.h>

void RefreshUI();
void RefreshTasks();
HWND Main_TaskList();
HWND Main_CalList();
std::wstring Main_SearchText();
void Main_FocusSearch();
void Main_SyncMonthCal();
void Main_UpdateTitle();
void Main_EditSelectedTask();
void Ribbon_Invalidate();

static MIN_T s_forceStart = 0;   // start time for "new" commands issued from the view's context menu
static int s_forceConf = -1;     // conference provider for a new meeting created from an "Add ..." button

// ---------------------------------------------------------------- helpers
static int Choose(HWND owner, const wchar_t* title, const std::wstring& main, const std::wstring& content, const std::vector<std::wstring>& buttons) {
    std::vector<TASKDIALOG_BUTTON> b;
    for (size_t i = 0; i < buttons.size(); ++i) b.push_back({ (int)(100 + i), buttons[i].c_str() });
    TASKDIALOGCONFIG c{}; c.cbSize = sizeof(c); c.hwndParent = owner; c.hInstance = g_hinst;
    c.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    c.dwCommonButtons = TDCBF_CANCEL_BUTTON; c.pszWindowTitle = title; c.pszMainInstruction = main.c_str();
    c.pszContent = content.empty() ? nullptr : content.c_str(); c.cButtons = (UINT)b.size(); c.pButtons = b.data();
    int r = 0;
    if (FAILED(TaskDialogIndirect(&c, &r, nullptr, nullptr))) return -1;
    return r >= 100 ? r - 100 : -1;
}
static Event* Sel() { return g_selEvent ? FindEvent(g_selEvent) : nullptr; }
static std::wstring Quote(const std::wstring& s) { return L"\u201C" + (s.empty() ? std::wstring(L"(no title)") : s) + L"\u201D"; }
static std::wstring JoinLink(const Event& e) {
    if (!Trim(e.confLink).empty()) return Trim(e.confLink);
    std::wstring l = Trim(e.location);
    if (_wcsnicmp(l.c_str(), L"https://", 8) == 0 || _wcsnicmp(l.c_str(), L"http://", 7) == 0) return l;
    size_t p = e.description.find(L"https://");
    if (p != std::wstring::npos) {
        size_t q = e.description.find_first_of(L" \t\r\n<>\"", p);
        std::wstring u = e.description.substr(p, q == std::wstring::npos ? std::wstring::npos : q - p);
        if (DetectConfProvider(u) != CONF_NONE) return u;
    }
    return L"";
}
static MIN_T SelDisplayStart(const Event& e) { return EventToDisplay(e, g_selWall ? g_selWall : e.start); }

// ---------------------------------------------------------------- events
static void StoreNewEvent(HWND owner, Event& e, int page) {
    g_evSendInvites = false;
    if (!EventDialog(owner, e, true, page)) return;
    e.id = NewId(); e.modified = NowUtc();
    g.events.push_back(e);
    int id = e.id;
    if (g_evSendInvites && !e.attendees.empty()) SendInvitations(*FindEvent(id), owner);
    Event* ne = FindEvent(id);
    std::wstring block;
    if (ne && ne->busy != BUSY_FOCUS && ne->busy != BUSY_OOO && FindFocusConflict(*ne, &block))
        App_SetStatus(L"Note: " + Quote(ne->title) + L" overlaps the focus/out-of-office block " + Quote(block) + L".");
    g_selEvent = id; g_selWall = ne ? ne->start : 0;
    App_GoToDay(DayOf(EventToDisplay(*ne, ne->start)), -1);
    App_ModelChanged();
}

void App_NewEventAt(HWND owner, MIN_T start, bool allDay) {
    Event e = NewEventTemplate(start, allDay);
    StoreNewEvent(owner, e, 0);
}

void App_NewEvent(HWND owner, int kind) {
    MIN_T start;
    if (s_forceStart) { start = s_forceStart; s_forceStart = 0; }
    else if (g_curDay == TodayDisplay()) { MIN_T n = NowDisplay(); start = (n + 29) / 30 * 30; }
    else start = (MIN_T)g_curDay * DAYMIN + g.set.workStart * 60;
    bool allDay = kind == NEW_ALLDAY || kind == NEW_OOO;
    Event e = NewEventTemplate(start, allDay);
    int page = 0;
    switch (kind) {
    case NEW_MEETING: {
        page = 3;
        int p = s_forceConf >= 0 ? s_forceConf : (g.set.autoAddConf ? g.set.defaultConf : CONF_NONE);
        if (p > CONF_NONE && p < CONF_COUNT) { e.confProvider = p; e.confLink = GenerateConfLink(p); }
        break;
    }
    case NEW_FOCUS:
        e.title = L"Focus time"; e.busy = BUSY_FOCUS; e.autoDecline = g.set.autoDecline; e.declineMsg = g.set.declineMsg;
        e.duration = std::max(15, g.set.focusLength); e.reminders.clear(); break;
    case NEW_OOO:
        e.title = L"Out of office"; e.busy = BUSY_OOO; e.autoDecline = true; e.declineMsg = g.set.declineMsg; e.reminders.clear(); break;
    }
    s_forceConf = -1;
    StoreNewEvent(owner, e, page);
}

void App_EditEvent(HWND owner, int id, MIN_T wall, int page) {
    Event* e = FindEvent(id); if (!e) return;
    bool occurrence = false;
    if (wall && e->rec.freq && e->myResponse < 0 && !EventReadOnly(*e)) {
        int c = Choose(owner, L"Open Recurring Item", Quote(e->title) + L" is a recurring event.",
            L"Do you want to open only this occurrence or the whole series?",
            { L"Just this one\nChanges apply to the occurrence on " + FmtDate(EventToDisplay(*e, wall), L"dddd, MMMM d"),
              L"The entire series\nChanges apply to every occurrence" });
        if (c < 0) return;
        occurrence = c == 0;
    }
    Event copy = *e;
    if (occurrence) { copy.rec = Recurrence(); copy.exdates.clear(); copy.start = wall; }
    int oldResp = copy.myResponse;
    g_evSendInvites = false;
    if (!EventDialog(owner, copy, false, page)) return;
    e = FindEvent(id); if (!e) return;
    if (occurrence) {
        e->exdates.push_back(DayOf(wall)); e->modified = NowUtc();
        copy.id = NewId(); copy.uid = NewUid(); copy.modified = NowUtc();
        g.events.push_back(copy); id = copy.id;
        g_selEvent = id; g_selWall = copy.start;
    } else *e = copy;
    Event* ne = FindEvent(id);
    if (ne->myResponse >= 0 && ne->myResponse != oldResp) {
        int resp = ne->myResponse; ne->myResponse = oldResp;
        if (resp == RSVP_PROPOSED) {
            MIN_T st = EventToDisplay(*ne, ne->start); std::wstring cmt;
            if (ProposeTimeDialog(owner, st, ne->duration, &cmt)) SendResponse(*ne, resp, owner, st, cmt);
        } else SendResponse(*ne, resp, owner);
    }
    ne = FindEvent(id);
    if (g_evSendInvites && ne && !ne->attendees.empty()) SendInvitations(*ne, owner);
    App_ModelChanged();
}

static void DeleteSelectedTask(HWND owner) {
    HWND tl = Main_TaskList();
    int i = ListView_GetNextItem(tl, -1, LVNI_SELECTED); if (i < 0) return;
    LVITEMW it{}; it.mask = LVIF_PARAM; it.iItem = i; ListView_GetItem(tl, &it);
    Task* t = FindTask((int)it.lParam); if (!t) return;
    if (MessageBoxW(owner, (L"Delete the task " + Quote(t->title) + L"?").c_str(), L"Delete Task", MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    int id = t->id;
    g.tasks.erase(std::remove_if(g.tasks.begin(), g.tasks.end(), [&](const Task& x) { return x.id == id; }), g.tasks.end());
    App_ModelChanged();
}

static void DeleteSelected(HWND owner) {
    if (GetFocus() == Main_TaskList()) { DeleteSelectedTask(owner); return; }
    Event* e = Sel(); if (!e) { App_SetStatus(L"Select an event to delete."); return; }
    if (EventReadOnly(*e)) { MessageBoxW(owner, L"This event belongs to a read-only calendar (synced account or view-only share) and cannot be deleted here.", L"Delete", MB_ICONINFORMATION); return; }
    int id = e->id; bool series = true;
    if (e->rec.freq && g_selWall) {
        int c = Choose(owner, L"Delete", L"Delete " + Quote(e->title) + L"?", L"This is a recurring event.",
            { L"Delete this occurrence\n" + FmtDate(EventToDisplay(*e, g_selWall), L"dddd, MMMM d"), L"Delete the entire series" });
        if (c < 0) return;
        series = c == 1;
    } else if (MessageBoxW(owner, (L"Delete " + Quote(e->title) + L"?").c_str(), L"Delete", MB_YESNO | MB_ICONQUESTION) != IDYES) return;

    if (e->myResponse < 0 && !e->attendees.empty()) {
        if (series) {
            if (MessageBoxW(owner, L"Send a cancellation notice to the attendees?", L"Cancel Meeting", MB_YESNO | MB_ICONQUESTION) == IDYES) SendInvitations(*e, owner, true);
        } else {
            e->exdates.push_back(DayOf(g_selWall)); e->modified = NowUtc();
            if (MessageBoxW(owner, L"Send an updated invitation (without this occurrence) to the attendees?", L"Cancel Occurrence", MB_YESNO | MB_ICONQUESTION) == IDYES) SendInvitations(*e, owner);
            g_selEvent = 0; g_selWall = 0; App_ModelChanged(); return;
        }
    } else if (e->myResponse >= 0 && e->myResponse != RSVP_DECLINED && !e->organizer.empty() && series) {
        if (MessageBoxW(owner, L"Notify the organizer that you are declining?", L"Delete Invitation", MB_YESNO | MB_ICONQUESTION) == IDYES) SendResponse(*e, RSVP_DECLINED, owner);
    }
    e = FindEvent(id);
    if (e) {
        if (series) g.events.erase(std::remove_if(g.events.begin(), g.events.end(), [&](const Event& x) { return x.id == id; }), g.events.end());
        else { e->exdates.push_back(DayOf(g_selWall)); e->modified = NowUtc(); }
    }
    g_selEvent = 0; g_selWall = 0;
    App_ModelChanged();
}

static void Respond(HWND owner, int resp) {
    Event* e = Sel();
    if (!e || e->myResponse < 0) { MeetingsDialog(owner, 1); App_ModelChanged(); return; }
    if (resp == RSVP_ACCEPTED) {
        std::wstring b;
        if (FindFocusConflict(*e, &b) && MessageBoxW(owner, (L"This meeting overlaps your focus/out-of-office block " + Quote(b) + L".\n\nAccept anyway?").c_str(),
            L"Accept", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    }
    if (resp == RSVP_PROPOSED) {
        MIN_T st = SelDisplayStart(*e); std::wstring cmt;
        if (!ProposeTimeDialog(owner, st, e->duration, &cmt)) return;
        SendResponse(*e, resp, owner, st, cmt);
    } else SendResponse(*e, resp, owner);
    App_ModelChanged();
}

static void AddConference(HWND owner, int provider) {
    Event* e = Sel();
    if (!e) { s_forceConf = provider; App_NewEvent(owner, NEW_MEETING); return; }
    if (EventReadOnly(*e) || e->myResponse >= 0) { MessageBoxW(owner, L"Only the organizer of a writable event can add a meeting link.", kConfNames[provider], MB_ICONINFORMATION); return; }
    if (!e->confLink.empty() && MessageBoxW(owner, (L"This event already has a meeting link:\n" + e->confLink + L"\n\nReplace it?").c_str(), kConfNames[provider], MB_YESNO | MB_ICONQUESTION) != IDYES) return;
    e->confProvider = provider; e->confLink = GenerateConfLink(provider); e->modified = NowUtc();
    int id = e->id;
    App_ModelChanged();
    e = FindEvent(id);
    if (e && !e->attendees.empty() && MessageBoxW(owner, L"Meeting link added. Send the updated invitation to the attendees now?", kConfNames[provider], MB_YESNO | MB_ICONQUESTION) == IDYES)
        SendInvitations(*e, owner);
    else App_SetStatus(std::wstring(kConfNames[provider]) + L" link added: " + (e ? e->confLink : L""));
}

static HBITMAP ColorBitmap(COLORREF c) {
    HDC sdc = GetDC(nullptr); HDC dc = CreateCompatibleDC(sdc);
    int sz = GetSystemMetrics(SM_CXMENUCHECK);
    HBITMAP bm = CreateCompatibleBitmap(sdc, sz, sz); HGDIOBJ old = SelectObject(dc, bm);
    RECT r{ 0, 0, sz, sz }; FillRect(dc, &r, GetSysColorBrush(COLOR_MENU));
    HBRUSH b = CreateSolidBrush(c); RECT r2{ 2, 2, sz - 2, sz - 2 }; FillRect(dc, &r2, b); DeleteObject(b);
    SelectObject(dc, old); DeleteDC(dc); ReleaseDC(nullptr, sdc); return bm;
}
static void CategorizeMenu(HWND owner) {
    Event* e = Sel();
    if (!e || EventReadOnly(*e)) { App_SetStatus(L"Select a writable event to categorize."); return; }
    HMENU m = CreatePopupMenu(); std::vector<HBITMAP> bms;
    AppendMenuW(m, MF_STRING | (e->category.empty() ? MF_CHECKED : 0), IDM_CAT_NONE, L"&No category");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < g.categories.size(); ++i) {
        AppendMenuW(m, MF_STRING | (e->category == g.categories[i].name ? MF_CHECKED : 0), IDM_CAT_FIRST + (UINT)i, g.categories[i].name.c_str());
        HBITMAP bm = ColorBitmap(g.categories[i].color); bms.push_back(bm);
        MENUITEMINFOW mi{ sizeof(mi) }; mi.fMask = MIIM_BITMAP; mi.hbmpItem = bm; SetMenuItemInfoW(m, IDM_CAT_FIRST + (UINT)i, FALSE, &mi);
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, IDM_CATEGORIES, L"&Manage categories...");
    POINT pt; GetCursorPos(&pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(m); for (auto b : bms) DeleteObject(b);
    if (cmd) SendMessageW(owner, WM_COMMAND, cmd, 0);
}

void ShowContextMenu(HWND owner, POINT pt) {
    Event* e = Sel();
    HMENU m = CreatePopupMenu();
    if (e) {
        bool ro = EventReadOnly(*e);
        AppendMenuW(m, MF_STRING, IDM_OPEN, L"&Open\tCtrl+O");
        if (!ro) {
            HMENU cm = CreatePopupMenu();
            AppendMenuW(cm, MF_STRING | (e->category.empty() ? MF_CHECKED : 0), IDM_CAT_NONE, L"&No category");
            for (size_t i = 0; i < g.categories.size(); ++i)
                AppendMenuW(cm, MF_STRING | (e->category == g.categories[i].name ? MF_CHECKED : 0), IDM_CAT_FIRST + (UINT)i, g.categories[i].name.c_str());
            AppendMenuW(m, MF_POPUP, (UINT_PTR)cm, L"&Categorize");
        }
        std::wstring link = JoinLink(*e);
        if (!link.empty()) {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, IDM_JOIN, L"&Join online meeting");
            AppendMenuW(m, MF_STRING, IDM_COPYLINK, L"Copy meeting &link");
        }
        if (e->myResponse >= 0) {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING | (e->myResponse == RSVP_ACCEPTED ? MF_CHECKED : 0), IDM_ACCEPT, L"&Accept");
            AppendMenuW(m, MF_STRING | (e->myResponse == RSVP_TENTATIVE ? MF_CHECKED : 0), IDM_TENTATIVE, L"&Tentative");
            AppendMenuW(m, MF_STRING | (e->myResponse == RSVP_DECLINED ? MF_CHECKED : 0), IDM_DECLINE, L"De&cline");
            AppendMenuW(m, MF_STRING, IDM_PROPOSE, L"&Propose new time...");
        } else if (!ro) {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, IDM_ATTENDEES, L"Invite &attendees...");
            if (!e->attendees.empty()) {
                AppendMenuW(m, MF_STRING, IDM_RESENDINVITES, L"&Send invitations / updates");
                AppendMenuW(m, MF_STRING, IDM_RSVPTRACKER, L"&RSVP tracker...");
            }
        }
        if (!ro) { AppendMenuW(m, MF_SEPARATOR, 0, nullptr); AppendMenuW(m, MF_STRING, IDM_DELETE, L"&Delete\tDel"); }
    } else {
        AppendMenuW(m, MF_STRING, IDM_NEWEVENT, L"New &event...\tCtrl+N");
        AppendMenuW(m, MF_STRING, IDM_NEWALLDAY, L"New &all-day event...");
        AppendMenuW(m, MF_STRING, IDM_NEWMEETING, L"New &meeting...\tCtrl+Shift+M");
        AppendMenuW(m, MF_STRING, IDM_NEWFOCUS, L"New &focus block...");
        AppendMenuW(m, MF_STRING, IDM_NEWOOO, L"New &out of office...");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, IDM_TODAY, L"Go to &today\tCtrl+T");
        AppendMenuW(m, MF_STRING, IDM_GOTO, L"&Go to date...\tCtrl+G");
    }
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, owner, nullptr);
    DestroyMenu(m);
    if (!cmd) return;
    if (!e && cmd != IDM_TODAY && cmd != IDM_GOTO) s_forceStart = g_newAt;
    SendMessageW(owner, WM_COMMAND, cmd, 0);
    s_forceStart = 0;
}

// ---------------------------------------------------------------- calendars / data
static void NewCalendar(HWND owner) {
    static const COLORREF pal[] = { RGB(0,120,215), RGB(16,137,62), RGB(202,80,16), RGB(135,100,184), RGB(194,57,179), RGB(0,153,188), RGB(196,38,46), RGB(122,117,116) };
    Calendar c; c.id = NewId();
    for (int n = (int)g.calendars.size() + 1;; ++n) {
        std::wstring nm = L"Calendar " + std::to_wstring(n); bool used = false;
        for (auto& k : g.calendars) if (_wcsicmp(k.name.c_str(), nm.c_str()) == 0) used = true;
        if (!used) { c.name = nm; break; }
    }
    c.color = pal[g.calendars.size() % 8]; c.owner = g.set.userEmail;
    g.calendars.push_back(c);
    App_ModelChanged();
    App_SetStatus(L"Created " + Quote(c.name) + L". Rename it, pick a color or share it from the Calendars and Sharing tabs.");
    OrganizerDialog(owner, 0);
    App_ModelChanged();
}

static void OpenShared(HWND owner) {
    std::wstring p;
    if (!OpenFileDlg(owner, p, L"Shared calendar (*.wcal)\0*.wcal\0All files\0*.*\0", L"wcal", false)) return;
    for (auto& k : g.calendars) if (_wcsicmp(k.sharedPath.c_str(), p.c_str()) == 0) { MessageBoxW(owner, L"You are already subscribed to this calendar.", L"Open Shared", MB_ICONINFORMATION); return; }
    Calendar nc; nc.id = NewId(); nc.sharedPath = p; nc.color = RGB(135, 100, 184);
    g.calendars.push_back(nc);
    Calendar& ref = g.calendars.back();
    if (!LoadSharedCalendarFile(ref)) { g.calendars.pop_back(); MessageBoxW(owner, L"This file is not a WinCalendar shared calendar.", L"Open Shared", MB_ICONERROR); return; }
    if (ref.name.empty()) ref.name = L"Shared calendar";
    std::wstring me = Lower(g.set.userEmail); bool member = Lower(ref.owner) == me;
    for (auto& m : ref.members) if (Lower(m.email) == me) member = true;
    std::wstring nm = ref.name;
    App_ModelChanged();
    MessageBoxW(owner, (L"Subscribed to " + Quote(nm) + L"." + (member || ref.members.empty() ? L"" : L"\n\nYou (" + g.set.userEmail + L") are not a member of this calendar, so it is shown read-only.")).c_str(),
        L"Open Shared", MB_ICONINFORMATION);
}

static void ExportIcs(HWND owner) {
    std::vector<std::wstring> btns{ L"All calendars" }; std::vector<int> ids{ -1 };
    for (auto& c : g.calendars) { btns.push_back(c.name); ids.push_back(c.id); }
    int ch = btns.size() == 2 ? 0 : Choose(owner, L"Export", L"Which calendar do you want to export?", L"Events are written as an iCalendar (.ics) file that other calendar apps can import.", btns);
    if (ch < 0) return;
    std::wstring p = ch == 0 ? L"calendar.ics" : btns[ch] + L".ics";
    if (!OpenFileDlg(owner, p, L"iCalendar (*.ics)\0*.ics\0", L"ics", true)) return;
    if (ExportIcsFile(p, ids[ch])) App_SetStatus(L"Exported to " + p);
    else MessageBoxW(owner, (L"Could not write " + p).c_str(), L"Export", MB_ICONERROR);
}

void App_SyncAll(HWND owner, bool interactive) {
    if (g.accounts.empty()) {
        if (interactive && MessageBoxW(owner, L"No calendar accounts are configured.\n\nAdd an iCloud, Google Workspace, Exchange or other account now?", L"Sync", MB_YESNO | MB_ICONQUESTION) == IDYES)
            OrganizerDialog(owner, 3);
        return;
    }
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    App_SetStatus(L"Synchronizing accounts...");
    std::wstring rep; int ok = 0, n = 0;
    for (size_t i = 0; i < g.accounts.size(); ++i) {
        Account& a = g.accounts[i];
        if (!interactive && !a.autoSync) continue;
        std::wstring msg; bool r = SyncAccount(g.accounts[i], msg);
        ok += r; n++;
        rep += (g.accounts[i].name.empty() ? std::wstring(kProviderNames[g.accounts[i].provider]) : g.accounts[i].name) + L": " + msg + L"\n";
    }
    SetCursor(old);
    App_ModelChanged();
    App_SetStatus(L"Sync finished: " + std::to_wstring(ok) + L" of " + std::to_wstring(n) + L" account(s) updated.");
    if (interactive) MessageBoxW(owner, rep.c_str(), L"Sync", ok == n ? MB_ICONINFORMATION : MB_ICONWARNING);
}

void AutoSyncTick(HWND owner) {
    static std::map<int, ULONGLONG> next;
    ULONGLONG now = GetTickCount64(); bool due = false;
    for (auto& a : g.accounts) {
        if (!a.autoSync || Trim(a.url).empty()) continue;
        auto it = next.find(a.id);
        if (it == next.end()) {
            MIN_T age = NowUtc() - a.lastSync;
            next[a.id] = now + (age >= 30 ? 0 : (ULONGLONG)(30 - age) * 60000);
            if (age >= 30) due = true;
        } else if (now >= it->second) due = true;
    }
    if (!due) return;
    for (auto& a : g.accounts) next[a.id] = now + 30ull * 60000;
    App_SyncAll(owner, false);
}

// ---------------------------------------------------------------- reminders
void CheckReminders() {
    MIN_T now = NowUtc(), nd = NowDisplay();
    auto occs = Expand(nd - DAYMIN, nd + DAYMIN * 15, true);
    bool changed = false;
    for (auto& o : occs) {
        Event* e = FindEvent(o.id);
        if (!e || e->myResponse == RSVP_DECLINED) continue;
        for (auto& r : e->reminders) {
            MIN_T fire = o.utc - r.minutes;
            if (fire > now || now > o.utc + std::max(5, e->duration)) continue;
            std::wstring key = e->uid + L"|" + std::to_wstring(o.utc) + L"|" + std::to_wstring(r.minutes);
            if (!g.fired.insert(key).second) continue;
            changed = true;
            bool popup = r.method != REM_EMAIL, mail = r.method != REM_POPUP;
            std::wstring when = FmtWhen(*e, o.start);
            if (popup && g.set.remindPopup) Reminders_Add(o, r.minutes);
            if (popup) App_Notify(L"Reminder: " + (e->title.empty() ? std::wstring(L"(no title)") : e->title), when + (e->location.empty() ? L"" : L"\n" + e->location));
            if (g.set.remindSound) PlaySoundW(L"Notification.Reminder", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT) || PlaySoundW(L"SystemNotification", nullptr, SND_ALIAS | SND_ASYNC);
            if (mail || g.set.remindEmail) {
                std::wstring body = L"Reminder: " + e->title + L"\r\nWhen: " + when + L"\r\n";
                if (!e->location.empty()) body += L"Where: " + e->location + L"\r\n";
                if (!e->confLink.empty()) body += L"Join: " + e->confLink + L"\r\n";
                if (!e->description.empty()) body += L"\r\n" + e->description + L"\r\n";
                SendMail({ { g.set.userName, g.set.userEmail } }, L"Reminder: " + e->title + L" (" + when + L")", body, nullptr, L"", false, g_hwnd);
            }
        }
    }
    // forget reminders for occurrences more than two days in the past
    for (auto it = g.fired.begin(); it != g.fired.end();) {
        size_t a = it->find(L'|'), b = a == std::wstring::npos ? a : it->find(L'|', a + 1);
        MIN_T utc = (a != std::wstring::npos && b != std::wstring::npos) ? _wtoi64(it->substr(a + 1, b - a - 1).c_str()) : 0;
        if (utc < now - 2 * DAYMIN) { it = g.fired.erase(it); changed = true; } else ++it;
    }
    if (changed) SaveModel();
}

// ---------------------------------------------------------------- ribbon state
bool App_CommandEnabled(UINT id) {
    Event* e = Sel();
    switch (id) {
    case IDM_OPEN: return e != nullptr;
    case IDM_DELETE: case IDM_CATEGORIZE: return e && !EventReadOnly(*e);
    case IDM_ACCEPT: case IDM_TENTATIVE: case IDM_DECLINE: case IDM_PROPOSE: return e && e->myResponse >= 0;
    case IDM_SENDINVITES: return e && e->myResponse < 0 && !e->attendees.empty() && !EventReadOnly(*e);
    case IDM_JOIN: return e && !JoinLink(*e).empty();
    }
    return true;
}
bool App_CommandChecked(UINT id, bool& checked) {
    if (id >= IDM_VIEWDAY && id <= IDM_VIEWAGENDA) { checked = (int)(id - IDM_VIEWDAY) == g_viewMode; return true; }
    if (id == IDM_AUTOTZ) { checked = g.set.autoTz; return true; }
    if (id == IDM_SHOWCOMPLETED) { checked = g.set.showCompleted; return true; }
    return false;
}

static const wchar_t* kHelp =
    L"WinCalendar keyboard shortcuts\n\n"
    L"Ctrl+N\tNew event\nCtrl+Shift+M\tNew meeting\nCtrl+Shift+K\tNew task\nCtrl+O / Enter\tOpen selected event\nDel\tDelete selection\n"
    L"Ctrl+T\tGo to today\nCtrl+G\tGo to date\nCtrl+1..5\tDay / Week / Month / Year / Agenda view\nPgUp / PgDn\tPrevious / next period\n"
    L"Ctrl+F\tSmart search\nCtrl+E\tQuick search box\nCtrl+I\tImport .ics\nF5\tSync accounts\nF1\tAbout\n\n"
    L"Mouse: double-click an empty slot to create an event, double-click an event to open it, right-click for commands.\n\n"
    L"Invitations and replies are exchanged as standard iCalendar (.ics) e-mail attachments; use Import Invite / Import Replies to process files you receive.";

// ---------------------------------------------------------------- dispatcher
void OnCommand(HWND hwnd, int id) {
    Event* e = Sel();
    if (id >= IDM_CAT_FIRST && id < IDM_CAT_FIRST + (int)g.categories.size()) {
        if (e && !EventReadOnly(*e)) { e->category = g.categories[id - IDM_CAT_FIRST].name; e->modified = NowUtc(); App_ModelChanged(); }
        return;
    }
    switch (id) {
    case IDM_NEWEVENT: App_NewEvent(hwnd, NEW_EVENT); break;
    case IDM_NEWALLDAY: App_NewEvent(hwnd, NEW_ALLDAY); break;
    case IDM_NEWMEETING: App_NewEvent(hwnd, NEW_MEETING); break;
    case IDM_NEWFOCUS: App_NewEvent(hwnd, NEW_FOCUS); break;
    case IDM_NEWOOO: App_NewEvent(hwnd, NEW_OOO); break;
    case IDM_NEWTASK: {
        Task t; t.hasDue = true; t.dueDay = g_curDay;
        if (EditTaskDlg(hwnd, t, true)) { t.id = NewId(); g.tasks.push_back(t); App_ModelChanged(); }
        break;
    }
    case IDM_OPEN:
        if (GetFocus() == Main_TaskList()) { Main_EditSelectedTask(); break; }
        if (e) App_EditEvent(hwnd, e->id, g_selWall, 0); else App_SetStatus(L"Select an event first.");
        break;
    case IDM_DELETE: DeleteSelected(hwnd); break;
    case IDM_CATEGORIZE: CategorizeMenu(hwnd); break;
    case IDM_CAT_NONE: if (e && !EventReadOnly(*e)) { e->category.clear(); e->modified = NowUtc(); App_ModelChanged(); } break;

    case IDM_TODAY: View_Navigate(0); Main_SyncMonthCal(); Main_UpdateTitle(); break;
    case IDM_PREV: View_Navigate(-1); Main_SyncMonthCal(); Main_UpdateTitle(); break;
    case IDM_NEXT: View_Navigate(1); Main_SyncMonthCal(); Main_UpdateTitle(); break;
    case IDM_GOTO: { int d = g_curDay; if (GoToDateDialog(hwnd, d)) App_GoToDay(d, -1); break; }
    case IDM_VIEWDAY: case IDM_VIEWWEEK: case IDM_VIEWMONTH: case IDM_VIEWYEAR: case IDM_VIEWAGENDA:
        View_SetMode(id - IDM_VIEWDAY); Main_UpdateTitle(); Ribbon_Invalidate(); break;
    case IDM_SEARCH: SearchDialog(hwnd, Main_SearchText()); Main_UpdateTitle(); Ribbon_Invalidate(); break;
    case IDM_FOCUSSEARCH: Main_FocusSearch(); break;

    case IDM_ATTENDEES: if (e) App_EditEvent(hwnd, e->id, 0, 3); else App_NewEvent(hwnd, NEW_MEETING); break;
    case IDM_SENDINVITES: case IDM_RESENDINVITES:
        if (e && e->myResponse < 0 && !e->attendees.empty()) { SendInvitations(*e, hwnd); App_ModelChanged(); }
        else MessageBoxW(hwnd, L"Select a meeting you organize that has attendees.", L"Send Invitations", MB_ICONINFORMATION);
        break;
    case IDM_RSVPTRACKER: MeetingsDialog(hwnd, 0); App_ModelChanged(); break;
    case IDM_INVITATIONS: MeetingsDialog(hwnd, 1); App_ModelChanged(); break;
    case IDM_ACCEPT: Respond(hwnd, RSVP_ACCEPTED); break;
    case IDM_TENTATIVE: Respond(hwnd, RSVP_TENTATIVE); break;
    case IDM_DECLINE: Respond(hwnd, RSVP_DECLINED); break;
    case IDM_PROPOSE: Respond(hwnd, RSVP_PROPOSED); break;
    case IDM_ADDTEAMS: AddConference(hwnd, CONF_TEAMS); break;
    case IDM_ADDZOOM: AddConference(hwnd, CONF_ZOOM); break;
    case IDM_ADDMEET: AddConference(hwnd, CONF_MEET); break;
    case IDM_ADDJITSI: AddConference(hwnd, CONF_JITSI); break;
    case IDM_JOIN: {
        std::wstring l = e ? JoinLink(*e) : L"";
        if (l.empty()) MessageBoxW(hwnd, L"The selected event has no online meeting link.", L"Join", MB_ICONINFORMATION);
        else ShellExecuteW(hwnd, L"open", l.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    }
    case IDM_COPYLINK: if (e && !JoinLink(*e).empty()) { CopyToClipboard(hwnd, JoinLink(*e)); App_SetStatus(L"Meeting link copied to the clipboard."); } break;
    case IDM_IMPORTINVITE: case IDM_IMPORT: {
        std::wstring p;
        if (!OpenFileDlg(hwnd, p, L"iCalendar (*.ics)\0*.ics\0All files\0*.*\0", L"ics", false)) break;
        ImportIcsFile(p, hwnd, DefaultCalendarId()); App_ModelChanged(); break;
    }
    case IDM_EXPORT: ExportIcs(hwnd); break;

    case IDM_CALENDARS: OrganizerDialog(hwnd, 0); App_ModelChanged(); break;
    case IDM_NEWCALENDAR: NewCalendar(hwnd); break;
    case IDM_SHARING: OrganizerDialog(hwnd, 1); App_ModelChanged(); break;
    case IDM_OPENSHARED: OpenShared(hwnd); break;
    case IDM_CATEGORIES: OrganizerDialog(hwnd, 2); App_ModelChanged(); break;
    case IDM_ACCOUNTS: OrganizerDialog(hwnd, 3); App_ModelChanged(); break;
    case IDM_SYNC: App_SyncAll(hwnd, true); break;

    case IDM_TIMEZONES: OptionsDialog(hwnd, 1); break;
    case IDM_AUTOTZ:
        g.set.autoTz = !g.set.autoTz;
        if (!g.set.autoTz && g.set.tzKey.empty()) g.set.tzKey = SystemTzKey();
        RefreshTimeZoneCache(); App_ModelChanged();
        App_SetStatus(g.set.autoTz ? L"Times follow the Windows time zone automatically." : L"Display time zone locked to " + TzDisplayName(DisplayTzKey()) + L".");
        break;
    case IDM_REMINDERS: Reminders_Show(hwnd); break;
    case IDM_NOTIFYSETTINGS: OptionsDialog(hwnd, 2); break;
    case IDM_TESTNOTIFY:
        App_Notify(L"WinCalendar test notification", L"Reminders will appear like this.");
        if (g.set.remindSound) PlaySoundW(L"Notification.Reminder", nullptr, SND_ALIAS | SND_ASYNC);
        break;
    case IDM_FOCUSSETTINGS: OptionsDialog(hwnd, 4); break;
    case IDM_TASKS: TasksManagerDialog(hwnd); App_ModelChanged(); break;
    case IDM_SHOWCOMPLETED: g.set.showCompleted = !g.set.showCompleted; App_ModelChanged(); break;
    case IDM_OPTIONS: OptionsDialog(hwnd, 0); break;
    case IDM_EXIT: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
    case IDM_HELP: MessageBoxW(hwnd, kHelp, L"WinCalendar Help", MB_ICONINFORMATION); break;
    case IDM_ABOUT:
        MessageBoxW(hwnd, L"WinCalendar 1.0\n\nA calendar built only on Windows APIs: Win32, Windows Ribbon Framework, Common Controls, GDI, "
            L"WinHTTP, Simple MAPI and the Windows ICU time-zone database.\n\nClick the ribbon's Help (?) button for keyboard shortcuts.", L"About WinCalendar", MB_ICONINFORMATION);
        break;
    }
    Ribbon_Invalidate();
}
