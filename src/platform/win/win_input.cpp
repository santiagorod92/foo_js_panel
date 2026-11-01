#include "win_input.h"
#include <algorithm>

namespace jsp::win {

namespace {

enum { IDC_PROMPT = 100, IDC_VALUE };
constexpr wchar_t kClass[] = L"foo_js_panel_input";

std::wstring widen(const std::string& s) { return std::wstring(pfc::stringcvt::string_wide_from_utf8(s.c_str())); }
std::string narrow(const std::wstring& s) { return std::string(pfc::stringcvt::string_utf8_from_wide(s.c_str())); }

struct State {
    bool done = false, ok = false;
    std::wstring value;
};

LRESULT CALLBACK proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* st = reinterpret_cast<State*>(GetWindowLongPtrW(wnd, GWLP_USERDATA));
    switch (msg) {
    case WM_COMMAND:
        if (!st) break;
        if (LOWORD(wp) == IDOK) {
            HWND ed = GetDlgItem(wnd, IDC_VALUE);
            const int n = GetWindowTextLengthW(ed);
            st->value.assign((size_t)n + 1, L'\0');
            GetWindowTextW(ed, &st->value[0], n + 1);
            st->value.resize((size_t)n);
            st->ok = st->done = true;
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL) {
            st->done = true;
            return 0;
        }
        break;
    case WM_CLOSE:
        if (st) st->done = true;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

void register_class() {
    static bool done = false;
    if (done) return;
    done = true;
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = core_api::get_my_instance();
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
}

}

bool input_box(HWND owner, const std::string& caption, const std::string& prompt, std::string& value) {
    register_class();
    HWND root = owner ? GetAncestor(owner, GA_ROOT) : nullptr;
    HDC screen = GetDC(nullptr);
    const int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    auto px = [dpi](int v) { return MulDiv(v, dpi, 96); };
    NONCLIENTMETRICSW ncm{ sizeof ncm };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    HFONT font = CreateFontIndirectW(&ncm.lfMessageFont);
    const std::wstring wprompt = widen(prompt);
    const int width = px(400), pad = px(12), btnW = px(84), btnH = px(26), editH = px(24);
    RECT text{ 0, 0, width - 2 * pad, 0 };
    HGDIOBJ old = SelectObject(screen, font);
    DrawTextW(screen, wprompt.c_str(), -1, &text, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(screen, old);
    ReleaseDC(nullptr, screen);
    const int promptH = std::max<int>(text.bottom, px(16));
    const int height = pad + promptH + pad / 2 + editH + pad + btnH + pad;
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    RECT frame{ 0, 0, width, height };
    AdjustWindowRectEx(&frame, style, FALSE, WS_EX_DLGMODALFRAME);
    RECT parent{};
    if (root) GetWindowRect(root, &parent);
    else SystemParametersInfoW(SPI_GETWORKAREA, 0, &parent, 0);
    const int fw = frame.right - frame.left, fh = frame.bottom - frame.top;
    const int x = parent.left + (parent.right - parent.left - fw) / 2, y = parent.top + (parent.bottom - parent.top - fh) / 3;
    HWND wnd = CreateWindowExW(WS_EX_DLGMODALFRAME, kClass, widen(caption.empty() ? std::string("JS Panel") : caption).c_str(), style,
                               x, y, fw, fh, root, nullptr, core_api::get_my_instance(), nullptr);
    if (!wnd) {
        DeleteObject(font);
        return false;
    }
    State st;
    SetWindowLongPtrW(wnd, GWLP_USERDATA, (LONG_PTR)&st);
    HINSTANCE inst = core_api::get_my_instance();
    HWND label = CreateWindowExW(0, L"STATIC", wprompt.c_str(), WS_CHILD | WS_VISIBLE | SS_NOPREFIX, pad, pad, width - 2 * pad, promptH,
                                 wnd, (HMENU)(INT_PTR)IDC_PROMPT, inst, nullptr);
    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", widen(value).c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                pad, pad + promptH + pad / 2, width - 2 * pad, editH, wnd, (HMENU)(INT_PTR)IDC_VALUE, inst, nullptr);
    const int by = height - pad - btnH;
    HWND ok = CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, width - pad - 2 * btnW - px(8), by,
                              btnW, btnH, wnd, (HMENU)(INT_PTR)IDOK, inst, nullptr);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, width - pad - btnW, by, btnW, btnH, wnd,
                                  (HMENU)(INT_PTR)IDCANCEL, inst, nullptr);
    for (HWND c : { label, edit, ok, cancel }) SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    if (root) EnableWindow(root, FALSE);
    ShowWindow(wnd, SW_SHOW);
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);
    MSG msg;
    bool quit = false;
    while (!st.done) {
        const BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) {
            quit = r == 0;
            break;
        }
        if (!IsDialogMessageW(wnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (root) EnableWindow(root, TRUE);
    DestroyWindow(wnd);
    if (root) SetActiveWindow(root);
    DeleteObject(font);
    if (quit) PostQuitMessage((int)msg.wParam);
    if (st.ok) value = narrow(st.value);
    return st.ok;
}

}
