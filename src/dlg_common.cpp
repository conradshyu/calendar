// WinCalendar - dialog helpers and small dialogs (Go to, Propose time, Tasks)
#include "dlgutil.h"
#include <prsht.h>

const int kReminderPresets[] = { 0, 5, 10, 15, 30, 60, 120, 240, 1440, 2880, 10080 };
const int kReminderPresetCount = (int)(sizeof(kReminderPresets) / sizeof(kReminderPresets[0]));

std::wstring DlgText(HWND d, int id) {
    HWND h = GetDlgItem(d, id); int n = GetWindowTextLengthW(h);
    std::wstring s(n, 0); if (n) GetWindowTextW(h, &s[0], n + 1); return s;
}
void SetText(HWND d, int id, const std::wstring& s) { SetDlgItemTextW(d, id, s.c_str()); }
int CbAdd(HWND d, int id, const std::wstring& s, LPARAM data) {
    int i = (int)SendDlgItemMessageW(d, id, CB_ADDSTRING, 0, (LPARAM)s.c_str());
    SendDlgItemMessageW(d, id, CB_SETITEMDATA, i, data); return i;
}
LPARAM CbData(HWND d, int id, LPARAM def) {
    int i = (int)SendDlgItemMessageW(d, id, CB_GETCURSEL, 0, 0);
    return i < 0 ? def : SendDlgItemMessageW(d, id, CB_GETITEMDATA, i, 0);
}
void CbSelData(HWND d, int id, LPARAM v) {
    int n = (int)SendDlgItemMessageW(d, id, CB_GETCOUNT, 0, 0);
    for (int i = 0; i < n; i++) if (SendDlgItemMessageW(d, id, CB_GETITEMDATA, i, 0) == v) { SendDlgItemMessageW(d, id, CB_SETCURSEL, i, 0); return; }
}
void CbReset(HWND d, int id) { SendDlgItemMessageW(d, id, CB_RESETCONTENT, 0, 0); }
void LvCols(HWND lv, std::initializer_list<std::pair<const wchar_t*, int>> cols, DWORD exStyle) {
    ListView_SetExtendedListViewStyle(lv, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP | exStyle);
    int i = 0;
    for (auto& c : cols) {
        LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH; col.pszText = (LPWSTR)c.first; col.cx = S(c.second);
        ListView_InsertColumn(lv, i++, &col);
    }
}
int LvAdd(HWND lv, const std::vector<std::wstring>& cells, LPARAM lp, int image) {
    LVITEMW it{}; it.mask = LVIF_TEXT | LVIF_PARAM | (image >= 0 ? LVIF_IMAGE : 0); it.iItem = ListView_GetItemCount(lv);
    it.pszText = (LPWSTR)(cells.empty() ? L"" : cells[0].c_str()); it.lParam = lp; it.iImage = image;
    int idx = ListView_InsertItem(lv, &it);
    for (size_t c = 1; c < cells.size(); c++) ListView_SetItemText(lv, idx, (int)c, (LPWSTR)cells[c].c_str());
    return idx;
}
int LvSel(HWND lv) { return ListView_GetNextItem(lv, -1, LVNI_SELECTED); }
LPARAM LvData(HWND lv, int item) {
    if (item < 0) return 0;
    LVITEMW it{}; it.mask = LVIF_PARAM; it.iItem = item; ListView_GetItem(lv, &it); return it.lParam;
}
void LvSelect(HWND lv, int item) {
    if (item < 0 || item >= ListView_GetItemCount(lv)) return;
    ListView_SetItemState(lv, item, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(lv, item, FALSE);
}
void SetDtp(HWND d, int id, MIN_T m) {
    HWND h = GetDlgItem(d, id);
    if ((GetWindowLongW(h, GWL_STYLE) & DTS_TIMEFORMAT) == DTS_TIMEFORMAT) {
        WCHAR fmt[80] = L"h:mm tt";
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SSHORTTIME, fmt, 80);
        DateTime_SetFormat(h, fmt);
    }
    SYSTEMTIME st = MinToSt(m); DateTime_SetSystemtime(h, GDT_VALID, &st);
}
MIN_T GetDtp(HWND d, int id) { SYSTEMTIME st{}; DateTime_GetSystemtime(GetDlgItem(d, id), &st); return StToMin(st); }
bool IsChecked(HWND d, int id) { return IsDlgButtonChecked(d, id) == BST_CHECKED; }
void Check(HWND d, int id, bool on) { CheckDlgButton(d, id, on ? BST_CHECKED : BST_UNCHECKED); }
void Enable(HWND d, int id, bool on) { EnableWindow(GetDlgItem(d, id), on); }

void FillTzCombo(HWND d, int id, const std::wstring& selKey) {
    CbReset(d, id);
    const auto& all = AllTimeZones();
    int sel = -1;
    for (size_t i = 0; i < all.size(); i++) {
        int k = CbAdd(d, id, all[i].display, (LPARAM)i);
        (void)k;
    }
    // CB_ADDSTRING on a non-sorted combo keeps order, so index == position
    for (size_t i = 0; i < all.size(); i++) if (_wcsicmp(all[i].key.c_str(), selKey.c_str()) == 0) sel = (int)i;
    SendDlgItemMessageW(d, id, CB_SETCURSEL, sel, 0);
}
std::wstring TzComboKey(HWND d, int id) {
    LPARAM i = CbData(d, id); const auto& all = AllTimeZones();
    return (i >= 0 && i < (LPARAM)all.size()) ? all[i].key : L"";
}
void FillReminderPresets(HWND d, int id, bool withNone) {
    CbReset(d, id);
    if (withNone) CbAdd(d, id, L"None", -1);
    for (int i = 0; i < kReminderPresetCount; i++) CbAdd(d, id, FmtReminder(kReminderPresets[i]), kReminderPresets[i]);
}
int ParseReminderText(const std::wstring& s) {
    for (int i = 0; i < kReminderPresetCount; i++) if (FmtReminder(kReminderPresets[i]) == s) return kReminderPresets[i];
    std::wstring l = Lower(Trim(s));
    if (l.empty() || !iswdigit(l[0])) return -1;
    int n = _wtoi(l.c_str());
    if (l.find(L'w') != std::wstring::npos) return n * 10080;
    if (l.find(L'd') != std::wstring::npos) return n * 1440;
    if (l.find(L'h') != std::wstring::npos) return n * 60;
    return n;
}
void SetPageData(HWND d, LPARAM lp) { SetWindowLongPtrW(d, DWLP_USER, ((PROPSHEETPAGEW*)lp)->lParam); }
void SetMsgResult(HWND d, LONG_PTR r) { SetWindowLongPtrW(d, DWLP_MSGRESULT, r); }
static BOOL CALLBACK DisableChild(HWND h, LPARAM) {
    wchar_t cls[32]; GetClassNameW(h, cls, 32);
    if (_wcsicmp(cls, L"Static") == 0) return TRUE;
    if (_wcsicmp(cls, L"Edit") == 0) { SendMessageW(h, EM_SETREADONLY, TRUE, 0); return TRUE; }
    if (_wcsicmp(cls, WC_LISTVIEWW) == 0) return TRUE;
    EnableWindow(h, FALSE); return TRUE;
}
void DisableEditing(HWND d) { EnumChildWindows(d, DisableChild, 0); }

bool OpenFileDlg(HWND owner, std::wstring& path, const wchar_t* filter, const wchar_t* ext, bool save) {
    wchar_t buf[MAX_PATH * 2] = {}; wcsncpy_s(buf, path.c_str(), _TRUNCATE);
    OPENFILENAMEW of{ sizeof(of) }; of.hwndOwner = owner; of.lpstrFilter = filter; of.lpstrFile = buf; of.nMaxFile = MAX_PATH * 2;
    of.lpstrDefExt = ext;
    of.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    BOOL ok = save ? GetSaveFileNameW(&of) : GetOpenFileNameW(&of);
    if (ok) path = buf;
    return ok != FALSE;
}
void CopyToClipboard(HWND owner, const std::wstring& s) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    if (h) { memcpy(GlobalLock(h), s.c_str(), (s.size() + 1) * sizeof(wchar_t)); GlobalUnlock(h); SetClipboardData(CF_UNICODETEXT, h); }
    CloseClipboard();
}

static bool s_closeOnly;
static int CALLBACK SheetCallback(HWND hwnd, UINT msg, LPARAM) {
    if (msg == PSCB_INITIALIZED && s_closeOnly) {
        ShowWindow(GetDlgItem(hwnd, IDCANCEL), SW_HIDE);
        SetDlgItemTextW(hwnd, IDOK, L"Close");
    }
    return 0;
}
int PropSheet(HWND owner, const wchar_t* caption, std::initializer_list<std::pair<int, DLGPROC>> pages, LPARAM data, int startPage, bool closeOnly) {
    std::vector<PROPSHEETPAGEW> ps;
    for (auto& p : pages) {
        PROPSHEETPAGEW pg{ sizeof(pg) }; pg.hInstance = g_hinst; pg.pszTemplate = MAKEINTRESOURCEW(p.first);
        pg.pfnDlgProc = p.second; pg.lParam = data; ps.push_back(pg);
    }
    PROPSHEETHEADERW h{ sizeof(h) };
    h.dwFlags = PSH_PROPSHEETPAGE | PSH_NOAPPLYNOW | PSH_USEICONID | PSH_NOCONTEXTHELP | PSH_USECALLBACK;
    h.hwndParent = owner; h.hInstance = g_hinst; h.pszIcon = MAKEINTRESOURCEW(IDI_APP); h.pszCaption = caption;
    h.nPages = (UINT)ps.size(); h.nStartPage = (UINT)std::clamp(startPage, 0, (int)ps.size() - 1); h.ppsp = ps.data();
    h.pfnCallback = SheetCallback;
    bool prev = s_closeOnly; s_closeOnly = closeOnly;
    INT_PTR r = PropertySheetW(&h);
    s_closeOnly = prev;
    return (int)r;
}

// ------------------------------------------------------------------ misc shared
bool PickColor(HWND owner, COLORREF& c) {
    static COLORREF custom[16] = { RGB(0,120,215), RGB(16,137,62), RGB(202,80,16), RGB(209,52,56), RGB(136,23,152), RGB(0,153,188),
                                   RGB(255,185,0), RGB(96,94,92), RGB(135,100,184), RGB(227,0,140), RGB(0,183,195), RGB(73,130,5),
                                   RGB(142,86,46), RGB(16,124,16), RGB(232,17,35), RGB(0,99,177) };
    CHOOSECOLORW cc{ sizeof(cc) }; cc.hwndOwner = owner; cc.rgbResult = c; cc.lpCustColors = custom; cc.Flags = CC_FULLOPEN | CC_RGBINIT;
    if (!ChooseColorW(&cc)) return false;
    c = cc.rgbResult; return true;
}
HIMAGELIST MakeColorImageList(const std::vector<COLORREF>& colors, int size) {
    HIMAGELIST il = ImageList_Create(size, size, ILC_COLOR32, (int)colors.size() + 1, 4);
    HDC sdc = GetDC(nullptr); HDC dc = CreateCompatibleDC(sdc);
    for (COLORREF c : colors) {
        HBITMAP bmp = CreateCompatibleBitmap(sdc, size, size);
        HGDIOBJ old = SelectObject(dc, bmp);
        RECT r{ 0, 0, size, size };
        FillRect(dc, &r, GetSysColorBrush(COLOR_WINDOW));
        int m = std::max(1, size / 8); RECT in{ m, m, size - m, size - m };
        SetDCBrushColor(dc, c); FillRect(dc, &in, (HBRUSH)GetStockObject(DC_BRUSH));
        SelectObject(dc, old);
        ImageList_Add(il, bmp, nullptr); DeleteObject(bmp);
    }
    DeleteDC(dc); ReleaseDC(nullptr, sdc);
    return il;
}
MIN_T NextOccurrence(const Event& e, MIN_T* wall) {
    MIN_T now = NowDisplay();
    std::vector<Occ> v; ExpandEvent(e, now, now + DAYMIN * 800, v);
    std::sort(v.begin(), v.end(), [](const Occ& a, const Occ& b) { return a.start < b.start; });
    for (auto& o : v) if (o.end > now || o.allDay) { if (wall) *wall = o.wall; return o.start; }
    if (wall) *wall = e.start;
    return EventToDisplay(e, e.start);
}
std::wstring FmtWhen(const Event& e, MIN_T ds) {
    std::wstring s = FmtDate(ds, L"ddd, MMM d, yyyy");
    if (e.allDay) return s + L" (all day)";
    return s + L" " + FmtTime(ds) + L" - " + FmtTime(ds + e.duration);
}

// ------------------------------------------------------------------ Go to date
static INT_PTR CALLBACK GoToProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG: SetWindowLongPtrW(d, DWLP_USER, lp); SetDtp(d, IDC_DATE, (MIN_T)*(int*)lp * DAYMIN); return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) { *(int*)GetWindowLongPtrW(d, DWLP_USER) = DayOf(GetDtp(d, IDC_DATE)); EndDialog(d, IDOK); }
        else if (LOWORD(wp) == IDCANCEL) EndDialog(d, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
bool GoToDateDialog(HWND owner, int& day) { return DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_GOTO), owner, GoToProc, (LPARAM)&day) == IDOK; }

// ------------------------------------------------------------------ Propose new time
struct ProposeData { MIN_T* start; int duration; std::wstring* comment; };
static INT_PTR CALLBACK ProposeProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    ProposeData* p = (ProposeData*)GetWindowLongPtrW(d, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(d, DWLP_USER, lp); p = (ProposeData*)lp;
        SetText(d, IDC_SUMMARY, L"Currently: " + FmtDate(*p->start, L"dddd, MMMM d") + L" " + FmtTime(*p->start) + L" (" + FmtDuration(p->duration) + L"). Choose the time you would prefer:");
        SetDtp(d, IDC_DATE, *p->start); SetDtp(d, IDC_TIME, *p->start);
        return TRUE;
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) {
            *p->start = (MIN_T)DayOf(GetDtp(d, IDC_DATE)) * DAYMIN + TimeOf(GetDtp(d, IDC_TIME));
            if (p->comment) *p->comment = DlgText(d, IDC_COMMENT);
            EndDialog(d, IDOK);
        } else if (LOWORD(wp) == IDCANCEL) EndDialog(d, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
bool ProposeTimeDialog(HWND owner, MIN_T& start, int duration, std::wstring* comment) {
    ProposeData p{ &start, duration, comment };
    return DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_PROPOSE), owner, ProposeProc, (LPARAM)&p) == IDOK;
}

// ------------------------------------------------------------------ Task edit
static INT_PTR CALLBACK TaskProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    Task* t = (Task*)GetWindowLongPtrW(d, DWLP_USER);
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtrW(d, DWLP_USER, lp); t = (Task*)lp;
        SetText(d, IDC_TITLE, t->title); SetText(d, IDC_NOTES, t->notes);
        Check(d, IDC_HASDUE, t->hasDue); SetDtp(d, IDC_DUEDATE, (MIN_T)(t->hasDue ? t->dueDay : TodayDisplay()) * DAYMIN);
        Enable(d, IDC_DUEDATE, t->hasDue);
        CbAdd(d, IDC_PRIORITY, L"Low", 0); CbAdd(d, IDC_PRIORITY, L"Normal", 1); CbAdd(d, IDC_PRIORITY, L"High", 2);
        CbSelData(d, IDC_PRIORITY, t->priority); Check(d, IDC_DONE, t->done);
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_HASDUE: Enable(d, IDC_DUEDATE, IsChecked(d, IDC_HASDUE)); break;
        case IDOK: {
            std::wstring title = Trim(DlgText(d, IDC_TITLE));
            if (title.empty()) { MessageBoxW(d, L"Please enter a task.", L"Task", MB_ICONWARNING); SetFocus(GetDlgItem(d, IDC_TITLE)); break; }
            t->title = title; t->notes = DlgText(d, IDC_NOTES); t->hasDue = IsChecked(d, IDC_HASDUE);
            t->dueDay = DayOf(GetDtp(d, IDC_DUEDATE)); t->priority = (int)CbData(d, IDC_PRIORITY, 1); t->done = IsChecked(d, IDC_DONE);
            EndDialog(d, IDOK); break;
        }
        case IDCANCEL: EndDialog(d, IDCANCEL); break;
        }
        return TRUE;
    }
    return FALSE;
}
bool EditTaskDlg(HWND owner, Task& t, bool) { return DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_TASK), owner, TaskProc, (LPARAM)&t) == IDOK; }

// ------------------------------------------------------------------ To-do list manager
static bool s_tasksFilling;
static void FillTasks(HWND d) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    int selId = (int)LvData(lv, LvSel(lv));
    s_tasksFilling = true;
    ListView_DeleteAllItems(lv);
    std::vector<Task*> v; for (auto& t : g.tasks) if (g.set.showCompleted || !t.done) v.push_back(&t);
    std::stable_sort(v.begin(), v.end(), [](Task* a, Task* b) {
        if (a->done != b->done) return !a->done;
        if (a->hasDue != b->hasDue) return a->hasDue;
        if (a->hasDue && a->dueDay != b->dueDay) return a->dueDay < b->dueDay;
        return a->priority > b->priority; });
    static const wchar_t* prio[] = { L"Low", L"Normal", L"High" };
    int today = TodayDisplay();
    for (Task* t : v) {
        std::wstring due = t->hasDue ? FmtDate((MIN_T)t->dueDay * DAYMIN) : L"";
        if (t->hasDue && !t->done && t->dueDay < today) due += L" (overdue)";
        int i = LvAdd(lv, { t->title, due, prio[std::clamp(t->priority, 0, 2)] }, t->id);
        ListView_SetCheckState(lv, i, t->done);
        if (t->id == selId) LvSelect(lv, i);
    }
    s_tasksFilling = false;
}
static INT_PTR CALLBACK TasksProc(HWND d, UINT msg, WPARAM wp, LPARAM lp) {
    HWND lv = GetDlgItem(d, IDC_LIST);
    switch (msg) {
    case WM_INITDIALOG:
        LvCols(lv, { { L"Task", 300 }, { L"Due", 130 }, { L"Priority", 70 } }, LVS_EX_CHECKBOXES);
        Check(d, IDC_SHOWDONE, g.set.showCompleted);
        FillTasks(d);
        return TRUE;
    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->idFrom == IDC_LIST && nh->code == LVN_ITEMCHANGED && !s_tasksFilling) {
            NMLISTVIEW* nl = (NMLISTVIEW*)lp;
            if ((nl->uChanged & LVIF_STATE) && ((nl->uNewState ^ nl->uOldState) & LVIS_STATEIMAGEMASK)) {
                if (Task* t = FindTask((int)nl->lParam)) {
                    t->done = ListView_GetCheckState(lv, nl->iItem) != 0; App_ModelChanged();
                    PostMessageW(d, WM_APP + 50, 0, 0);
                }
            }
        } else if (nh->idFrom == IDC_LIST && nh->code == NM_DBLCLK) SendMessageW(d, WM_COMMAND, IDC_EDIT, 0);
        break;
    }
    case WM_APP + 50: FillTasks(d); return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_SHOWDONE: g.set.showCompleted = IsChecked(d, IDC_SHOWDONE); App_ModelChanged(); FillTasks(d); break;
        case IDC_NEW: {
            Task t; t.id = 0; t.hasDue = true; t.dueDay = TodayDisplay();
            if (EditTaskDlg(d, t, true)) { t.id = NewId(); g.tasks.push_back(t); App_ModelChanged(); FillTasks(d); }
            break;
        }
        case IDC_EDIT: if (Task* t = FindTask((int)LvData(lv, LvSel(lv)))) { Task c = *t; if (EditTaskDlg(d, c, false)) { *FindTask(c.id) = c; App_ModelChanged(); FillTasks(d); } } break;
        case IDC_DELETE: {
            int id = (int)LvData(lv, LvSel(lv)); if (!id) break;
            g.tasks.erase(std::remove_if(g.tasks.begin(), g.tasks.end(), [&](const Task& t) { return t.id == id; }), g.tasks.end());
            App_ModelChanged(); FillTasks(d); break;
        }
        case IDC_CLEARDONE:
            g.tasks.erase(std::remove_if(g.tasks.begin(), g.tasks.end(), [](const Task& t) { return t.done; }), g.tasks.end());
            App_ModelChanged(); FillTasks(d); break;
        case IDOK: case IDCANCEL: EndDialog(d, IDOK); break;
        }
        return TRUE;
    }
    return FALSE;
}
void TasksManagerDialog(HWND owner) { DialogBoxParamW(g_hinst, MAKEINTRESOURCEW(IDD_TASKS), owner, TasksProc, 0); }
