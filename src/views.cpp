// WinCalendar - main calendar view (Day / Week / Month / Year / Agenda), GDI painted
#include "app.h"

int g_viewMode = VIEW_WEEK, g_curDay = 0, g_selEvent = 0;
MIN_T g_selWall = 0, g_newAt = 0;

static HWND s_hwnd;
static HFONT s_fTitle, s_fNormal, s_fBold, s_fSmall, s_fBig;
static int s_scrollY = 0;
static int s_contentH = 0, s_pageH = 0;

enum { HIT_EVENT, HIT_DAY, HIT_MORE, HIT_TASK };
namespace { struct Hit { RECT rc; int kind; int id; MIN_T wall; int day; }; }
static std::vector<Hit> s_hits;

static const COLORREF kAccent = RGB(0, 103, 192);
static const COLORREF kGrid = RGB(225, 223, 221);
static const COLORREF kText = RGB(32, 31, 30);
static const COLORREF kSub = RGB(96, 94, 92);

static int TitleH() { return S(46); }
static int DayHdrH() { return S(30); }
static int HourH() { return S(44); }
static int Gutter() { return S(60) + (g.set.showSecondTz && FindTz(g.set.secondTz) ? S(52) : 0); }
static int WeekStartOf(int day) { return day - (Weekday(day) - g.set.weekStart + 7) % 7; }

void View_CreateFonts() {
    auto mk = [](int pt, int weight, const wchar_t* face) {
        return CreateFontW(-MulDiv(pt, g_dpi, 72), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face); };
    s_fTitle = mk(17, FW_NORMAL, L"Segoe UI Semilight");
    s_fBig = mk(13, FW_SEMIBOLD, L"Segoe UI");
    s_fNormal = mk(9, FW_NORMAL, L"Segoe UI");
    s_fBold = mk(9, FW_SEMIBOLD, L"Segoe UI");
    s_fSmall = mk(8, FW_NORMAL, L"Segoe UI");
}

static COLORREF Blend(COLORREF c, int pctWhite) {
    auto f = [&](int v) { return v + (255 - v) * pctWhite / 100; };
    return RGB(f(GetRValue(c)), f(GetGValue(c)), f(GetBValue(c)));
}
static void Fill(HDC dc, const RECT& r, COLORREF c) { SetDCBrushColor(dc, c); FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH)); }
static void Frame(HDC dc, const RECT& r, COLORREF c, int w = 1) {
    HPEN p = CreatePen(PS_SOLID, w, c); HGDIOBJ op = SelectObject(dc, p); HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left, r.top, r.right, r.bottom); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p);
}
static void HLine(HDC dc, int x0, int x1, int y, COLORREF c) { RECT r{ x0, y, x1, y + 1 }; Fill(dc, r, c); }
static void VLine(HDC dc, int x, int y0, int y1, COLORREF c) { RECT r{ x, y0, x + 1, y1 }; Fill(dc, r, c); }
static void Text(HDC dc, const std::wstring& s, RECT r, HFONT f, COLORREF c, UINT fmt = DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX) {
    SelectObject(dc, f); SetTextColor(dc, c); SetBkMode(dc, TRANSPARENT); DrawTextW(dc, s.c_str(), (int)s.size(), &r, fmt | DT_NOPREFIX);
}

std::wstring ViewTitle() {
    MIN_T d = (MIN_T)g_curDay * DAYMIN;
    switch (g_viewMode) {
    case VIEW_DAY: return FmtDate(d, L"dddd, MMMM d, yyyy");
    case VIEW_WEEK: { int f = WeekStartOf(g_curDay); MIN_T a = (MIN_T)f * DAYMIN, b = (MIN_T)(f + 6) * DAYMIN;
        return FmtDate(a, L"MMMM d") + L" \x2013 " + FmtDate(b, MinToSt(a).wMonth == MinToSt(b).wMonth ? L"d, yyyy" : L"MMMM d, yyyy"); }
    case VIEW_MONTH: return FmtDate(d, L"MMMM yyyy");
    case VIEW_YEAR: return FmtDate(d, L"yyyy");
    default: return L"Agenda \x2013 from " + FmtDate(d, L"dddd, MMMM d");
    }
}

static void DrawEventBox(HDC dc, RECT r, const Event& e, const Occ& o, bool compact) {
    COLORREF c = EventColor(e);
    bool declined = e.myResponse == RSVP_DECLINED, pending = e.myResponse == RSVP_NONE;
    bool sel = e.id == g_selEvent && o.wall == g_selWall;
    Fill(dc, r, declined ? RGB(245, 245, 245) : Blend(c, sel ? 55 : 78));
    if (e.busy == BUSY_FOCUS || e.busy == BUSY_OOO) {
        HBRUSH hb = CreateHatchBrush(HS_BDIAGONAL, Blend(c, 45)); SetBkMode(dc, TRANSPARENT);
        RECT ri = r; ri.left += S(4); FillRect(dc, &ri, hb); DeleteObject(hb);
    }
    RECT bar = r; bar.right = bar.left + S(4); Fill(dc, bar, declined ? RGB(180, 180, 180) : c);
    if (pending) {
        HPEN p = CreatePen(PS_DOT, 1, c); HGDIOBJ op = SelectObject(dc, p); HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        Rectangle(dc, r.left, r.top, r.right, r.bottom); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p);
    }
    if (sel) Frame(dc, r, Blend(c, 0) == c ? RGB(0, 0, 0) : c, 2);
    COLORREF tc = declined ? RGB(150, 150, 150) : kText;
    std::wstring title = e.title;
    if (pending) title = L"[?] " + title;
    if (declined) title = L"Declined: " + title;
    if (e.rec.freq) title += L" \x21BB";
    if (!e.confLink.empty()) title += L" \x25B6";
    RECT tr = r; tr.left += S(8); tr.right -= S(3); tr.top += S(2);
    int lh = S(16);
    if (compact || r.bottom - r.top < S(34)) {
        std::wstring s = (o.allDay || e.allDay) ? title : FmtTime(o.start) + L"  " + title;
        tr.bottom = std::min<LONG>(r.bottom, tr.top + lh + S(2)); Text(dc, s, tr, s_fBold, tc); return;
    }
    tr.bottom = tr.top + lh; Text(dc, title, tr, s_fBold, tc);
    tr.top += lh; tr.bottom = tr.top + lh;
    if (tr.bottom <= r.bottom) Text(dc, FmtTime(o.start) + L" \x2013 " + FmtTime(o.end), tr, s_fSmall, kSub);
    tr.top += lh; tr.bottom = tr.top + lh;
    if (tr.bottom <= r.bottom && !e.location.empty()) Text(dc, e.location, tr, s_fSmall, kSub);
    tr.top += lh; tr.bottom = tr.top + lh;
    if (tr.bottom <= r.bottom && e.tz != TZ_FLOATING && !e.tz.empty() && _wcsicmp(e.tz.c_str(), DisplayTzKey().c_str()) != 0) {
        MIN_T wall = o.wall; Text(dc, FmtTime(wall) + L" " + TzDisplayName(e.tz), tr, s_fSmall, kSub);
    }
}

// ------------------------------------------------------------ Day / Week
namespace { struct Lane { Occ o; MIN_T s, e; int col, ncols; }; }
static void PaintTimeGrid(HDC dc, int W, int H) {
    int nd = g_viewMode == VIEW_DAY ? 1 : 7;
    int first = g_viewMode == VIEW_DAY ? g_curDay : WeekStartOf(g_curDay);
    int gx = Gutter(), y = TitleH();
    double colW = (double)(W - gx) / nd;
    auto colX = [&](int i) { return gx + (int)(i * colW); };
    int today = TodayDisplay();
    MIN_T from = (MIN_T)first * DAYMIN, to = (MIN_T)(first + nd) * DAYMIN;
    std::vector<Occ> occs = Expand(from, to);

    // day headers
    for (int i = 0; i < nd; i++) {
        RECT r{ colX(i), y, colX(i + 1), y + DayHdrH() };
        bool isToday = first + i == today;
        if (first + i == g_curDay && nd > 1) Fill(dc, r, RGB(243, 242, 241));
        MIN_T dm = (MIN_T)(first + i) * DAYMIN;
        RECT r1 = r; r1.left += S(8);
        Text(dc, FmtDate(dm, L"ddd"), r1, s_fNormal, isToday ? kAccent : kSub);
        r1.left += S(34); Text(dc, FmtDate(dm, L"d"), r1, s_fBig, isToday ? kAccent : kText);
        if (isToday) { RECT u{ r.left, r.bottom - S(3), r.right, r.bottom }; Fill(dc, u, kAccent); }
        s_hits.push_back({ r, HIT_DAY, 0, 0, first + i });
    }
    y += DayHdrH();
    HLine(dc, 0, W, y, kGrid);

    // all-day strip
    std::vector<std::pair<Occ, std::pair<int, int>>> ad;   // occ, [startCol,endCol]
    for (auto& o : occs) if (o.allDay || o.end - o.start >= DAYMIN) {
        int a = std::max(0, DayOf(o.start) - first), b = std::min(nd - 1, DayOf(o.end - 1) - first);
        if (b >= a) ad.push_back({ o, { a, b } });
    }
    std::vector<std::vector<bool>> laneUse;
    std::vector<int> laneOf(ad.size());
    for (size_t k = 0; k < ad.size(); k++) {
        int L = 0;
        for (;; L++) {
            if (L == (int)laneUse.size()) laneUse.push_back(std::vector<bool>(nd, false));
            bool free = true; for (int c = ad[k].second.first; c <= ad[k].second.second; c++) if (laneUse[L][c]) free = false;
            if (free) break;
        }
        for (int c = ad[k].second.first; c <= ad[k].second.second; c++) laneUse[L][c] = true;
        laneOf[k] = L;
    }
    int lanes = std::min<int>((int)laneUse.size(), 4), laneH = S(22);
    int adH = std::max(S(26), lanes * laneH + S(6));
    RECT lbl{ 0, y, gx, y + adH }; Text(dc, L"All day", lbl, s_fSmall, kSub, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    for (size_t k = 0; k < ad.size(); k++) {
        if (laneOf[k] >= lanes) continue;
        RECT r{ colX(ad[k].second.first) + S(2), y + S(3) + laneOf[k] * laneH, colX(ad[k].second.second + 1) - S(3), y + S(3) + laneOf[k] * laneH + laneH - S(2) };
        if (Event* e = FindEvent(ad[k].first.id)) { DrawEventBox(dc, r, *e, ad[k].first, true); s_hits.push_back({ r, HIT_EVENT, e->id, ad[k].first.wall, DayOf(ad[k].first.start) }); }
    }
    for (int i = 0; i < nd; i++) s_hits.insert(s_hits.begin(), { RECT{ colX(i), y, colX(i + 1), y + adH }, HIT_DAY, -1, 0, first + i });
    y += adH;
    HLine(dc, 0, W, y, RGB(200, 198, 196));
    int gridTop = y + 1;
    s_pageH = H - gridTop;
    s_contentH = 24 * HourH();

    HRGN clip = CreateRectRgn(0, gridTop, W, H); SelectClipRgn(dc, clip);
    int top = gridTop - s_scrollY, hh = HourH();
    for (int i = 0; i < nd; i++) {
        bool workday = Weekday(first + i) != 0 && Weekday(first + i) != 6;
        for (int h = 0; h < 24; h++) {
            RECT r{ colX(i), top + h * hh, colX(i + 1), top + (h + 1) * hh };
            bool work = workday && h >= g.set.workStart && h < g.set.workEnd;
            Fill(dc, r, work ? RGB(255, 255, 255) : RGB(248, 248, 247));
        }
        VLine(dc, colX(i), top, top + 24 * hh, kGrid);
    }
    bool second = g.set.showSecondTz && FindTz(g.set.secondTz);
    for (int h = 0; h < 24; h++) {
        int yy = top + h * hh;
        HLine(dc, gx - S(6), W, yy, kGrid);
        for (int i = 0; i < nd; i++) { RECT hr{ colX(i) + 1, yy + hh / 2, colX(i + 1), yy + hh / 2 + 1 }; Fill(dc, hr, RGB(240, 239, 238)); }
        RECT tr{ second ? S(52) : 0, yy - S(8), gx - S(8), yy + S(8) };
        if (h) Text(dc, FmtTime((MIN_T)h * 60), tr, s_fSmall, kSub, DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        if (second) {
            MIN_T utc = ZoneToUtc((MIN_T)first * DAYMIN + h * 60, L"");
            MIN_T other = UtcToZone(utc, g.set.secondTz);
            RECT r2{ 0, yy - S(8), S(48), yy + S(8) };
            if (h) Text(dc, FmtTime(other), r2, s_fSmall, RGB(150, 148, 146), DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        }
    }

    // timed events
    for (int i = 0; i < nd; i++) {
        int day = first + i; MIN_T ds = (MIN_T)day * DAYMIN, de = ds + DAYMIN;
        std::vector<Lane> L;
        for (auto& o : occs) {
            if (o.allDay || o.end - o.start >= DAYMIN) continue;
            MIN_T s = std::max(o.start, ds), e = std::min(std::max(o.end, o.start + 15), de);
            if (e <= ds || s >= de || (s >= e)) continue;
            L.push_back({ o, s, e, 0, 1 });
        }
        std::sort(L.begin(), L.end(), [](const Lane& a, const Lane& b) { return a.s < b.s || (a.s == b.s && a.e > b.e); });
        size_t gs = 0; MIN_T gEnd = -1; std::vector<MIN_T> colEnd;
        auto closeGroup = [&](size_t end) { int n = (int)colEnd.size(); for (size_t k = gs; k < end; k++) L[k].ncols = n; };
        for (size_t k = 0; k < L.size(); k++) {
            if (L[k].s >= gEnd && k > gs) { closeGroup(k); gs = k; colEnd.clear(); }
            if (k == gs) colEnd.clear();
            int c = 0; while (c < (int)colEnd.size() && colEnd[c] > L[k].s) c++;
            if (c == (int)colEnd.size()) colEnd.push_back(L[k].e); else colEnd[c] = L[k].e;
            L[k].col = c; gEnd = std::max(gEnd, L[k].e);
        }
        closeGroup(L.size());
        for (auto& l : L) {
            Event* e = FindEvent(l.o.id); if (!e) continue;
            double w = (colW - S(8)) / l.ncols;
            RECT r{ colX(i) + S(2) + (int)(l.col * w), top + (int)((l.s - ds) * hh / 60), colX(i) + S(2) + (int)((l.col + 1) * w) - S(2), top + (int)((l.e - ds) * hh / 60) - 1 };
            if (r.bottom - r.top < S(16)) r.bottom = r.top + S(16);
            DrawEventBox(dc, r, *e, l.o, false);
            s_hits.push_back({ r, HIT_EVENT, e->id, l.o.wall, day });
        }
    }
    // now line
    MIN_T now = NowDisplay();
    if (DayOf(now) >= first && DayOf(now) < first + nd) {
        int i = DayOf(now) - first; int yy = top + TimeOf(now) * hh / 60;
        RECT r{ colX(i), yy - 1, colX(i + 1), yy + 1 }; Fill(dc, r, RGB(196, 43, 28));
        RECT dot{ colX(i) - S(4), yy - S(4), colX(i) + S(4), yy + S(4) };
        SetDCBrushColor(dc, RGB(196, 43, 28)); SelectObject(dc, GetStockObject(DC_BRUSH)); SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
    }
    SelectClipRgn(dc, nullptr); DeleteObject(clip);
}

// ------------------------------------------------------------ Month
static void PaintMonth(HDC dc, int W, int H) {
    int y0 = TitleH(); int yy, mm, dd; DayYMD(g_curDay, yy, mm, dd);
    int first = MakeDay(yy, mm, 1), start = WeekStartOf(first);
    int hdr = S(26);
    for (int c = 0; c < 7; c++) {
        RECT r{ c * W / 7, y0, (c + 1) * W / 7, y0 + hdr }; r.left += S(8);
        Text(dc, FmtDate((MIN_T)(start + c) * DAYMIN, L"dddd"), r, s_fNormal, kSub);
    }
    y0 += hdr; HLine(dc, 0, W, y0, kGrid);
    int cellH = (H - y0) / 6;
    std::vector<Occ> occs = Expand((MIN_T)start * DAYMIN, (MIN_T)(start + 42) * DAYMIN);
    std::vector<std::vector<Occ>> perDay(42);
    for (auto& o : occs) {
        int a = std::max(0, DayOf(o.start) - start), b = std::min(41, DayOf(std::max(o.end - 1, o.start)) - start);
        for (int d = a; d <= b; d++) perDay[d].push_back(o);
    }
    int today = TodayDisplay(), lineH = S(19);
    for (int k = 0; k < 42; k++) {
        int day = start + k, row = k / 7, col = k % 7;
        RECT r{ col * W / 7, y0 + row * cellH, (col + 1) * W / 7, y0 + (row + 1) * cellH };
        int y, m, d; DayYMD(day, y, m, d);
        bool other = m != mm;
        Fill(dc, r, other ? RGB(248, 248, 247) : (day == g_curDay ? RGB(239, 246, 252) : RGB(255, 255, 255)));
        s_hits.push_back({ r, HIT_DAY, 0, 0, day });
        RECT nr{ r.left + S(6), r.top + S(3), r.left + S(30), r.top + S(23) };
        if (day == today) {
            SetDCBrushColor(dc, kAccent); SelectObject(dc, GetStockObject(DC_BRUSH)); SelectObject(dc, GetStockObject(NULL_PEN));
            Ellipse(dc, nr.left - S(2), nr.top, nr.left + S(22), nr.top + S(22));
            RECT t{ nr.left - S(2), nr.top, nr.left + S(22), nr.top + S(22) };
            Text(dc, std::to_wstring(d), t, s_fBold, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else Text(dc, d == 1 ? FmtDate((MIN_T)day * DAYMIN, L"MMM d") : std::to_wstring(d), nr, d == 1 ? s_fBold : s_fNormal, other ? RGB(160, 158, 156) : kText, DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_NOCLIP);
        int maxLines = std::max(0, (cellH - S(26)) / lineH);
        auto& list = perDay[k];
        int show = (int)list.size() <= maxLines ? (int)list.size() : std::max(0, maxLines - 1);
        for (int i = 0; i < show; i++) {
            Event* e = FindEvent(list[i].id); if (!e) continue;
            RECT er{ r.left + S(3), r.top + S(25) + i * lineH, r.right - S(3), r.top + S(25) + i * lineH + lineH - S(2) };
            if (list[i].allDay || list[i].end - list[i].start >= DAYMIN) DrawEventBox(dc, er, *e, list[i], true);
            else {
                bool sel = e->id == g_selEvent && list[i].wall == g_selWall;
                if (sel) Fill(dc, er, Blend(EventColor(*e), 70));
                RECT dot{ er.left + S(2), er.top + S(5), er.left + S(9), er.top + S(12) };
                SetDCBrushColor(dc, EventColor(*e)); SelectObject(dc, GetStockObject(DC_BRUSH)); SelectObject(dc, GetStockObject(NULL_PEN));
                Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
                RECT tr = er; tr.left += S(13);
                std::wstring s = FmtTime(list[i].start) + L" " + e->title;
                Text(dc, s, tr, s_fNormal, e->myResponse == RSVP_DECLINED ? RGB(160, 160, 160) : kText);
            }
            s_hits.push_back({ er, HIT_EVENT, e->id, list[i].wall, day });
        }
        if (show < (int)list.size()) {
            RECT mr{ r.left + S(8), r.top + S(25) + show * lineH, r.right - S(3), r.top + S(25) + show * lineH + lineH };
            Text(dc, L"+" + std::to_wstring(list.size() - show) + L" more", mr, s_fBold, kAccent);
            s_hits.push_back({ mr, HIT_MORE, 0, 0, day });
        }
        VLine(dc, r.left, r.top, r.bottom, kGrid); HLine(dc, r.left, r.right, r.bottom - 1, kGrid);
    }
}

// ------------------------------------------------------------ Year
static void PaintYear(HDC dc, int W, int H) {
    int y, m, d; DayYMD(g_curDay, y, m, d);
    int y0 = TitleH();
    int cols = W > H * 1.2 ? 4 : 3, rows = 12 / cols;
    int bw = W / cols, bh = (H - y0) / rows;
    int ys = MakeDay(y, 1, 1), ye = MakeDay(y + 1, 1, 1);
    std::vector<int> cnt(ye - ys, 0);
    for (auto& o : Expand((MIN_T)ys * DAYMIN, (MIN_T)ye * DAYMIN)) {
        int a = std::max(ys, DayOf(o.start)), b = std::min(ye - 1, DayOf(std::max(o.start, o.end - 1)));
        for (int k = a; k <= b; k++) cnt[k - ys]++;
    }
    int today = TodayDisplay();
    for (int mo = 1; mo <= 12; mo++) {
        int bx = ((mo - 1) % cols) * bw, by = y0 + ((mo - 1) / cols) * bh;
        RECT tr{ bx + S(12), by + S(4), bx + bw, by + S(28) };
        Text(dc, FmtDate(MakeMin(y, mo, 1), L"MMMM"), tr, s_fBig, mo == MinToSt(NowDisplay()).wMonth && y == MinToSt(NowDisplay()).wYear ? kAccent : kText);
        int cw = std::min((bw - S(24)) / 7, S(34)), ch = std::min((bh - S(54)) / 6, S(26));
        int gx = bx + S(12), gy = by + S(30);
        for (int c = 0; c < 7; c++) { RECT r{ gx + c * cw, gy, gx + (c + 1) * cw, gy + S(16) }; Text(dc, FmtDate((MIN_T)(WeekStartOf(0) + c) * DAYMIN, L"ddd").substr(0, 2), r, s_fSmall, kSub, DT_CENTER | DT_SINGLELINE | DT_VCENTER); }
        gy += S(18);
        int f = MakeDay(y, mo, 1), st = WeekStartOf(f), n = DaysInMonth(y, mo);
        for (int k = 0; k < 42; k++) {
            int day = st + k; if (day < f || day >= f + n) continue;
            RECT r{ gx + (k % 7) * cw, gy + (k / 7) * ch, gx + (k % 7 + 1) * cw, gy + (k / 7 + 1) * ch };
            int c = cnt[day - ys];
            RECT in = r; InflateRect(&in, -1, -1);
            if (c) Fill(dc, in, Blend(kAccent, std::max(30, 88 - c * 14)));
            if (day == today) Frame(dc, in, RGB(196, 43, 28), 2);
            if (day == g_curDay) Frame(dc, in, kText, 1);
            int dy, dm, dd; DayYMD(day, dy, dm, dd);
            Text(dc, std::to_wstring(dd), r, c >= 4 ? s_fBold : s_fNormal, c >= 5 ? RGB(255, 255, 255) : kText, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            s_hits.push_back({ r, HIT_DAY, 0, 0, day });
        }
    }
}

// ------------------------------------------------------------ Agenda
static void PaintAgenda(HDC dc, int W, int H, bool measureOnly) {
    int y0 = TitleH(); s_pageH = H - y0;
    int from = g_curDay, to = g_curDay + 90;
    std::vector<Occ> occs = Expand((MIN_T)from * DAYMIN, (MIN_T)to * DAYMIN);
    int y = y0 - s_scrollY, hdrH = S(30), rowH = S(46);
    HRGN clip = CreateRectRgn(0, y0, W, H); if (!measureOnly) SelectClipRgn(dc, clip);
    int total = 0;
    int today = TodayDisplay();
    for (int day = from; day < to; day++) {
        std::vector<Occ> list;
        for (auto& o : occs) if (o.end > (MIN_T)day * DAYMIN && o.start < (MIN_T)(day + 1) * DAYMIN && (DayOf(o.start) == day || day == from)) list.push_back(o);
        std::vector<const Task*> tasks;
        for (auto& t : g.tasks) if (t.hasDue && t.dueDay == day && (!t.done || g.set.showCompleted)) tasks.push_back(&t);
        if (list.empty() && tasks.empty()) continue;
        if (!measureOnly) {
            RECT hr{ 0, y, W, y + hdrH }; Fill(dc, hr, RGB(243, 242, 241));
            RECT tr = hr; tr.left = S(16);
            std::wstring label = FmtDate((MIN_T)day * DAYMIN, L"dddd, MMMM d, yyyy");
            if (day == today) label = L"Today \x2013 " + label; else if (day == today + 1) label = L"Tomorrow \x2013 " + label;
            Text(dc, label, tr, s_fBold, day == today ? kAccent : kText);
            s_hits.push_back({ hr, HIT_DAY, 0, 0, day });
        }
        y += hdrH; total += hdrH;
        for (auto& o : list) {
            Event* e = FindEvent(o.id); if (!e) continue;
            if (!measureOnly) {
                RECT r{ 0, y, W, y + rowH };
                bool sel = e->id == g_selEvent && o.wall == g_selWall;
                if (sel) Fill(dc, r, Blend(EventColor(*e), 80));
                RECT bar{ S(16), y + S(6), S(20), y + rowH - S(6) }; Fill(dc, bar, EventColor(*e));
                RECT tc{ S(28), y + S(4), S(150), y + S(22) };
                Text(dc, o.allDay ? L"All day" : FmtTime(o.start), tc, s_fNormal, kText);
                tc.top += S(18); tc.bottom += S(18);
                if (!o.allDay) Text(dc, FmtDuration((int)(o.end - o.start)), tc, s_fSmall, kSub);
                RECT tt{ S(160), y + S(4), W - S(220), y + S(22) };
                std::wstring title = e->title;
                if (e->myResponse == RSVP_NONE) title = L"[Invitation - respond] " + title;
                if (e->myResponse == RSVP_DECLINED) title = L"Declined: " + title;
                if (e->busy == BUSY_FOCUS) title += L"   (focus time)"; else if (e->busy == BUSY_OOO) title += L"   (out of office)";
                Text(dc, title, tt, s_fBold, e->myResponse == RSVP_DECLINED ? RGB(150, 150, 150) : kText);
                std::wstring sub = e->location;
                if (!e->attendees.empty()) { if (!sub.empty()) sub += L"  \x2022  "; sub += std::to_wstring(e->attendees.size()) + L" attendee(s)"; }
                if (!e->confLink.empty()) { if (!sub.empty()) sub += L"  \x2022  "; sub += kConfNames[std::clamp(e->confProvider, 0, CONF_COUNT - 1)]; }
                if (e->rec.freq) { if (!sub.empty()) sub += L"  \x2022  "; sub += RecurrenceText(e->rec, e->start); }
                tt.top += S(18); tt.bottom += S(18); Text(dc, sub, tt, s_fSmall, kSub);
                Calendar* c = FindCalendar(e->calendarId);
                RECT cr{ W - S(210), y + S(4), W - S(16), y + S(22) };
                Text(dc, c ? c->name : L"", cr, s_fSmall, c ? c->color : kSub, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
                if (!e->category.empty()) { cr.top += S(18); cr.bottom += S(18); Text(dc, e->category, cr, s_fSmall, EventColor(*e), DT_RIGHT | DT_SINGLELINE | DT_VCENTER); }
                HLine(dc, S(16), W - S(16), y + rowH - 1, RGB(237, 235, 233));
                s_hits.push_back({ r, HIT_EVENT, e->id, o.wall, day });
            }
            y += rowH; total += rowH;
        }
        for (auto t : tasks) {
            if (!measureOnly) {
                RECT r{ 0, y, W, y + S(28) };
                RECT cb{ S(28), y + S(7), S(42), y + S(21) }; Frame(dc, cb, kSub);
                if (t->done) { RECT in = cb; InflateRect(&in, -S(3), -S(3)); Fill(dc, in, kAccent); }
                RECT tt{ S(160), y, W - S(16), y + S(28) };
                Text(dc, L"Task: " + t->title + (t->priority == 2 ? L"  (high priority)" : L""), tt, s_fNormal, t->done ? RGB(150, 150, 150) : kText);
                s_hits.push_back({ r, HIT_TASK, t->id, 0, day });
            }
            y += S(28); total += S(28);
        }
    }
    if (total == 0 && !measureOnly) {
        RECT r{ 0, y0 + S(40), W, y0 + S(80) };
        Text(dc, L"No events in the next 90 days.", r, s_fNormal, kSub, DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }
    s_contentH = total + S(20);
    SelectClipRgn(dc, nullptr); DeleteObject(clip);
}

static void Paint(HDC dc, int W, int H) {
    s_hits.clear();
    RECT all{ 0, 0, W, H }; Fill(dc, all, RGB(255, 255, 255));
    RECT tr{ S(16), 0, W - S(16), TitleH() };
    Text(dc, ViewTitle(), tr, s_fTitle, kText);
    std::wstring tz = L"Times in " + TzDisplayName(DisplayTzKey()) + (g.set.autoTz ? L"  (automatic)" : L"  (manual)");
    Text(dc, tz, tr, s_fSmall, kSub, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    HLine(dc, 0, W, TitleH() - 1, kGrid);
    switch (g_viewMode) {
    case VIEW_DAY: case VIEW_WEEK: PaintTimeGrid(dc, W, H); break;
    case VIEW_MONTH: PaintMonth(dc, W, H); break;
    case VIEW_YEAR: PaintYear(dc, W, H); break;
    default: PaintAgenda(dc, W, H, false); break;
    }
}

static void UpdateScroll() {
    RECT rc; GetClientRect(s_hwnd, &rc);
    if (g_viewMode == VIEW_DAY || g_viewMode == VIEW_WEEK) {
        s_contentH = 24 * HourH();
    } else if (g_viewMode == VIEW_AGENDA) {
        HDC dc = GetDC(s_hwnd); PaintAgenda(dc, rc.right, rc.bottom, true); ReleaseDC(s_hwnd, dc); s_hits.clear();
    } else { s_contentH = 0; }
    int page = std::max(1, (int)rc.bottom - TitleH() - ((g_viewMode <= VIEW_WEEK) ? DayHdrH() + S(40) : 0));
    int maxY = std::max(0, s_contentH - page);
    s_scrollY = std::clamp(s_scrollY, 0, maxY);
    SCROLLINFO si{ sizeof(si), SIF_ALL | SIF_DISABLENOSCROLL, 0, std::max(0, s_contentH - 1), (UINT)page, s_scrollY };
    if (s_contentH == 0) { si.nMax = 0; si.nPage = 1; }
    SetScrollInfo(s_hwnd, SB_VERT, &si, TRUE);
}

void View_Refresh() { if (!s_hwnd) return; UpdateScroll(); InvalidateRect(s_hwnd, nullptr, FALSE); }
void View_ScrollToWork() { s_scrollY = std::max(0, g.set.workStart * HourH() - S(10)); View_Refresh(); }
void View_SetMode(int mode) {
    int old = g_viewMode; g_viewMode = mode; g.set.view = mode;
    if (mode == VIEW_AGENDA || (old != VIEW_DAY && old != VIEW_WEEK)) s_scrollY = 0;
    if ((mode == VIEW_DAY || mode == VIEW_WEEK) && old != VIEW_DAY && old != VIEW_WEEK) { View_ScrollToWork(); return; }
    View_Refresh();
}
void View_Navigate(int dir) {
    if (dir == 0) g_curDay = TodayDisplay();
    else switch (g_viewMode) {
    case VIEW_DAY: g_curDay += dir; break;
    case VIEW_WEEK: g_curDay += 7 * dir; break;
    case VIEW_MONTH: g_curDay = AddMonths(g_curDay, dir); break;
    case VIEW_YEAR: g_curDay = AddMonths(g_curDay, 12 * dir); break;
    default: g_curDay += 7 * dir; s_scrollY = 0; break;
    }
    View_Refresh();
}

static const Hit* HitTest(int x, int y) {
    for (size_t i = s_hits.size(); i-- > 0;) { POINT p{ x, y }; if (PtInRect(&s_hits[i].rc, p)) return &s_hits[i]; }
    return nullptr;
}
// Converts a point in the time grid into a display time; returns false outside the grid
static bool PointToTime(int x, int y, MIN_T& t, bool& allDay) {
    const Hit* h = HitTest(x, y);
    if (g_viewMode == VIEW_DAY || g_viewMode == VIEW_WEEK) {
        RECT rc; GetClientRect(s_hwnd, &rc);
        int nd = g_viewMode == VIEW_DAY ? 1 : 7, first = g_viewMode == VIEW_DAY ? g_curDay : WeekStartOf(g_curDay);
        int gx = Gutter(); if (x < gx) return false;
        int col = std::min(nd - 1, (int)((x - gx) * nd / std::max(1, (int)rc.right - gx)));
        if (h && h->kind == HIT_DAY && h->id == -1) { t = (MIN_T)(first + col) * DAYMIN; allDay = true; return true; }
        int gridTop = rc.bottom - s_pageH;
        if (y < gridTop) return false;
        int mins = (y - gridTop + s_scrollY) * 60 / HourH(); mins = mins / 30 * 30;
        t = (MIN_T)(first + col) * DAYMIN + std::clamp(mins, 0, 1410); allDay = false; return true;
    }
    if (h && (h->kind == HIT_DAY || h->kind == HIT_EVENT)) { t = (MIN_T)h->day * DAYMIN + 9 * 60; allDay = false; return true; }
    return false;
}

static LRESULT CALLBACK ViewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, std::max(1L, rc.right), std::max(1L, rc.bottom));
        HGDIOBJ old = SelectObject(mem, bmp);
        Paint(mem, rc.right, rc.bottom);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps); return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: UpdateScroll(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    case WM_VSCROLL: {
        SCROLLINFO si{ sizeof(si), SIF_ALL }; GetScrollInfo(hwnd, SB_VERT, &si);
        int y = si.nPos;
        switch (LOWORD(wp)) {
        case SB_LINEUP: y -= S(30); break; case SB_LINEDOWN: y += S(30); break;
        case SB_PAGEUP: y -= si.nPage; break; case SB_PAGEDOWN: y += si.nPage; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: y = si.nTrackPos; break;
        case SB_TOP: y = 0; break; case SB_BOTTOM: y = si.nMax; break;
        }
        s_scrollY = y; UpdateScroll(); InvalidateRect(hwnd, nullptr, FALSE); return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp);
        if (g_viewMode == VIEW_MONTH || g_viewMode == VIEW_YEAR) { View_Navigate(delta > 0 ? -1 : 1); SendMessage(GetParent(hwnd), WM_APP_SELCHANGED, 1, 0); }
        else { s_scrollY -= delta * S(90) / WHEEL_DELTA; UpdateScroll(); InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: {
        SetFocus(hwnd);
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        const Hit* h = HitTest(x, y);
        int oldSel = g_selEvent; MIN_T oldWall = g_selWall; int oldDay = g_curDay;
        g_selEvent = 0; g_selWall = 0;
        if (h) {
            if (h->kind == HIT_EVENT) { g_selEvent = h->id; g_selWall = h->wall; if (g_viewMode == VIEW_MONTH || g_viewMode == VIEW_AGENDA) g_curDay = h->day; }
            else if (h->kind == HIT_MORE && msg == WM_LBUTTONDOWN) { App_GoToDay(h->day, VIEW_DAY); return 0; }
            else if (h->kind == HIT_DAY && g_viewMode == VIEW_YEAR && msg == WM_LBUTTONDOWN) { App_GoToDay(h->day, VIEW_DAY); return 0; }
            else if (h->kind == HIT_DAY && h->day) g_curDay = h->day;
            else if (h->kind == HIT_TASK && msg == WM_LBUTTONDOWN) { if (Task* t = FindTask(h->id)) { t->done = !t->done; App_ModelChanged(); } return 0; }
        }
        if (g_viewMode == VIEW_WEEK || g_viewMode == VIEW_DAY) {
            MIN_T t; bool ad; if (PointToTime(x, y, t, ad)) g_curDay = DayOf(t);
        }
        if (oldSel != g_selEvent || oldWall != g_selWall || oldDay != g_curDay) { InvalidateRect(hwnd, nullptr, FALSE); SendMessage(GetParent(hwnd), WM_APP_SELCHANGED, 0, 0); }
        if (msg == WM_RBUTTONDOWN) {
            MIN_T t; bool ad;
            if (PointToTime(x, y, t, ad)) g_newAt = t; else g_newAt = (MIN_T)g_curDay * DAYMIN + 9 * 60;
            POINT pt{ x, y }; ClientToScreen(hwnd, &pt);
            PostMessage(GetParent(hwnd), WM_APP_CONTEXT, 0, MAKELPARAM(pt.x, pt.y));
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        const Hit* h = HitTest(x, y);
        if (h && h->kind == HIT_EVENT) { PostMessage(GetParent(hwnd), WM_APP_OPENSEL, 0, 0); return 0; }
        MIN_T t; bool ad;
        if (g_viewMode != VIEW_YEAR && g_viewMode != VIEW_AGENDA && PointToTime(x, y, t, ad)) { g_newAt = t; PostMessage(GetParent(hwnd), WM_APP_NEWAT, ad, 0); }
        return 0;
    }
    case WM_KEYDOWN:
        switch (wp) {
        case VK_PRIOR: View_Navigate(-1); SendMessage(GetParent(hwnd), WM_APP_SELCHANGED, 1, 0); return 0;
        case VK_NEXT: View_Navigate(1); SendMessage(GetParent(hwnd), WM_APP_SELCHANGED, 1, 0); return 0;
        case VK_LEFT: g_curDay -= (g_viewMode == VIEW_MONTH || g_viewMode == VIEW_WEEK) ? 1 : (g_viewMode == VIEW_DAY ? 1 : 0); View_Refresh(); SendMessage(GetParent(hwnd), WM_APP_SELCHANGED, 1, 0); return 0;
        case VK_RIGHT: g_curDay += (g_viewMode == VIEW_MONTH || g_viewMode == VIEW_WEEK || g_viewMode == VIEW_DAY) ? 1 : 0; View_Refresh(); SendMessage(GetParent(hwnd), WM_APP_SELCHANGED, 1, 0); return 0;
        case VK_UP: if (g_viewMode == VIEW_MONTH) { g_curDay -= 7; View_Refresh(); SendMessage(GetParent(hwnd), WM_APP_SELCHANGED, 1, 0); } else { s_scrollY -= S(40); View_Refresh(); } return 0;
        case VK_DOWN: if (g_viewMode == VIEW_MONTH) { g_curDay += 7; View_Refresh(); SendMessage(GetParent(hwnd), WM_APP_SELCHANGED, 1, 0); } else { s_scrollY += S(40); View_Refresh(); } return 0;
        case VK_RETURN: if (g_selEvent) PostMessage(GetParent(hwnd), WM_APP_OPENSEL, 0, 0); return 0;
        case VK_DELETE: if (g_selEvent) PostMessage(GetParent(hwnd), WM_COMMAND, IDM_DELETE, 0); return 0;
        }
        break;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND CreateCalView(HWND parent) {
    WNDCLASSW wc{}; wc.lpfnWndProc = ViewProc; wc.hInstance = g_hinst; wc.lpszClassName = L"WinCalView";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    RegisterClassW(&wc);
    s_hwnd = CreateWindowExW(0, L"WinCalView", L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP, 0, 0, 10, 10, parent, (HMENU)IDC_VIEW, g_hinst, nullptr);
    return s_hwnd;
}
