#include "win_editor.h"
#include "../../core/js_engine.h"
#include <algorithm>
#include <memory>

namespace jsp::win {

namespace {

enum { IDC_CODE = 100, IDC_STATUS, IDC_REVERT, IDC_APPLY, IDC_OK, IDC_CANCEL, IDC_GUTTER };
enum { kTimerCheck = 1 };
constexpr wchar_t kClass[] = L"foo_js_panel_editor";
constexpr char kKeys[] = "   ·   Ctrl+S: apply   Ctrl+Enter: apply and close   F8: go to the error   Esc: close";

std::wstring widen(const std::string& s) { return std::wstring(pfc::stringcvt::string_wide_from_utf8(s.c_str())); }
std::string narrow(const std::wstring& s) { return std::string(pfc::stringcvt::string_utf8_from_wide(s.c_str())); }

std::wstring to_crlf(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + s.size() / 16);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\n' && (i == 0 || s[i - 1] != L'\r')) out += L'\r';
        out += s[i];
    }
    return out;
}

std::string from_crlf(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s)
        if (c != '\r') out += c;
    return out;
}

struct State {
    EditorHooks hooks;
    std::wstring original;
    std::string syntax;
    std::string applied;
    HFONT code = nullptr;
    HFONT ui = nullptr;
    WNDPROC editProc = nullptr;
    int lineHeight = 16;
};

State* state(HWND wnd) { return reinterpret_cast<State*>(GetWindowLongPtrW(wnd, GWLP_USERDATA)); }

std::wstring edit_text(HWND ed) {
    const int n = GetWindowTextLengthW(ed);
    std::wstring w((size_t)n + 1, L'\0');
    GetWindowTextW(ed, &w[0], n + 1);
    w.resize((size_t)n);
    return w;
}

std::string source(HWND wnd) { return from_crlf(narrow(edit_text(GetDlgItem(wnd, IDC_CODE)))); }

void update_status(HWND wnd) {
    State* st = state(wnd);
    if (!st) return;
    HWND ed = GetDlgItem(wnd, IDC_CODE);
    DWORD selStart = 0;
    SendMessageW(ed, EM_GETSEL, (WPARAM)&selStart, 0);
    const std::wstring w = edit_text(ed);
    const std::string before = from_crlf(narrow(w.substr(0, std::min<size_t>(selStart, w.size()))));
    const std::string text = editor_status(text_pos(before, before.size()), st->syntax, st->applied) + kKeys;
    SetWindowTextW(GetDlgItem(wnd, IDC_STATUS), widen(text).c_str());
}

void layout(HWND wnd);

void recheck(HWND wnd) {
    State* st = state(wnd);
    if (!st || !st->hooks.check) return;
    st->syntax = st->hooks.check(source(wnd));
    update_status(wnd);
    layout(wnd);
    InvalidateRect(GetDlgItem(wnd, IDC_GUTTER), nullptr, FALSE);
}

bool apply(HWND wnd) {
    State* st = state(wnd);
    if (!st || !st->hooks.apply) return false;
    st->applied = st->hooks.apply(source(wnd));
    recheck(wnd);
    return st->applied.rfind("Applied", 0) == 0;
}

void goto_line(HWND wnd, int line) {
    HWND ed = GetDlgItem(wnd, IDC_CODE);
    const LRESULT at = SendMessageW(ed, EM_LINEINDEX, (WPARAM)std::max(0, line - 1), 0);
    if (at < 0) return;
    SendMessageW(ed, EM_SETSEL, (WPARAM)at, (LPARAM)at);
    SendMessageW(ed, EM_SCROLLCARET, 0, 0);
    SetFocus(ed);
    InvalidateRect(GetDlgItem(wnd, IDC_GUTTER), nullptr, TRUE);
    update_status(wnd);
}

void goto_error(HWND wnd) {
    State* st = state(wnd);
    if (!st) return;
    const int line = st->syntax.empty() ? st->hooks.line : error_line(st->syntax, "");
    if (line > 0) goto_line(wnd, line);
}

int gutter_width(HWND wnd) {
    State* st = state(wnd);
    const LRESULT lines = SendMessageW(GetDlgItem(wnd, IDC_CODE), EM_GETLINECOUNT, 0, 0);
    int digits = 3;
    for (LRESULT n = lines; n >= 1000; n /= 10) ++digits;
    HDC dc = GetDC(wnd);
    HGDIOBJ old = SelectObject(dc, st ? st->code : nullptr);
    SIZE sz{};
    GetTextExtentPoint32W(dc, L"0", 1, &sz);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    if (st) st->lineHeight = tm.tmHeight;
    SelectObject(dc, old);
    ReleaseDC(wnd, dc);
    return sz.cx * digits + 14;
}

void draw_gutter(HWND wnd, const DRAWITEMSTRUCT& di) {
    State* st = state(wnd);
    HWND ed = GetDlgItem(wnd, IDC_CODE);
    FillRect(di.hDC, &di.rcItem, GetSysColorBrush(COLOR_BTNFACE));
    if (!st) return;
    const int first = (int)SendMessageW(ed, EM_GETFIRSTVISIBLELINE, 0, 0);
    const int count = (int)SendMessageW(ed, EM_GETLINECOUNT, 0, 0);
    const int error = st->syntax.empty() ? 0 : error_line(st->syntax, "");
    RECT client{};
    GetClientRect(ed, &client);
    POINT origin{ 0, 0 };
    const LRESULT pos = SendMessageW(ed, EM_POSFROMCHAR, (WPARAM)SendMessageW(ed, EM_LINEINDEX, (WPARAM)first, 0), 0);
    const int top = pos >= 0 ? (short)HIWORD(pos) : 0;
    MapWindowPoints(ed, di.hwndItem, &origin, 1);
    HGDIOBJ old = SelectObject(di.hDC, st->code);
    SetBkMode(di.hDC, TRANSPARENT);
    for (int line = first; line < count; ++line) {
        const int y = origin.y + top + (line - first) * st->lineHeight;
        if (y > di.rcItem.bottom) break;
        RECT r{ 0, y, di.rcItem.right - 6, y + st->lineHeight };
        SetTextColor(di.hDC, line + 1 == error ? RGB(200, 30, 30) : GetSysColor(COLOR_GRAYTEXT));
        const std::wstring n = std::to_wstring(line + 1);
        DrawTextW(di.hDC, n.c_str(), (int)n.size(), &r, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
    }
    SelectObject(di.hDC, old);
}

void layout(HWND wnd) {
    RECT rc{};
    GetClientRect(wnd, &rc);
    const int w = rc.right, h = rc.bottom, bw = 84, bh = 28, pad = 8, sh = 20, gw = gutter_width(wnd);
    MoveWindow(GetDlgItem(wnd, IDC_GUTTER), pad, pad, gw, h - 4 * pad - bh - sh, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_CODE), pad + gw, pad, w - 2 * pad - gw, h - 4 * pad - bh - sh, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_STATUS), pad, h - 2 * pad - bh - sh, w - 2 * pad, sh, TRUE);
    int x = w - pad - bw;
    const int y = h - pad - bh;
    MoveWindow(GetDlgItem(wnd, IDC_CANCEL), x, y, bw, bh, TRUE);
    x -= bw + pad;
    MoveWindow(GetDlgItem(wnd, IDC_OK), x, y, bw, bh, TRUE);
    x -= bw + pad;
    MoveWindow(GetDlgItem(wnd, IDC_APPLY), x, y, bw, bh, TRUE);
    MoveWindow(GetDlgItem(wnd, IDC_REVERT), pad, y, bw, bh, TRUE);
}

LRESULT CALLBACK code_proc(HWND ed, UINT msg, WPARAM wp, LPARAM lp) {
    HWND wnd = GetParent(ed);
    State* st = state(wnd);
    if (msg == WM_KEYDOWN && st) {
        const bool ctrl = GetKeyState(VK_CONTROL) < 0;
        if (ctrl && wp == 'S') { apply(wnd); return 0; }
        if (ctrl && wp == VK_RETURN) { if (apply(wnd)) DestroyWindow(wnd); return 0; }
        if (ctrl && wp == 'A') { SendMessageW(ed, EM_SETSEL, 0, -1); return 0; }
        if (wp == VK_ESCAPE) { DestroyWindow(wnd); return 0; }
        if (wp == VK_F8) { goto_error(wnd); return 0; }
    }
    if (msg == WM_CHAR && GetKeyState(VK_CONTROL) < 0 && (wp == 0x13 || wp == 0x01 || wp == '\n')) return 0;
    const LRESULT r = CallWindowProcW(st ? st->editProc : DefWindowProcW, ed, msg, wp, lp);
    if (msg == WM_KEYUP || msg == WM_LBUTTONUP) update_status(wnd);
    if (msg == WM_VSCROLL || msg == WM_MOUSEWHEEL || msg == WM_KEYDOWN || msg == WM_CHAR || msg == WM_LBUTTONUP || msg == WM_MOUSEMOVE)
        InvalidateRect(GetDlgItem(wnd, IDC_GUTTER), nullptr, FALSE);
    return r;
}

HWND child(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, HFONT font) {
    HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent, (HMENU)(INT_PTR)id,
                             core_api::get_my_instance(), nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
    return h;
}

LRESULT CALLBACK proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* st = state(wnd);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(wnd, GWLP_USERDATA, (LONG_PTR) reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        break;
    case WM_CREATE: {
        st = state(wnd);
        st->code = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                               FIXED_PITCH | FF_MODERN, L"Consolas");
        NONCLIENTMETRICSW ncm{ sizeof ncm };
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        st->ui = CreateFontIndirectW(&ncm.lfMessageFont);
        HWND ed = child(wnd, L"EDIT", L"", WS_BORDER | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                                               ES_AUTOHSCROLL | ES_WANTRETURN | ES_NOHIDESEL, IDC_CODE, st->code);
        SendMessageW(ed, EM_SETLIMITTEXT, 0x7FFFFFFE, 0);
        const int tabStops = 16;
        SendMessageW(ed, EM_SETTABSTOPS, 1, (LPARAM)&tabStops);
        SetWindowTextW(ed, st->original.c_str());
        st->editProc = (WNDPROC)SetWindowLongPtrW(ed, GWLP_WNDPROC, (LONG_PTR)code_proc);
        child(wnd, L"STATIC", L"", SS_OWNERDRAW, IDC_GUTTER, st->code);
        child(wnd, L"STATIC", L"", SS_LEFTNOWORDWRAP | SS_ENDELLIPSIS, IDC_STATUS, st->ui);
        child(wnd, L"BUTTON", L"Revert", BS_PUSHBUTTON, IDC_REVERT, st->ui);
        child(wnd, L"BUTTON", L"Apply", BS_PUSHBUTTON, IDC_APPLY, st->ui);
        child(wnd, L"BUTTON", L"OK", BS_DEFPUSHBUTTON, IDC_OK, st->ui);
        child(wnd, L"BUTTON", L"Cancel", BS_PUSHBUTTON, IDC_CANCEL, st->ui);
        SetFocus(ed);
        recheck(wnd);
        layout(wnd);
        if (st->hooks.line > 0) goto_line(wnd, st->hooks.line);
        return 0;
    }
    case WM_DRAWITEM:
        if (wp == IDC_GUTTER) {
            draw_gutter(wnd, *reinterpret_cast<const DRAWITEMSTRUCT*>(lp));
            return TRUE;
        }
        break;
    case WM_SIZE:
        layout(wnd);
        return 0;
    case WM_TIMER:
        if (wp == kTimerCheck) {
            KillTimer(wnd, kTimerCheck);
            recheck(wnd);
        }
        return 0;
    case WM_COMMAND: {
        const int id = LOWORD(wp);
        if (id == IDC_CODE && HIWORD(wp) == EN_CHANGE) {
            SetTimer(wnd, kTimerCheck, 400, nullptr);
            InvalidateRect(GetDlgItem(wnd, IDC_GUTTER), nullptr, FALSE);
            return 0;
        }
        if (id == IDC_CODE && (HIWORD(wp) == EN_VSCROLL || HIWORD(wp) == EN_UPDATE)) {
            InvalidateRect(GetDlgItem(wnd, IDC_GUTTER), nullptr, FALSE);
            return 0;
        }
        if (id == IDC_APPLY || id == IDC_OK) {
            if (apply(wnd) && id == IDC_OK) DestroyWindow(wnd);
            return 0;
        }
        if (id == IDC_REVERT && st) {
            SetWindowTextW(GetDlgItem(wnd, IDC_CODE), st->original.c_str());
            apply(wnd);
            return 0;
        }
        if (id == IDC_CANCEL) {
            DestroyWindow(wnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(wnd);
        return 0;
    case WM_NCDESTROY:
        if (st) {
            if (st->code) DeleteObject(st->code);
            if (st->ui) DeleteObject(st->ui);
            delete st;
            SetWindowLongPtrW(wnd, GWLP_USERDATA, 0);
        }
        return 0;
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

}

HWND open_editor(HWND existing, const std::string& title, const std::string& text, EditorHooks hooks) {
    if (existing && IsWindow(existing)) {
        ShowWindow(existing, SW_RESTORE);
        SetForegroundWindow(existing);
        return existing;
    }
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = proc;
        wc.hInstance = core_api::get_my_instance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = kClass;
        registered = RegisterClassW(&wc) != 0;
    }
    auto* st = new State{ std::move(hooks), to_crlf(widen(text)) };
    HWND wnd = CreateWindowExW(0, kClass, widen(title).c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 900, 680,
                               (HWND)core_api::get_main_window(), nullptr, core_api::get_my_instance(), st);
    if (!wnd) {
        delete st;
        return nullptr;
    }
    ShowWindow(wnd, SW_SHOW);
    return wnd;
}

void close_editor(HWND editor) {
    if (editor && IsWindow(editor)) DestroyWindow(editor);
}

}
