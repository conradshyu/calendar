// Small helpers shared by the dialog modules
#pragma once
#include "app.h"
#include <initializer_list>

std::wstring DlgText(HWND d, int id);
void SetText(HWND d, int id, const std::wstring& s);
int  CbAdd(HWND d, int id, const std::wstring& s, LPARAM data);
LPARAM CbData(HWND d, int id, LPARAM def = -1);
void CbSelData(HWND d, int id, LPARAM v);
void CbReset(HWND d, int id);
void LvCols(HWND lv, std::initializer_list<std::pair<const wchar_t*, int>> cols, DWORD exStyle = 0);
int  LvAdd(HWND lv, const std::vector<std::wstring>& cells, LPARAM lp, int image = -1);
int  LvSel(HWND lv);
LPARAM LvData(HWND lv, int item);
void LvSelect(HWND lv, int item);
void SetDtp(HWND d, int id, MIN_T m);
MIN_T GetDtp(HWND d, int id);
bool IsChecked(HWND d, int id);
void Check(HWND d, int id, bool on);
void Enable(HWND d, int id, bool on);
void FillTzCombo(HWND d, int id, const std::wstring& selKey);
std::wstring TzComboKey(HWND d, int id);
void FillReminderPresets(HWND d, int id, bool withNone);
int  ParseReminderText(const std::wstring& s);
void SetPageData(HWND d, LPARAM lp);
template <class T> T* PageData(HWND d) { return (T*)GetWindowLongPtrW(d, DWLP_USER); }
void SetMsgResult(HWND d, LONG_PTR r);
void DisableEditing(HWND d);
bool OpenFileDlg(HWND owner, std::wstring& path, const wchar_t* filter, const wchar_t* ext, bool save);
void CopyToClipboard(HWND owner, const std::wstring& s);
int  PropSheet(HWND owner, const wchar_t* caption, std::initializer_list<std::pair<int, DLGPROC>> pages, LPARAM data, int startPage, bool closeOnly);
extern const int kReminderPresets[];
extern const int kReminderPresetCount;
