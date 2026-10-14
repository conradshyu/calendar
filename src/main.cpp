// WinCalendar - main window, sidebar, tray notifications and message loop
#include "app.h"
#include "dlgutil.h"
#include <objbase.h>
#include <uxtheme.h>
#pragma comment(lib, "uxtheme.lib")

HINSTANCE g_hinst = nullptr;
HWND g_hwnd = nullptr;
int g_dpi = 96;

std::wstring ViewTitle();
bool Ribbon_Init(HWND);
void Ribbon_Destroy();
int  Ribbon_Height();
void Ribbon_Invalidate();
void OnCommand(HWND hwnd, int id);
void ShowContextMenu(HWND owner, POINT pt);
void CheckReminders();
void AutoSyncTick(HWND owner);
void App_NewEventAt(HWND owner, MIN_T start, bool allDay);

#define WM_APP_REFRESHTASKS (WM_APP + 20)
static const wchar_t* kMainClass = L"WinCalendarMain";
static HWND s_status, s_search, s_month, s_calList, s_taskList, s_lblCals, s_lblTasks, s_addTask, s_view;
static HFONT s_font, s_bold;
static HIMAGELIST s_calImages;
static bool s_filling;
static NOTIFYICONDATAW s_nid;
static UINT s_taskbarCreated;
static MONTHDAYSTATE s_dayState[24];
static int s_today;

// ---------------------------------------------------------------- small services
HWND Main_TaskList() { return s_taskList; }
HWND Main_CalList() { return s_calList; }
std::wstring Main_SearchText() { return Trim(DlgText(g_hwnd, IDC_QSEARCH)); }
void Main_FocusSearch() { SetFocus(s_search); SendMessageW(s_search, EM_SETSEL, 0, -1); }
void App_SetStatus(const std::wstring& s) { if (s_status) SendMessageW(s_status, SB_SETTEXTW, 0, (LPARAM)s.c_str()); }
void App_Notify(const std::wstring& title, const std::wstring& text) {
    NOTIFYICONDATAW n = s_nid; n.uFlags = NIF_INFO;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, text.empty() ? L" " : text.c_str(), _TRUNCATE);
    n.dwInfoFlags = NIIF_INFO | NIIF_RESPECT_QUIET_TIME;
    Shell_NotifyIconW(NIM_MODIFY, &n);
    App_SetStatus(title + (text.empty() ? L"" : L" - " + text));
}

void Main_UpdateTitle() {
    SetWindowTextW(g_hwnd, (ViewTitle() + L" - WinCalendar").c_str());
    std::wstring tz = L"Time zone: " + TzDisplayName(DisplayTzKey()) + (g.set.autoTz ? L" (automatic)" : L" (locked)");
    SendMessageW(s_status, SB_SETTEXTW, 1, (LPARAM)tz.c_str());
}
void Main_SyncMonthCal() {
    SYSTEMTIME st = MinToSt((MIN_T)g_curDay * DAYMIN);
    MonthCal_SetCurSel(s_month, &st);
}

// ---------------------------------------------------------------- mini calendar bold days
static void ComputeDayState(const SYSTEMTIME& first, int count, MONTHDAYSTATE* out) {
    count = std::min(count, 24);
    for (int i = 0; i < count; ++i) out[i] = 0;
    int y0 = first.wYear, m0 = first.wMonth;
    int from = MakeDay(y0, m0, 1), to = AddMonths(from, count);
    auto occs = Expand((MIN_T)from * DAYMIN, (MIN_T)to * DAYMIN, true);
    for (auto& o : occs) {
        int d0 = DayOf(o.start), d1 = DayOf(std::max(o.start, o.end - 1));
        for (int d = std::max(d0, from); d <= std::min(d1, to - 1) && d - d0 < 62; ++d) {
            int y, m, dd; DayYMD(d, y, m, dd);
            int idx = (y * 12 + m) - (y0 * 12 + m0);
            if (idx >= 0 && idx < count) out[idx] |= 1u << (dd - 1);
        }
    }
}
static void UpdateMonthBold() {
    SYSTEMTIME r[2]{};
    int n = MonthCal_GetMonthRange(s_month, GMR_DAYSTATE, r);
    if (n <= 0) return;
    n = std::min(n, 24);
    ComputeDayState(r[0], n, s_dayState);
    MonthCal_SetDayState(s_month, n, s_dayState);
}

// ---------------------------------------------------------------- sidebar lists
static void RefreshCalendars() {
    int selId = 0, si = ListView_GetNextItem(s_calList, -1, LVNI_SELECTED);
    if (si >= 0) { LVITEMW it{}; it.mask = LVIF_PARAM; it.iItem = si; ListView_GetItem(s_calList, &it); selId = (int)it.lParam; }
    s_filling = true;
    ListView_DeleteAllItems(s_calList);
    std::vector<COLORREF> cols; for (auto& c : g.calendars) cols.push_back(c.color);
    HIMAGELIST il = MakeColorImageList(cols, S(14));
    ListView_SetImageList(s_calList, il, LVSIL_SMALL);
    if (s_calImages) ImageList_Destroy(s_calImages);
    s_calImages = il;
    for (size_t i = 0; i < g.calendars.size(); ++i) {
        Calendar& c = g.calendars[i];
        std::wstring t = c.name;
        if (c.accountId) t += L"  (synced)";
        else if (!c.sharedPath.empty()) t += CalendarReadOnly(&c) ? L"  (shared, view)" : L"  (shared)";
        if (c.id == DefaultCalendarId()) t += L"  \u2605";
        LVITEMW it{}; it.mask = LVIF_TEXT | LVIF_PARAM | LVIF_IMAGE; it.iItem = (int)i; it.pszText = (LPWSTR)t.c_str(); it.iImage = (int)i; it.lParam = c.id;
        int idx = ListView_InsertItem(s_calList, &it);
        ListView_SetCheckState(s_calList, idx, c.visible);
        if (c.id == selId) ListView_SetItemState(s_calList, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    s_filling = false;
}

static std::wstring DueText(const Task& t) {
    if (!t.hasDue) return L"";
    int today = TodayDisplay();
    if (t.dueDay == today) return L"Today";
    if (t.dueDay == today + 1) return L"Tomorrow";
    if (t.dueDay == today - 1) return L"Yesterday";
    return FmtDate((MIN_T)t.dueDay * DAYMIN, L"MMM d");
}
void RefreshTasks() {
    int selId = 0, si = ListView_GetNextItem(s_taskList, -1, LVNI_SELECTED);
    if (si >= 0) { LVITEMW it{}; it.mask = LVIF_PARAM; it.iItem = si; ListView_GetItem(s_taskList, &it); selId = (int)it.lParam; }
    std::vector<const Task*> v; int open = 0;
    for (auto& t : g.tasks) { if (!t.done) open++; if (!t.done || g.set.showCompleted) v.push_back(&t); }
    std::stable_sort(v.begin(), v.end(), [](const Task* a, const Task* b) {
        if (a->done != b->done) return !a->done;
        if (a->hasDue != b->hasDue) return a->hasDue;
        if (a->hasDue && a->dueDay != b->dueDay) return a->dueDay < b->dueDay;
        return a->priority > b->priority; });
    s_filling = true;
    SendMessageW(s_taskList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(s_taskList);
    for (size_t i = 0; i < v.size(); ++i) {
        std::wstring t = (v[i]->priority == 2 ? L"! " : L"") + v[i]->title;
        LVITEMW it{}; it.mask = LVIF_TEXT | LVIF_PARAM; it.iItem = (int)i; it.pszText = (LPWSTR)t.c_str(); it.lParam = v[i]->id;
        int idx = ListView_InsertItem(s_taskList, &it);
        std::wstring due = DueText(*v[i]);
        ListView_SetItemText(s_taskList, idx, 1, (LPWSTR)due.c_str());
        ListView_SetCheckState(s_taskList, idx, v[i]->done);
        if (v[i]->id == selId) ListView_SetItemState(s_taskList, idx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    SendMessageW(s_taskList, WM_SETREDRAW, TRUE, 0);
    s_filling = false;
    std::wstring lbl = L"Tasks  (" + std::to_wstring(open) + L" open)";
    SetWindowTextW(s_lblTasks, lbl.c_str());
}
static int SelectedTaskId() {
    int i = ListView_GetNextItem(s_taskList, -1, LVNI_SELECTED); if (i < 0) return 0;
    LVITEMW it{}; it.mask = LVIF_PARAM; it.iItem = i; ListView_GetItem(s_taskList, &it); return (int)it.lParam;
}
void Main_EditSelectedTask() {
    Task* t = FindTask(SelectedTaskId()); if (!t) return;
    Task copy = *t;
    if (EditTaskDlg(g_hwnd, copy, false)) { if (Task* t2 = FindTask(copy.id)) *t2 = copy; App_ModelChanged(); }
}

void RefreshUI() {
    if (g_selEvent && !FindEvent(g_selEvent)) { g_selEvent = 0; g_selWall = 0; }
    MonthCal_SetFirstDayOfWeek(s_month, (g.set.weekStart + 6) % 7);
    RefreshCalendars(); RefreshTasks();
    View_Refresh(); UpdateMonthBold(); Main_UpdateTitle(); Ribbon_Invalidate();
}
void App_ModelChanged() { SaveModel(); if (g_hwnd) RefreshUI(); }

void App_GoToDay(int day, int view) {
    g_curDay = day;
    if (view >= 0 && view != g_viewMode) View_SetMode(view); else View_Refresh();
    Main_SyncMonthCal(); Main_UpdateTitle(); Ribbon_Invalidate();
}

// ---------------------------------------------------------------- layout
static void Layout(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    SendMessageW(s_status, WM_SIZE, 0, 0);
    RECT sr; GetWindowRect(s_status, &sr);
    int sbH = sr.bottom - sr.top;
    int parts[2] = { std::max(100, (int)rc.right - S(380)), -1 };
    SendMessageW(s_status, SB_SETPARTS, 2, (LPARAM)parts);
    int top = Ribbon_Height(), bottom = rc.bottom - sbH;
    RECT mr{}; MonthCal_GetMinReqRect(s_month, &mr);
    int sideW = std::max(S(250), (int)(mr.right - mr.left) + S(16));
    int x = S(8), w = sideW - S(16), y = top + S(8);
    HDWP dw = BeginDeferWindowPos(9);
    dw = DeferWindowPos(dw, s_search, nullptr, x, y, w, S(24), SWP_NOZORDER); y += S(32);
    int mcW = mr.right - mr.left, mcH = mr.bottom - mr.top;
    dw = DeferWindowPos(dw, s_month, nullptr, x + std::max(0, (w - mcW) / 2), y, mcW, mcH, SWP_NOZORDER); y += mcH + S(8);
    int lblH = S(20), btnH = S(26);
    int avail = std::max(0, bottom - S(8) - y - 2 * lblH - btnH - S(12));
    int calH = std::max(S(48), avail * 2 / 5), taskH = std::max(S(48), avail - calH);
    dw = DeferWindowPos(dw, s_lblCals, nullptr, x, y, w, lblH, SWP_NOZORDER); y += lblH;
    dw = DeferWindowPos(dw, s_calList, nullptr, x, y, w, calH, SWP_NOZORDER); y += calH + S(6);
    dw = DeferWindowPos(dw, s_lblTasks, nullptr, x, y, w, lblH, SWP_NOZORDER); y += lblH;
    dw = DeferWindowPos(dw, s_taskList, nullptr, x, y, w, taskH, SWP_NOZORDER); y += taskH + S(6);
    dw = DeferWindowPos(dw, s_addTask, nullptr, x, y, w, btnH, SWP_NOZORDER);
    dw = DeferWindowPos(dw, s_view, nullptr, sideW, top, std::max(0, (int)rc.right - sideW), std::max(0, bottom - top), SWP_NOZORDER);
    EndDeferWindowPos(dw);
    int sb = GetSystemMetrics(SM_CXVSCROLL);
    ListView_SetColumnWidth(s_calList, 0, w - sb - S(4));
    ListView_SetColumnWidth(s_taskList, 1, S(72));
    ListView_SetColumnWidth(s_taskList, 0, w - S(72) - sb - S(4));
    InvalidateRect(hwnd, nullptr, TRUE);
}
void App_RibbonResized(int) { if (g_hwnd && s_view) Layout(g_hwnd); }

// ---------------------------------------------------------------- quick search box
static LRESULT CALLBACK SearchSubclass(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) { OnCommand(g_hwnd, IDM_SEARCH); return 0; }
    if (msg == WM_KEYDOWN && wp == VK_ESCAPE) { SetWindowTextW(h, L""); SetFocus(s_view); return 0; }
    if (msg == WM_CHAR && (wp == L'\r' || wp == 27)) return 0;
    return DefSubclassProc(h, msg, wp, lp);
}

// ---------------------------------------------------------------- creation
static HWND MakeList(HWND hwnd, int id, int cols) {
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_NOCOLUMNHEADER | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
        0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)id, g_hinst, nullptr);
    ListView_SetExtendedListViewStyle(h, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
    for (int i = 0; i < cols; ++i) { LVCOLUMNW c{}; c.mask = LVCF_WIDTH | LVCF_FMT; c.cx = 100; c.fmt = i ? LVCFMT_RIGHT : LVCFMT_LEFT; ListView_InsertColumn(h, i, &c); }
    SendMessageW(h, WM_SETFONT, (WPARAM)s_font, 0);
    return h;
}

static void CreateChildren(HWND hwnd) {
    NONCLIENTMETRICSW ncm{ sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    s_font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW lf = ncm.lfMessageFont; lf.lfWeight = FW_SEMIBOLD; s_bold = CreateFontIndirectW(&lf);

    s_status = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP, 0, 0, 0, 0, hwnd, (HMENU)IDC_STATUS, g_hinst, nullptr);
    s_search = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 10, 10, hwnd, (HMENU)IDC_QSEARCH, g_hinst, nullptr);
    SendMessageW(s_search, WM_SETFONT, (WPARAM)s_font, 0);
    SendMessageW(s_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search events, people, places (Ctrl+E)");
    SetWindowSubclass(s_search, SearchSubclass, 1, 0);
    s_month = CreateWindowExW(0, MONTHCAL_CLASSW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | MCS_DAYSTATE | MCS_NOTODAYCIRCLE,
        0, 0, 10, 10, hwnd, (HMENU)IDC_MONTHCAL, g_hinst, nullptr);
    MonthCal_SetFirstDayOfWeek(s_month, (g.set.weekStart + 6) % 7);
    s_lblCals = CreateWindowExW(0, WC_STATICW, L"Calendars", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 0, 0, 10, 10, hwnd, (HMENU)IDC_LBL_CALS, g_hinst, nullptr);
    s_lblTasks = CreateWindowExW(0, WC_STATICW, L"Tasks", WS_CHILD | WS_VISIBLE | SS_CENTERIMAGE, 0, 0, 10, 10, hwnd, (HMENU)IDC_LBL_TASKS, g_hinst, nullptr);
    SendMessageW(s_lblCals, WM_SETFONT, (WPARAM)s_bold, 0);
    SendMessageW(s_lblTasks, WM_SETFONT, (WPARAM)s_bold, 0);
    s_calList = MakeList(hwnd, IDC_CALLIST, 1);
    s_taskList = MakeList(hwnd, IDC_TASKLIST, 2);
    s_addTask = CreateWindowExW(0, WC_BUTTONW, L"+ &Add task", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 10, 10, hwnd, (HMENU)IDC_ADDTASK, g_hinst, nullptr);
    SendMessageW(s_addTask, WM_SETFONT, (WPARAM)s_font, 0);
    View_CreateFonts();
    s_view = CreateCalView(hwnd);
}

static void AddTrayIcon(HWND hwnd) {
    s_nid = NOTIFYICONDATAW{}; s_nid.cbSize = sizeof(s_nid); s_nid.hWnd = hwnd; s_nid.uID = 1;
    s_nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE; s_nid.uCallbackMessage = WM_APP_TRAY;
    s_nid.hIcon = (HICON)LoadImageW(g_hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wcscpy_s(s_nid.szTip, L"WinCalendar");
    Shell_NotifyIconW(NIM_ADD, &s_nid);
}

static void RestoreMain(HWND hwnd) {
    if (IsIconic(hwnd)) ShowWindow(hwnd, SW_RESTORE);
    ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd);
}

static void Tick(HWND hwnd) {
    CheckReminders();
    Reminders_Tick();
    if (ReloadSharedCalendars(false)) { RefreshUI(); App_SetStatus(L"A shared calendar was updated by another member."); }
    int t = TodayDisplay();
    if (t != s_today) { s_today = t; RefreshUI(); }
    AutoSyncTick(hwnd);
}

// ---------------------------------------------------------------- notifications from child controls
static LRESULT OnNotify(HWND hwnd, NMHDR* nh) {
    if (nh->idFrom == IDC_MONTHCAL) {
        if (nh->code == MCN_SELECT) {
            auto* s = (NMSELCHANGE*)nh;
            App_GoToDay(DayOf(StToMin(s->stSelStart)), -1);
        } else if (nh->code == MCN_GETDAYSTATE) {
            auto* d = (NMDAYSTATE*)nh;
            int n = std::min(d->cDayState, 24);
            ComputeDayState(d->stStart, n, s_dayState);
            d->prgDayState = s_dayState;
        }
        return 0;
    }
    if (nh->idFrom == IDC_CALLIST) {
        if (nh->code == LVN_ITEMCHANGED && !s_filling) {
            auto* lv = (NMLISTVIEW*)nh;
            if ((lv->uChanged & LVIF_STATE) && ((lv->uOldState ^ lv->uNewState) & LVIS_STATEIMAGEMASK) && (lv->uOldState & LVIS_STATEIMAGEMASK)) {
                if (Calendar* c = FindCalendar((int)lv->lParam)) {
                    c->visible = ListView_GetCheckState(s_calList, lv->iItem) != 0;
                    SaveModel(); View_Refresh(); UpdateMonthBold();
                }
            }
        } else if (nh->code == NM_DBLCLK) { OnCommand(hwnd, IDM_CALENDARS); }
        else if (nh->code == NM_RCLICK) {
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, IDM_CALENDARS, L"&Manage calendars...");
            AppendMenuW(m, MF_STRING, IDM_NEWCALENDAR, L"&New calendar");
            AppendMenuW(m, MF_STRING, IDM_SHARING, L"&Sharing...");
            AppendMenuW(m, MF_STRING, IDM_OPENSHARED, L"&Open shared calendar...");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, IDM_ACCOUNTS, L"&Accounts...");
            AppendMenuW(m, MF_STRING, IDM_SYNC, L"S&ync now\tF5");
            POINT pt; GetCursorPos(&pt);
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(m);
            if (cmd) OnCommand(hwnd, cmd);
        }
        return 0;
    }
    if (nh->idFrom == IDC_TASKLIST) {
        if (nh->code == LVN_ITEMCHANGED && !s_filling) {
            auto* lv = (NMLISTVIEW*)nh;
            if ((lv->uChanged & LVIF_STATE) && ((lv->uOldState ^ lv->uNewState) & LVIS_STATEIMAGEMASK) && (lv->uOldState & LVIS_STATEIMAGEMASK)) {
                if (Task* t = FindTask((int)lv->lParam)) {
                    t->done = ListView_GetCheckState(s_taskList, lv->iItem) != 0;
                    SaveModel();
                    App_SetStatus(t->done ? L"Task completed: " + t->title : L"Task reopened: " + t->title);
                    PostMessageW(hwnd, WM_APP_REFRESHTASKS, 0, 0);
                }
            }
        } else if (nh->code == NM_DBLCLK) { Main_EditSelectedTask(); }
        else if (nh->code == LVN_KEYDOWN) {
            WORD k = ((NMLVKEYDOWN*)nh)->wVKey;
            if (k == VK_DELETE) OnCommand(hwnd, IDM_DELETE);
            else if (k == VK_RETURN || k == VK_F2) Main_EditSelectedTask();
        } else if (nh->code == NM_RCLICK) {
            HMENU m = CreatePopupMenu();
            bool has = SelectedTaskId() != 0;
            AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), 1, L"&Edit task...");
            AppendMenuW(m, MF_STRING | (has ? 0 : MF_GRAYED), 2, L"&Delete task");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, IDM_NEWTASK, L"&New task...");
            AppendMenuW(m, MF_STRING | (g.set.showCompleted ? MF_CHECKED : 0), IDM_SHOWCOMPLETED, L"&Show completed");
            AppendMenuW(m, MF_STRING, IDM_TASKS, L"&Manage tasks...");
            POINT pt; GetCursorPos(&pt);
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(m);
            if (cmd == 1) Main_EditSelectedTask();
            else if (cmd == 2) { SetFocus(s_taskList); OnCommand(hwnd, IDM_DELETE); }
            else if (cmd) OnCommand(hwnd, cmd);
        } else if (nh->code == NM_CUSTOMDRAW) {
            auto* cd = (NMLVCUSTOMDRAW*)nh;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                if (Task* t = FindTask((int)cd->nmcd.lItemlParam)) {
                    if (t->done) cd->clrText = GetSysColor(COLOR_GRAYTEXT);
                    else if (t->hasDue && t->dueDay < TodayDisplay()) cd->clrText = RGB(196, 38, 46);
                    else if (t->priority == 2) cd->clrText = RGB(202, 80, 16);
                }
                return CDRF_DODEFAULT;
            }
        }
        return 0;
    }
    return 0;
}

static LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == s_taskbarCreated && s_taskbarCreated) { AddTrayIcon(hwnd); return 0; }
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        CreateChildren(hwnd);
        if (!Ribbon_Init(hwnd)) MessageBoxW(hwnd, L"The Windows Ribbon Framework could not be initialized. Commands remain available through keyboard shortcuts and context menus.", L"WinCalendar", MB_ICONWARNING);
        AddTrayIcon(hwnd);
        s_today = TodayDisplay();
        View_SetMode(std::clamp(g.set.view, 0, 4));
        Main_SyncMonthCal();
        RefreshUI();
        View_ScrollToWork();
        App_SetStatus(L"Ready");
        SetTimer(hwnd, 1, 20000, nullptr);
        SetTimer(hwnd, 2, 2500, nullptr);
        return 0;
    case WM_SIZE: if (s_view) Layout(hwnd); return 0;
    case WM_GETMINMAXINFO: { auto* mm = (MINMAXINFO*)lp; mm->ptMinTrackSize = { S(820), S(600) }; return 0; }
    case WM_SETFOCUS: if (s_view) SetFocus(s_view); return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp; SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, (HWND)lp == s_lblCals || (HWND)lp == s_lblTasks ? RGB(0, 90, 158) : GetSysColor(COLOR_WINDOWTEXT));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }
    case WM_TIMER:
        if (wp == 2) KillTimer(hwnd, 2);
        Tick(hwnd);
        return 0;
    case WM_TIMECHANGE:
        RefreshTimeZoneCache(); RefreshUI(); return 0;
    case WM_SETTINGCHANGE:
        if (lp && (wcscmp((const wchar_t*)lp, L"intl") == 0)) { RefreshTimeZoneCache(); RefreshUI(); }
        break;
    case WM_NOTIFY: return OnNotify(hwnd, (NMHDR*)lp);
    case WM_COMMAND:
        if (lp) {   // notifications from child controls
            if (LOWORD(wp) == IDC_ADDTASK && HIWORD(wp) == BN_CLICKED) OnCommand(hwnd, IDM_NEWTASK);
            return 0;
        }
        OnCommand(hwnd, LOWORD(wp));
        return 0;
    case WM_APP_SELCHANGED:
        if (wp) Main_SyncMonthCal();
        Main_UpdateTitle(); Ribbon_Invalidate();
        if (Event* e = g_selEvent ? FindEvent(g_selEvent) : nullptr) {
            MIN_T st = EventToDisplay(*e, g_selWall ? g_selWall : e->start);
            std::wstring s = (e->title.empty() ? std::wstring(L"(no title)") : e->title) + L"  \u2022  " + FmtWhen(*e, st);
            if (!e->location.empty()) s += L"  \u2022  " + e->location;
            if (const Calendar* c = FindCalendar(e->calendarId)) s += L"  \u2022  " + c->name;
            if (e->myResponse >= 0) s += std::wstring(L"  \u2022  Your response: ") + kRsvpNames[e->myResponse];
            App_SetStatus(s);
        }
        return 0;
    case WM_APP_OPENSEL: if (g_selEvent) App_EditEvent(hwnd, g_selEvent, g_selWall, 0); return 0;
    case WM_APP_NEWAT: App_NewEventAt(hwnd, g_newAt, wp != 0); return 0;
    case WM_APP_CONTEXT: ShowContextMenu(hwnd, POINT{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) }); return 0;
    case WM_APP_GOTODAY: App_GoToDay((int)wp, (int)lp); return 0;
    case WM_APP_REFRESHTASKS: RefreshTasks(); return 0;
    case WM_APP_TRAY:
        switch (LOWORD(lp)) {
        case WM_LBUTTONDBLCLK: RestoreMain(hwnd); break;
        case NIN_BALLOONUSERCLICK: RestoreMain(hwnd); Reminders_Show(hwnd); break;
        case WM_RBUTTONUP: case WM_CONTEXTMENU: {
            HMENU m = CreatePopupMenu();
            AppendMenuW(m, MF_STRING, 1, L"&Open WinCalendar");
            AppendMenuW(m, MF_STRING, IDM_REMINDERS, L"&Reminders");
            AppendMenuW(m, MF_STRING, IDM_NEWEVENT, L"&New event...");
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, IDM_EXIT, L"E&xit");
            SetMenuDefaultItem(m, 1, FALSE);
            POINT pt; GetCursorPos(&pt); SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(m); PostMessageW(hwnd, WM_NULL, 0, 0);
            if (cmd == 1) RestoreMain(hwnd);
            else if (cmd) { if (cmd != IDM_EXIT) RestoreMain(hwnd); OnCommand(hwnd, cmd); }
            break;
        }
        }
        return 0;
    case WM_CLOSE:
        g.set.view = g_viewMode;
        SaveModel();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        Shell_NotifyIconW(NIM_DELETE, &s_nid);
        if (HWND rw = Reminders_Window()) DestroyWindow(rw);
        Ribbon_Destroy();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, LPWSTR, int show) {
    g_hinst = hinst;
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"WinCalendar.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND w = FindWindowW(kMainClass, nullptr)) { if (IsIconic(w)) ShowWindow(w, SW_RESTORE); SetForegroundWindow(w); }
        return 0;
    }
    if (FAILED(OleInitialize(nullptr))) return 1;
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES | ICC_DATE_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS | ICC_TAB_CLASSES | ICC_LINK_CLASS };
    InitCommonControlsEx(&icc);
    HDC dc = GetDC(nullptr); g_dpi = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(nullptr, dc);
    s_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    LoadModel();
    g_viewMode = std::clamp(g.set.view, 0, 4);
    g_curDay = TodayDisplay();

    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = MainProc; wc.hInstance = hinst; wc.lpszClassName = kMainClass;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hIcon = LoadIconW(hinst, MAKEINTRESOURCEW(IDI_APP));
    wc.hIconSm = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, kMainClass, L"WinCalendar", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, S(1320), S(860), nullptr, nullptr, hinst, nullptr);
    if (!hwnd) { OleUninitialize(); return 1; }
    ShowWindow(hwnd, show); UpdateWindow(hwnd);
    HACCEL accel = LoadAcceleratorsW(hinst, MAKEINTRESOURCEW(IDR_ACCEL));

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        HWND rw = Reminders_Window();
        if (rw && IsDialogMessageW(rw, &m)) continue;
        if (TranslateAcceleratorW(hwnd, accel, &m)) continue;
        TranslateMessage(&m); DispatchMessageW(&m);
    }
    OleUninitialize();
    if (mutex) { ReleaseMutex(mutex); CloseHandle(mutex); }
    return (int)m.wParam;
}
