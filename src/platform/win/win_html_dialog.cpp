#include "win_html_dialog.h"
#include "../../core/js_api_util.h"
#include "../../core/log.h"
#include <mshtmhst.h>
#include <urlmon.h>
#include <memory>

namespace jsp::win {

namespace {

std::wstring widen(const std::string& s) { return std::wstring(pfc::stringcvt::string_wide_from_utf8(s.c_str())); }
std::string narrow(const wchar_t* s) { return std::string(pfc::stringcvt::string_utf8_from_wide(s ? s : L"")); }

DialogValue from_variant(const VARIANT& in) {
    DialogValue d;
    const VARIANT& v = in.vt == (VT_BYREF | VT_VARIANT) && in.pvarVal ? *in.pvarVal : in;
    switch (v.vt) {
    case VT_EMPTY:
    case VT_NULL:
        return d;
    case VT_BOOL:
        d.kind = DialogValue::Kind::Bool;
        d.number = v.boolVal != VARIANT_FALSE ? 1 : 0;
        return d;
    case VT_BSTR:
        d.kind = DialogValue::Kind::String;
        d.text = narrow(v.bstrVal);
        return d;
    default:
        break;
    }
    VARIANT c;
    VariantInit(&c);
    if (v.vt != VT_DISPATCH && SUCCEEDED(VariantChangeType(&c, &v, 0, VT_R8))) {
        d.kind = DialogValue::Kind::Number;
        d.number = c.dblVal;
    } else if (SUCCEEDED(VariantChangeType(&c, &v, 0, VT_BSTR))) {
        d.kind = DialogValue::Kind::String;
        d.text = narrow(c.bstrVal);
    }
    VariantClear(&c);
    return d;
}

class Callback : public IDispatch {
public:
    Callback(int fn, const DialogCall* call, std::shared_ptr<bool> alive) : m_fn(fn), m_call(call), m_alive(std::move(alive)) {}
    virtual ~Callback() = default;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER;
        if (iid == IID_IUnknown || iid == IID_IDispatch) {
            *out = static_cast<IDispatch*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&m_refs); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = InterlockedDecrement(&m_refs);
        if (!n) delete this;
        return (ULONG)n;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* n) override {
        if (n) *n = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return DISP_E_UNKNOWNNAME; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD flags, DISPPARAMS* params, VARIANT* result, EXCEPINFO*, UINT*) override {
        if (id != DISPID_VALUE || !(flags & DISPATCH_METHOD)) return DISP_E_MEMBERNOTFOUND;
        if (result) VariantInit(result);
        if (!*m_alive || !m_call || !*m_call) return S_OK;
        std::vector<DialogValue> args;
        for (UINT i = params ? params->cArgs : 0; i-- > 0;) args.push_back(from_variant(params->rgvarg[i]));
        (*m_call)(m_fn, args);
        return S_OK;
    }

private:
    LONG m_refs = 1;
    int m_fn;
    const DialogCall* m_call;
    std::shared_ptr<bool> m_alive;
};

void to_variant(const DialogValue& d, VARIANT& out, const DialogCall& call, const std::shared_ptr<bool>& alive) {
    VariantInit(&out);
    switch (d.kind) {
    case DialogValue::Kind::Bool:
        out.vt = VT_BOOL;
        out.boolVal = d.number != 0 ? VARIANT_TRUE : VARIANT_FALSE;
        break;
    case DialogValue::Kind::Number:
        out.vt = VT_R8;
        out.dblVal = d.number;
        break;
    case DialogValue::Kind::String:
        out.vt = VT_BSTR;
        out.bstrVal = SysAllocString(widen(d.text).c_str());
        break;
    case DialogValue::Kind::Function:
        out.vt = VT_DISPATCH;
        out.pdispVal = new Callback(d.fn, &call, alive);
        break;
    case DialogValue::Kind::Array: {
        SAFEARRAY* a = SafeArrayCreateVector(VT_VARIANT, 0, (ULONG)d.items.size());
        if (!a) break;
        for (LONG i = 0; i < (LONG)d.items.size(); ++i) {
            VARIANT item;
            to_variant(d.items[(size_t)i], item, call, alive);
            SafeArrayPutElement(a, &i, &item);
            VariantClear(&item);
        }
        out.vt = VT_ARRAY | VT_VARIANT;
        out.parray = a;
        break;
    }
    default:
        out.vt = VT_NULL;
        break;
    }
}

}

bool html_dialog(HWND owner, const HtmlDialog& dialog, const DialogCall& call) {
    static HMODULE mshtml = LoadLibraryW(L"mshtml.dll");
    auto show = mshtml ? reinterpret_cast<SHOWHTMLDIALOGEXFN*>(GetProcAddress(mshtml, "ShowHTMLDialogEx")) : nullptr;
    if (!show) return false;
    const std::string html = dialog_html(dialog.html, dialog.baseDir, true);
    std::wstring dir = widen(dialog.baseDir);
    for (wchar_t& c : dir)
        if (c == L'/') c = L'\\';
    if (!dir.empty() && dir.back() != L'\\') dir += L'\\';
    FILE* f = nullptr;
    std::wstring path = dir + L"foo_js_panel_dialog.html";
    if (!dir.empty()) f = _wfopen(path.c_str(), L"wb");
    if (!f) {
        wchar_t temp[MAX_PATH];
        const DWORD n = GetTempPathW(MAX_PATH, temp);
        if (!n || n >= MAX_PATH) return false;
        path = std::wstring(temp) + L"foo_js_panel_dialog.html";
        f = _wfopen(path.c_str(), L"wb");
    }
    if (!f) return false;
    const bool written = fwrite("\xEF\xBB\xBF", 1, 3, f) == 3 && fwrite(html.data(), 1, html.size(), f) == html.size();
    fclose(f);
    if (!written) return false;
    IMoniker* moniker = nullptr;
    if (FAILED(CreateURLMonikerEx(nullptr, path.c_str(), &moniker, URL_MK_UNIFORM)) || !moniker) return false;
    std::wstring features = L"dialogWidth:" + std::to_wstring(dialog.width) + L"px;dialogHeight:" + std::to_wstring(dialog.height) +
                            L"px;center:" + (dialog.center ? L"yes" : L"no") + L";resizable:" + (dialog.resizable ? L"yes" : L"no") +
                            L";scroll:" + (dialog.scroll ? L"yes" : L"no") + L";status:no;help:no;";
    if (!dialog.center) features += L"dialogLeft:" + std::to_wstring(dialog.x) + L"px;dialogTop:" + std::to_wstring(dialog.y) + L"px;";
    auto alive = std::make_shared<bool>(true);
    VARIANT args;
    to_variant(dialog.data, args, call, alive);
    const HRESULT hr = show(owner, moniker, HTMLDLG_MODAL, &args, features.data(), nullptr);
    *alive = false;
    VariantClear(&args);
    moniker->Release();
    DeleteFileW(path.c_str());
    if (FAILED(hr)) log::warn("js", "ShowHtmlDialog: the HTML dialog failed (" + std::to_string((long)hr) + ")");
    return SUCCEEDED(hr);
}

}
