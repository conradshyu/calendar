// WinCalendar - Windows Ribbon Framework host
#include "app.h"
#include <uiribbon.h>
#include <uiribbonpropertyhelpers.h>
#pragma comment(lib, "propsys.lib")

bool App_CommandEnabled(UINT id);
bool App_CommandChecked(UINT id, bool& checked);
void App_RibbonResized(int height);

static IUIFramework* s_fw = nullptr;
static int s_height = 0;

class RibbonHost : public IUIApplication, public IUICommandHandler {
    LONG m_ref = 1;
public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IUIApplication)) *ppv = static_cast<IUIApplication*>(this);
        else if (riid == __uuidof(IUICommandHandler)) *ppv = static_cast<IUICommandHandler*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&m_ref); }
    STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&m_ref); if (!r) delete this; return r; }

    // IUIApplication
    STDMETHODIMP OnViewChanged(UINT32, UI_VIEWTYPE type, IUnknown* view, UI_VIEWVERB verb, INT32) override {
        if (type != UI_VIEWTYPE_RIBBON) return E_NOTIMPL;
        if (verb == UI_VIEWVERB_CREATE || verb == UI_VIEWVERB_SIZE) {
            IUIRibbon* rb = nullptr;
            if (view && SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&rb)))) {
                UINT32 h = 0; if (SUCCEEDED(rb->GetHeight(&h))) { s_height = (int)h; App_RibbonResized(s_height); }
                rb->Release();
            }
        } else if (verb == UI_VIEWVERB_DESTROY) { s_height = 0; }
        return S_OK;
    }
    STDMETHODIMP OnCreateUICommand(UINT32, UI_COMMANDTYPE, IUICommandHandler** handler) override {
        return QueryInterface(IID_PPV_ARGS(handler));
    }
    STDMETHODIMP OnDestroyUICommand(UINT32, UI_COMMANDTYPE, IUICommandHandler*) override { return S_OK; }

    // IUICommandHandler
    STDMETHODIMP Execute(UINT32 cmd, UI_EXECUTIONVERB verb, const PROPERTYKEY*, const PROPVARIANT*, IUISimplePropertySet*) override {
        if (verb == UI_EXECUTIONVERB_EXECUTE) PostMessageW(g_hwnd, WM_COMMAND, MAKEWPARAM(cmd, 0), 0);
        return S_OK;
    }
    STDMETHODIMP UpdateProperty(UINT32 cmd, REFPROPERTYKEY key, const PROPVARIANT*, PROPVARIANT* newValue) override {
        if (key == UI_PKEY_Enabled) return UIInitPropertyFromBoolean(UI_PKEY_Enabled, App_CommandEnabled(cmd) ? TRUE : FALSE, newValue);
        if (key == UI_PKEY_BooleanValue) {
            bool on = false;
            if (!App_CommandChecked(cmd, on)) return E_NOTIMPL;
            return UIInitPropertyFromBoolean(UI_PKEY_BooleanValue, on ? TRUE : FALSE, newValue);
        }
        return E_NOTIMPL;
    }
};

static RibbonHost* s_ribbonApp = nullptr;

bool Ribbon_Init(HWND hwnd) {
    if (FAILED(CoCreateInstance(CLSID_UIRibbonFramework, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&s_fw)))) return false;
    s_ribbonApp = new RibbonHost();
    if (FAILED(s_fw->Initialize(hwnd, s_ribbonApp)) || FAILED(s_fw->LoadUI(g_hinst, L"APPLICATION_RIBBON"))) {
        s_fw->Destroy(); s_fw->Release(); s_fw = nullptr; s_ribbonApp->Release(); s_ribbonApp = nullptr; return false;
    }
    return true;
}
void Ribbon_Destroy() {
    if (s_fw) { s_fw->Destroy(); s_fw->Release(); s_fw = nullptr; }
    if (s_ribbonApp) { s_ribbonApp->Release(); s_ribbonApp = nullptr; }
}
int Ribbon_Height() { return s_height; }
void Ribbon_Invalidate() {
    if (!s_fw) return;
    s_fw->InvalidateUICommand(UI_ALL_COMMANDS, UI_INVALIDATIONS_PROPERTY, &UI_PKEY_Enabled);
    static const UINT toggles[] = { IDM_VIEWDAY, IDM_VIEWWEEK, IDM_VIEWMONTH, IDM_VIEWYEAR, IDM_VIEWAGENDA, IDM_AUTOTZ, IDM_SHOWCOMPLETED };
    for (UINT t : toggles) s_fw->InvalidateUICommand(t, UI_INVALIDATIONS_PROPERTY, &UI_PKEY_BooleanValue);
}

