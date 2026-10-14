// WinCalendar - tabbed Organizer dialog (Calendars, Sharing, Categories, Accounts)
#include "dlgutil.h"
#include <prsht.h>

static void DrawSwatch(LPARAM lp, COLORREF c) {
    DRAWITEMSTRUCT* di = (DRAWITEMSTRUCT*)lp;
    RECT r = di->rcItem;
    FillRect(di->hDC, &r, GetSysColorBrush(COLOR_BTNFACE));
    InflateRect(&r, -2, -2);
    SetDCBrushColor(di->hDC, c); FillRect(di->hDC, &r, (HBRUSH)GetStockObject(DC_BRUSH));
    FrameRect(di->hDC, &r, GetSysColorBrush(COLOR_BTNSHADOW));
    if (di->itemState & ODS_FOCUS) { InflateRect(&r, 1, 1); DrawFocusRect(di->hDC, &r); }
}
static void SetImages(HWND lv, const std::vector<COLORREF>& cols) {
    HIMAGELIST il = MakeColorImageList(cols, GetSystemMetrics(SM_CXSMICON));
    HIMAGELIST old = ListView_SetImageList(lv, il, LVSIL_SMALL);
    if (old) ImageList_Destroy(old);
}
static bool IsSelChange(LPARAM lp, int id) {
    NMHDR* nh = (NMHDR*)lp;
    if (nh->idFrom != (UINT_PTR)id || nh->code != LVN_ITEMCHANGED) return false;
    NMLISTVIEW* nl = (NMLISTVIEW*)lp;
    return (nl->uChanged & LVIF_STATE) && ((nl->uNewState ^ nl->uOldState) & LVIS_SELECTED);
}
static std::wstring CalType(const Calendar& c) {
    if (c.accountId) { Account* a = FindAccount(c.accountId); return L"Account" + (a ? L" (" + std::wstring(kProviderNames[a->provider]) + L")" : L""); }
    if (!c.sharedPath.empty()) {
        if (c.owner.empty() || Lower(c.owner) == Lower(g.set.userEmail)) return L"Shared by me";
        return CalendarReadOnly(&c) ? L"Shared (view only)" : L"Shared (can edit)";
    }
    return L"Local";
}

// ================================================================== Calendars
static bool s_calFilling; static COLORREF s_calColor = RGB(0, 120, 215);
static void FillCalendars(HWND d, int selId) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    s_calFilling = true;
    ListView_DeleteAllItems(lv);
    std::vector<COLORREF> cols; for (auto& c : g.calendars) cols.push_back(c.color);
    SetImages(lv, cols);
    int i = 0;
    for (auto& c : g.calendars) {
        int n = 0; for (auto& e : g.events) if (e.calendarId == c.id) n++;
        std::wstring name = c.name + (c.id == DefaultCalendarId() ? L" (default)" : L"");
        int it = LvAdd(lv, { name, CalType(c), std::to_wstring(n) }, c.id, i++);
        ListView_SetCheckState(lv, it, c.visible);
        if (c.id == selId) LvSelect(lv, it);
    }
    s_calFilling = false;
}
static INT_PTR CALLBACK CalendarsProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    auto selCal = [&]() { return FindCalendar((int)LvData(lv, LvSel(lv))); };
    switch (msg) {
    case WM_INITDIALOG:
        LvCols(lv, { { L"Calendar", 200 }, { L"Type", 150 }, { L"Events", 60 } }, LVS_EX_CHECKBOXES);
        SetText(d, IDC_HINT, L"Synced account calendars are read-only. Shared calendars are stored in a file that other members open from the Sharing tab.");
        FillCalendars(d, g.calendars.empty() ? 0 : g.calendars[0].id);
        return TRUE;
    case WM_DRAWITEM: if (wp == IDC_COLORSWATCH) { DrawSwatch(lp, s_calColor); return TRUE; } break;
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->code == PSN_SETACTIVE) { Calendar* c = selCal(); FillCalendars(d, c ? c->id : 0); return TRUE; }
        if (nh->idFrom == IDC_LIST && nh->code == LVN_ITEMCHANGED && !s_calFilling) {
            NMLISTVIEW* nl = (NMLISTVIEW*)lp;
            if ((nl->uChanged & LVIF_STATE) && ((nl->uNewState ^ nl->uOldState) & LVIS_STATEIMAGEMASK)) {
                if (Calendar* c = FindCalendar((int)nl->lParam)) { c->visible = ListView_GetCheckState(lv, nl->iItem) != 0; App_ModelChanged(); }
            }
            if (IsSelChange(lp, IDC_LIST)) if (Calendar* c = selCal()) {
                SetText(d, IDC_NAME, c->name); s_calColor = c->color; InvalidateRect(GetDlgItem(d, IDC_COLORSWATCH), nullptr, TRUE);
                Enable(d, IDC_DELETE, !c->accountId && g.calendars.size() > 1); Enable(d, IDC_SETDEFAULT, !CalendarReadOnly(c));
            }
        }
        break;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_COLOR: case IDC_COLORSWATCH:
            if (PickColor(d, s_calColor)) {
                InvalidateRect(GetDlgItem(d, IDC_COLORSWATCH), nullptr, TRUE);
                if (Calendar* c = selCal()) { c->color = s_calColor; App_ModelChanged(); FillCalendars(d, c->id); }
            }
            break;
        case IDC_NEW: {
            std::wstring name = Trim(DlgText(d, IDC_NAME));
            for (auto& c : g.calendars) if (_wcsicmp(c.name.c_str(), name.c_str()) == 0) name.clear();
            if (name.empty()) name = L"Calendar " + std::to_wstring(g.calendars.size() + 1);
            Calendar c; c.id = NewId(); c.name = name; c.color = s_calColor;
            g.calendars.push_back(c); App_ModelChanged(); FillCalendars(d, c.id);
            break;
        }
        case IDC_RENAME: {
            Calendar* c = selCal(); std::wstring name = Trim(DlgText(d, IDC_NAME));
            if (!c || name.empty()) break;
            c->name = name; App_ModelChanged(); FillCalendars(d, c->id); break;
        }
        case IDC_DELETE: {
            Calendar* c = selCal(); if (!c) break;
            if (c->accountId) { MessageBoxW(d, L"Remove the account on the Accounts tab to remove its calendar.", L"Calendars", MB_ICONINFORMATION); break; }
            if (g.calendars.size() <= 1) break;
            int n = 0; for (auto& e : g.events) if (e.calendarId == c->id) n++;
            std::wstring q = L"Delete the calendar \"" + c->name + L"\"" + (n ? L" and its " + std::to_wstring(n) + L" event(s)?" : L"?");
            if (!c->sharedPath.empty()) q += L"\n\nThe shared file itself is not deleted.";
            if (MessageBoxW(d, q.c_str(), L"Delete calendar", MB_YESNO | MB_ICONWARNING) != IDYES) break;
            int id = c->id;
            g.events.erase(std::remove_if(g.events.begin(), g.events.end(), [&](const Event& e) { return e.calendarId == id; }), g.events.end());
            g.calendars.erase(std::remove_if(g.calendars.begin(), g.calendars.end(), [&](const Calendar& k) { return k.id == id; }), g.calendars.end());
            if (g.set.defaultCalendar == id) g.set.defaultCalendar = DefaultCalendarId();
            App_ModelChanged(); FillCalendars(d, g.calendars[0].id); break;
        }
        case IDC_SETDEFAULT: if (Calendar* c = selCal()) { g.set.defaultCalendar = c->id; App_ModelChanged(); FillCalendars(d, c->id); } break;
        }
        return TRUE;
    }
    return FALSE;
}

// ================================================================== Sharing
static int s_shareCal;
static void ShareShowMembers(HWND d) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    ListView_DeleteAllItems(lv);
    Calendar* c = FindCalendar(s_shareCal);
    if (!c) return;
    SetText(d, IDC_SHAREPATH, c->sharedPath);
    std::wstring owner = c->owner.empty() ? g.set.userEmail : c->owner;
    SetText(d, IDC_OWNER, c->sharedPath.empty() ? L"Not shared" : L"Owner: " + owner);
    for (size_t i = 0; i < c->members.size(); i++) LvAdd(lv, { c->members[i].email, c->members[i].perm ? L"Can edit" : L"Can view" }, (LPARAM)i);
    bool mine = c->owner.empty() || Lower(c->owner) == Lower(g.set.userEmail);
    bool ok = mine && !c->accountId;
    for (int id : { IDC_SHAREPATH, IDC_BROWSE, IDC_EMAIL, IDC_PERM, IDC_ADD, IDC_REMOVE, IDC_PUBLISH, IDC_EMAILSHARE }) Enable(d, id, ok);
    Enable(d, IDC_STOPSHARE, !c->sharedPath.empty());
    SetText(d, IDC_STOPSHARE, mine ? L"S&top sharing" : L"Unsubscri&be");
    std::wstring hint;
    if (c->accountId) hint = L"Account calendars are shared by their provider (Google, iCloud, Exchange).";
    else if (!mine) hint = L"This calendar is shared with you by " + c->owner + L". " + (CalendarReadOnly(c) ? L"You can view it." : L"You can view and edit it; changes are saved to the shared file.");
    else hint = L"Pick a file in a folder all members can reach (OneDrive, SharePoint, network share), add members with view or edit rights, then click Share. Members use 'Open shared...' to subscribe.";
    SetText(d, IDC_HINT, hint);
}
static void FillShareCombo(HWND d) {
    CbReset(d, IDC_CALENDAR);
    for (auto& c : g.calendars) if (!c.accountId) CbAdd(d, IDC_CALENDAR, c.name, c.id);
    if (!FindCalendar(s_shareCal) || FindCalendar(s_shareCal)->accountId) {
        s_shareCal = 0;
        for (auto& c : g.calendars) if (!c.accountId) { s_shareCal = c.id; break; }
    }
    CbSelData(d, IDC_CALENDAR, s_shareCal);
    ShareShowMembers(d);
}
static std::wstring SafeFileName(std::wstring s) {
    for (auto& ch : s) if (wcschr(L"\\/:*?\"<>|", ch)) ch = L'_';
    return s;
}
static INT_PTR CALLBACK SharingProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    switch (msg) {
    case WM_INITDIALOG:
        LvCols(lv, { { L"Member", 300 }, { L"Permission", 120 } });
        CbAdd(d, IDC_PERM, L"Can view", 0); CbAdd(d, IDC_PERM, L"Can edit", 1); CbSelData(d, IDC_PERM, 0);
        SendDlgItemMessageW(d, IDC_EMAIL, EM_SETCUEBANNER, TRUE, (LPARAM)L"member@example.com");
        s_shareCal = g_selEvent && FindEvent(g_selEvent) ? FindEvent(g_selEvent)->calendarId : DefaultCalendarId();
        FillShareCombo(d);
        return TRUE;
    case WM_NOTIFY:
        if (((NMHDR*)lp)->code == PSN_SETACTIVE) { FillShareCombo(d); return TRUE; }
        if (IsSelChange(lp, IDC_LIST)) {
            Calendar* c = FindCalendar(s_shareCal); int i = LvSel(lv);
            if (c && i >= 0) { auto& m = c->members[LvData(lv, i)]; SetText(d, IDC_EMAIL, m.email); CbSelData(d, IDC_PERM, m.perm); }
        }
        break;
    case WM_COMMAND: {
        Calendar* c = FindCalendar(s_shareCal);
        switch (LOWORD(wp)) {
        case IDC_CALENDAR: if (HIWORD(wp) == CBN_SELCHANGE) { s_shareCal = (int)CbData(d, IDC_CALENDAR, 0); ShareShowMembers(d); } break;
        case IDC_BROWSE: {
            if (!c) break;
            wchar_t docs[MAX_PATH] = L""; SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, docs);
            std::wstring p = Trim(DlgText(d, IDC_SHAREPATH));
            if (p.empty()) p = std::wstring(docs) + L"\\" + SafeFileName(c->name) + L".wcal";
            if (OpenFileDlg(d, p, L"Shared calendar (*.wcal)\0*.wcal\0All files\0*.*\0", L"wcal", true)) SetText(d, IDC_SHAREPATH, p);
            break;
        }
        case IDC_ADD: {
            if (!c) break;
            std::wstring em = Trim(DlgText(d, IDC_EMAIL));
            if (em.find(L'@') == std::wstring::npos) { MessageBoxW(d, L"Enter the member's e-mail address.", L"Sharing", MB_ICONWARNING); break; }
            int perm = (int)CbData(d, IDC_PERM, 0);
            bool found = false;
            for (auto& m : c->members) if (Lower(m.email) == Lower(em)) { m.perm = perm; found = true; }
            if (!found) c->members.push_back({ em, perm });
            if (c->owner.empty()) c->owner = g.set.userEmail;
            SetText(d, IDC_EMAIL, L"");
            if (!c->sharedPath.empty()) App_ModelChanged();
            ShareShowMembers(d); break;
        }
        case IDC_REMOVE: {
            int i = LvSel(lv); if (!c || i < 0) break;
            c->members.erase(c->members.begin() + LvData(lv, i));
            if (!c->sharedPath.empty()) App_ModelChanged();
            ShareShowMembers(d); break;
        }
        case IDC_PUBLISH: {
            if (!c) break;
            std::wstring p = Trim(DlgText(d, IDC_SHAREPATH));
            if (p.empty()) { SendMessageW(d, WM_COMMAND, IDC_BROWSE, 0); p = Trim(DlgText(d, IDC_SHAREPATH)); if (p.empty()) break; }
            for (auto& k : g.calendars) if (k.id != c->id && _wcsicmp(k.sharedPath.c_str(), p.c_str()) == 0) { MessageBoxW(d, L"Another calendar already uses this file.", L"Sharing", MB_ICONWARNING); return TRUE; }
            c->sharedPath = p; c->owner = g.set.userEmail;
            App_ModelChanged();   // SaveModel writes the shared file
            if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) { MessageBoxW(d, (L"Could not write " + p).c_str(), L"Sharing", MB_ICONERROR); break; }
            ShareShowMembers(d);
            if (MessageBoxW(d, L"The calendar is now shared through this file.\n\nE-mail the members an invitation with the file location now?", L"Sharing", MB_YESNO | MB_ICONINFORMATION) == IDYES)
                SendMessageW(d, WM_COMMAND, IDC_EMAILSHARE, 0);
            break;
        }
        case IDC_EMAILSHARE: {
            if (!c) break;
            if (c->members.empty()) { MessageBoxW(d, L"Add members first.", L"Sharing", MB_ICONINFORMATION); break; }
            std::vector<std::pair<std::wstring, std::wstring>> to;
            for (auto& m : c->members) to.push_back({ L"", m.email });
            std::vector<const Event*> evs; for (auto& e : g.events) if (e.calendarId == c->id) evs.push_back(&e);
            std::string ics = BuildIcs(evs, "PUBLISH");
            std::wstring body = g.set.userName + L" shared the calendar \"" + c->name + L"\" with you.\n\n";
            if (!c->sharedPath.empty()) body += L"WinCalendar users: Organize > Sharing > Open shared... and select\n" + c->sharedPath + L"\n\n";
            body += L"Other calendar apps: open the attached snapshot (.ics) to import the current events.";
            SendMail(to, L"Shared calendar: " + c->name, body, &ics, SafeFileName(c->name) + L".ics", true, d);
            break;
        }
        case IDC_STOPSHARE: {
            if (!c || c->sharedPath.empty()) break;
            bool mine = c->owner.empty() || Lower(c->owner) == Lower(g.set.userEmail);
            if (MessageBoxW(d, mine ? L"Stop sharing this calendar? Its events are kept in your own calendar; the shared file is left untouched."
                                    : L"Unsubscribe from this shared calendar? A local copy of its events is kept.", L"Sharing", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            c->sharedPath.clear(); c->members.clear(); c->owner.clear();
            App_ModelChanged(); ShareShowMembers(d); break;
        }
        case IDC_OPENSHARED: {
            std::wstring p;
            if (!OpenFileDlg(d, p, L"Shared calendar (*.wcal)\0*.wcal\0All files\0*.*\0", L"wcal", false)) break;
            for (auto& k : g.calendars) if (_wcsicmp(k.sharedPath.c_str(), p.c_str()) == 0) { MessageBoxW(d, L"You are already subscribed to this calendar.", L"Sharing", MB_ICONINFORMATION); return TRUE; }
            Calendar nc; nc.id = NewId(); nc.sharedPath = p; nc.color = RGB(135, 100, 184);
            g.calendars.push_back(nc);
            Calendar& ref = g.calendars.back();
            if (!LoadSharedCalendarFile(ref)) {
                g.calendars.pop_back();
                MessageBoxW(d, L"This file is not a WinCalendar shared calendar.", L"Sharing", MB_ICONERROR); break;
            }
            if (ref.name.empty()) ref.name = L"Shared calendar";
            std::wstring me = Lower(g.set.userEmail); bool member = Lower(ref.owner) == me;
            for (auto& m : ref.members) if (Lower(m.email) == me) member = true;
            if (!member && !ref.members.empty())
                MessageBoxW(d, (L"You (" + g.set.userEmail + L") are not a member of this calendar. It is shown read-only.").c_str(), L"Sharing", MB_ICONINFORMATION);
            s_shareCal = ref.id;
            App_ModelChanged(); FillShareCombo(d); break;
        }
        }
        return TRUE;
    }
    }
    return FALSE;
}

// ================================================================== Categories
static COLORREF s_catColor = RGB(0, 120, 215);
static void FillCategories(HWND d, int sel) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    ListView_DeleteAllItems(lv);
    std::vector<COLORREF> cols; for (auto& c : g.categories) cols.push_back(c.color);
    SetImages(lv, cols);
    for (size_t i = 0; i < g.categories.size(); i++) {
        int n = 0; for (auto& e : g.events) if (e.category == g.categories[i].name) n++;
        LvAdd(lv, { g.categories[i].name, std::to_wstring(n) }, (LPARAM)i, (int)i);
    }
    LvSelect(lv, sel);
}
static INT_PTR CALLBACK CategoriesProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    auto sel = [&]() { int i = LvSel(lv); return i < 0 ? -1 : (int)LvData(lv, i); };
    switch (msg) {
    case WM_INITDIALOG:
        LvCols(lv, { { L"Category", 250 }, { L"Events", 80 } });
        FillCategories(d, 0);
        return TRUE;
    case WM_DRAWITEM: if (wp == IDC_COLORSWATCH) { DrawSwatch(lp, s_catColor); return TRUE; } break;
    case WM_NOTIFY:
        if (IsSelChange(lp, IDC_LIST)) {
            int i = sel();
            if (i >= 0) { SetText(d, IDC_NAME, g.categories[i].name); s_catColor = g.categories[i].color; InvalidateRect(GetDlgItem(d, IDC_COLORSWATCH), nullptr, TRUE); }
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_COLOR: case IDC_COLORSWATCH:
            if (PickColor(d, s_catColor)) {
                InvalidateRect(GetDlgItem(d, IDC_COLORSWATCH), nullptr, TRUE);
                int i = sel(); if (i >= 0) { g.categories[i].color = s_catColor; App_ModelChanged(); FillCategories(d, i); }
            }
            break;
        case IDC_NEW: {
            std::wstring name = Trim(DlgText(d, IDC_NAME));
            if (name.empty()) { MessageBoxW(d, L"Type a name for the category.", L"Categories", MB_ICONINFORMATION); SetFocus(GetDlgItem(d, IDC_NAME)); break; }
            if (FindCategory(name)) { MessageBoxW(d, L"A category with this name already exists.", L"Categories", MB_ICONWARNING); break; }
            g.categories.push_back({ name, s_catColor }); App_ModelChanged(); FillCategories(d, (int)g.categories.size() - 1); break;
        }
        case IDC_RENAME: {
            int i = sel(); std::wstring name = Trim(DlgText(d, IDC_NAME));
            if (i < 0 || name.empty() || name == g.categories[i].name) break;
            if (FindCategory(name)) { MessageBoxW(d, L"A category with this name already exists.", L"Categories", MB_ICONWARNING); break; }
            std::wstring old = g.categories[i].name;
            for (auto& e : g.events) if (e.category == old && !EventReadOnly(e)) e.category = name;
            g.categories[i].name = name; App_ModelChanged(); FillCategories(d, i); break;
        }
        case IDC_DELETE: {
            int i = sel(); if (i < 0) break;
            std::wstring old = g.categories[i].name;
            if (MessageBoxW(d, (L"Delete the category \"" + old + L"\"? Events keep their calendar color.").c_str(), L"Categories", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            for (auto& e : g.events) if (e.category == old && !EventReadOnly(e)) e.category.clear();
            g.categories.erase(g.categories.begin() + i); App_ModelChanged();
            FillCategories(d, std::min(i, (int)g.categories.size() - 1)); break;
        }
        }
        return TRUE;
    }
    return FALSE;
}

// ================================================================== Accounts
static void AccHint(HWND d) {
    static const wchar_t* h[PROV_COUNT] = {
        L"Local: any iCalendar URL or file path.",
        L"Google Workspace / Gmail: Google Calendar > Settings > (your calendar) > Integrate calendar > 'Secret address in iCal format'. Paste the https://calendar.google.com/calendar/ical/.../basic.ics URL.",
        L"iCloud: Calendar app > calendar > Share Calendar > Public Calendar. Copy the webcal://p..-caldav.icloud.com/... link (webcal:// is accepted).",
        L"Microsoft Exchange / Microsoft 365: Outlook on the web > Settings > Calendar > Shared calendars > Publish a calendar > copy the ICS link (https://outlook.office365.com/owa/calendar/.../calendar.ics).",
        L"Outlook.com: Outlook.com > Settings > Calendar > Shared calendars > Publish a calendar > copy the ICS link.",
        L"Other: any CalDAV server or website that publishes an iCalendar (.ics) feed. webcal:// and https:// URLs and local .ics paths work." };
    SetText(d, IDC_HINT, h[std::clamp((int)CbData(d, IDC_PROVIDER, PROV_OTHER), 0, PROV_COUNT - 1)] +
        std::wstring(L"\r\nSynced events appear in a read-only calendar and are merged into all views."));
}
static void FillAccounts(HWND d, int selId) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    ListView_DeleteAllItems(lv);
    for (auto& a : g.accounts) {
        std::wstring last = a.lastSync ? FmtDate(UtcToZone(a.lastSync, DisplayTzKey())) + L" " + FmtTime(UtcToZone(a.lastSync, DisplayTzKey())) : L"never";
        int i = LvAdd(lv, { a.name, kProviderNames[a.provider], a.email, last, a.status }, a.id);
        if (a.id == selId) LvSelect(lv, i);
    }
}
static bool ReadAccount(HWND d, Account& a) {
    std::wstring url = Trim(DlgText(d, IDC_URL));
    if (url.empty()) { MessageBoxW(d, L"Paste the calendar's iCal (ICS) address. See the hint below for where to find it.", L"Accounts", MB_ICONWARNING); return false; }
    a.provider = (int)CbData(d, IDC_PROVIDER, PROV_OTHER);
    a.name = Trim(DlgText(d, IDC_NAME)); if (a.name.empty()) a.name = kProviderNames[a.provider];
    a.email = Trim(DlgText(d, IDC_EMAIL)); a.url = url; a.autoSync = IsChecked(d, IDC_AUTOSYNC);
    return true;
}
static void DoSync(HWND d, int id) {
    Account* a = FindAccount(id); if (!a) return;
    HCURSOR old = SetCursor(LoadCursor(nullptr, IDC_WAIT));
    std::wstring msg; bool ok = SyncAccount(*a, msg);
    SetCursor(old);
    if (Calendar* c = FindCalendar(a->calendarId)) if (c->name != a->name && !a->name.empty()) c->name = a->name;
    App_ModelChanged(); FillAccounts(d, id);
    if (!ok) MessageBoxW(d, (L"Sync failed for " + a->name + L":\n" + msg).c_str(), L"Accounts", MB_ICONWARNING);
}
static INT_PTR CALLBACK AccountsProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    switch (msg) {
    case WM_INITDIALOG:
        LvCols(lv, { { L"Name", 100 }, { L"Provider", 110 }, { L"E-mail", 120 }, { L"Last sync", 110 }, { L"Status", 140 } });
        for (int i = 1; i < PROV_COUNT; i++) CbAdd(d, IDC_PROVIDER, kProviderNames[i], i);
        CbSelData(d, IDC_PROVIDER, PROV_GOOGLE); Check(d, IDC_AUTOSYNC, true);
        SendDlgItemMessageW(d, IDC_URL, EM_SETCUEBANNER, TRUE, (LPARAM)L"https://...  or  webcal://...  (secret iCal address)");
        FillAccounts(d, g.accounts.empty() ? 0 : g.accounts[0].id);
        AccHint(d);
        return TRUE;
    case WM_NOTIFY:
        if (IsSelChange(lp, IDC_LIST)) if (Account* a = FindAccount((int)LvData(lv, LvSel(lv)))) {
            SetText(d, IDC_NAME, a->name); CbSelData(d, IDC_PROVIDER, a->provider); SetText(d, IDC_EMAIL, a->email);
            SetText(d, IDC_URL, a->url); Check(d, IDC_AUTOSYNC, a->autoSync); AccHint(d);
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_PROVIDER: if (HIWORD(wp) == CBN_SELCHANGE) AccHint(d); break;
        case IDC_ADD: {
            Account a; a.id = NewId();
            if (!ReadAccount(d, a)) break;
            g.accounts.push_back(a);
            DoSync(d, a.id); break;
        }
        case IDC_UPDATE: {
            Account* a = FindAccount((int)LvData(lv, LvSel(lv))); if (!a) break;
            Account t = *a; if (!ReadAccount(d, t)) break;
            bool urlChanged = t.url != a->url; *a = t;
            if (urlChanged) DoSync(d, a->id); else { App_ModelChanged(); FillAccounts(d, a->id); }
            break;
        }
        case IDC_REMOVE: {
            Account* a = FindAccount((int)LvData(lv, LvSel(lv))); if (!a) break;
            if (MessageBoxW(d, (L"Remove the account \"" + a->name + L"\" and its synced events?").c_str(), L"Accounts", MB_YESNO | MB_ICONQUESTION) != IDYES) break;
            int aid = a->id, cid = a->calendarId;
            g.events.erase(std::remove_if(g.events.begin(), g.events.end(), [&](const Event& e) { return e.calendarId == cid; }), g.events.end());
            g.calendars.erase(std::remove_if(g.calendars.begin(), g.calendars.end(), [&](const Calendar& c) { return c.id == cid; }), g.calendars.end());
            g.accounts.erase(std::remove_if(g.accounts.begin(), g.accounts.end(), [&](const Account& x) { return x.id == aid; }), g.accounts.end());
            App_ModelChanged(); FillAccounts(d, 0); break;
        }
        case IDC_SYNCNOW: {
            int id = (int)LvData(lv, LvSel(lv));
            if (id) DoSync(d, id); else { App_SyncAll(d, true); FillAccounts(d, 0); }
            break;
        }
        }
        return TRUE;
    }
    return FALSE;
}

void OrganizerDialog(HWND owner, int startPage) {
    PropSheet(owner, L"Organize Calendars", {
        { IDD_ORG_CALENDARS, CalendarsProc }, { IDD_ORG_SHARING, SharingProc },
        { IDD_ORG_CATEGORIES, CategoriesProc }, { IDD_ORG_ACCOUNTS, AccountsProc } }, 0, startPage, true);
    App_ModelChanged();
}
