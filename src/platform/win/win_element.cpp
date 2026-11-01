#include "win_sdk.h"
#include <commdlg.h>
#include <windowsx.h>
#include "platform/win/gdi_canvas.h"
#include "../../core/js_panel_api.h"
#include "../../core/log.h"
#include "../../core/panel.h"
#include "../../core/settings.h"
#include "win_editor.h"
#include "win_input.h"
#include <memory>

namespace jsp {

namespace {

const GUID& kElementGuid = js_panel::element_guid;
constexpr wchar_t kClassName[] = L"foo_js_panel_view";

std::wstring widen(const std::string& s) { return std::wstring(pfc::stringcvt::string_wide_from_utf8(s.c_str())); }
std::string narrow(const wchar_t* s) { return std::string(pfc::stringcvt::string_utf8_from_wide(s)); }

std::string config_path(ui_element_config::ptr cfg) {
    if (cfg.is_empty() || cfg->get_guid() != kElementGuid || cfg->get_data_size() == 0) return {};
    return std::string(static_cast<const char*>(cfg->get_data()), cfg->get_data_size());
}

ui_element_config::ptr make_config(const std::string& path) { return js_panel::make_config(path.c_str()); }

class WinSurface : public Surface {
public:
    HWND hwnd = nullptr;
    HWND editor = nullptr;

    bool key_pressed(int vk) override { return GetKeyState(vk) < 0; }
    void open_editor(const std::string& title, const std::string& text, EditorHooks hooks) override {
        editor = win::open_editor(editor, title, text, std::move(hooks));
    }
    void close_editor() {
        win::close_editor(editor);
        editor = nullptr;
    }

    void invalidate() override {
        if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
    }
    void invalidate_rect(const pui::gfx::Rect& r) override {
        if (!hwnd) return;
        RECT rc{ r.x, r.y, r.right(), r.bottom() };
        InvalidateRect(hwnd, &rc, FALSE);
    }
    void start_timer(int id, int ms) override {
        if (hwnd) SetTimer(hwnd, (UINT_PTR)id, (UINT)ms, nullptr);
    }
    void stop_timer(int id) override {
        if (hwnd) KillTimer(hwnd, (UINT_PTR)id);
    }
    std::string choose_script_file() override {
        wchar_t file[MAX_PATH * 4] = {};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof ofn;
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = L"JavaScript (*.js)\0*.js\0All files\0*.*\0";
        ofn.lpstrFile = file;
        ofn.nMaxFile = (DWORD)std::size(file);
        ofn.lpstrTitle = L"Choose a JS Panel script";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
        return GetOpenFileNameW(&ofn) ? narrow(file) : std::string();
    }
    static HMENU build_menu(const Menu& items) {
        HMENU menu = CreatePopupMenu();
        UINT pos = 0;
        for (const MenuItem& it : items) {
            std::wstring text = widen(it.text);
            MENUITEMINFOW mi{};
            mi.cbSize = sizeof mi;
            mi.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID;
            mi.fType = it.separator ? MFT_SEPARATOR : it.radio ? MFT_RADIOCHECK : MFT_STRING;
            mi.fState = (it.checked ? MFS_CHECKED : 0u) | (it.disabled ? MFS_DISABLED : 0u);
            mi.wID = (UINT)it.id;
            if (!it.separator) {
                mi.fMask |= MIIM_STRING;
                mi.dwTypeData = text.data();
            }
            if (it.submenu) {
                mi.fMask |= MIIM_SUBMENU;
                mi.hSubMenu = build_menu(it.children);
            }
            InsertMenuItemW(menu, pos++, TRUE, &mi);
        }
        return menu;
    }
    int popup_menu(int x, int y, const Menu& items) override {
        HMENU menu = build_menu(items);
        POINT pt{ x, y };
        ClientToScreen(hwnd, &pt);
        const int cmd = (int)TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
        PostMessageW(hwnd, WM_NULL, 0, 0);
        DestroyMenu(menu);
        return cmd;
    }
    HCURSOR cursor = nullptr;
    void set_cursor(int idc) override {
        cursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(idc));
        if (cursor) SetCursor(cursor);
    }
    bool input_box(const std::string& prompt, const std::string& caption, std::string& value) override {
        return win::input_box(hwnd, caption, prompt, value);
    }
    HWND root() const { return hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr; }
    bool visible() override { return hwnd && IsWindowVisible(hwnd); }
    int window_state() override {
        HWND r = root();
        return !r ? 0 : IsIconic(r) ? 1 : IsZoomed(r) ? 2 : 0;
    }
    void set_window_state(int state) override {
        if (HWND r = root()) ShowWindow(r, state == 1 ? SW_MINIMIZE : state == 2 ? SW_MAXIMIZE : SW_RESTORE);
    }
    bool full = false;
    WINDOWPLACEMENT saved{};
    LONG_PTR savedStyle = 0;
    bool fullscreen() override { return full; }
    void set_fullscreen(bool on) override {
        HWND r = root();
        if (!r || on == full) return;
        if (on) {
            saved.length = sizeof saved;
            GetWindowPlacement(r, &saved);
            savedStyle = GetWindowLongPtrW(r, GWL_STYLE);
            MONITORINFO mi{ sizeof mi };
            GetMonitorInfoW(MonitorFromWindow(r, MONITOR_DEFAULTTONEAREST), &mi);
            SetWindowLongPtrW(r, GWL_STYLE, savedStyle & ~(LONG_PTR)(WS_CAPTION | WS_THICKFRAME));
            SetWindowPos(r, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                         mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        } else {
            SetWindowLongPtrW(r, GWL_STYLE, savedStyle);
            SetWindowPlacement(r, &saved);
            SetWindowPos(r, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        }
        full = on;
    }
    void set_window_limits(int minW, int minH, int maxW, int maxH) override {
        HWND r = root();
        if (!r || IsIconic(r) || IsZoomed(r) || full) return;
        RECT rc, client;
        GetWindowRect(r, &rc);
        GetClientRect(r, &client);
        const int cw = client.right - client.left, ch = client.bottom - client.top;
        int w = cw, h = ch;
        if (minW > 0) w = std::max(w, minW);
        if (minH > 0) h = std::max(h, minH);
        if (maxW > 0) w = std::min(w, maxW);
        if (maxH > 0) h = std::min(h, maxH);
        if (w != cw || h != ch)
            SetWindowPos(r, nullptr, 0, 0, rc.right - rc.left + w - cw, rc.bottom - rc.top + h - ch, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    pui::gfx::Rect caption{};
    void set_caption(const pui::gfx::Rect& r) override { caption = r; }
    bool in_caption(int x, int y) {
        HWND r = root();
        if (!r || caption.w <= 0 || caption.h <= 0 || full) return false;
        POINT p{ x, y };
        MapWindowPoints(hwnd, r, &p, 1);
        return p.x >= caption.x && p.x < caption.x + caption.w && p.y >= caption.y && p.y < caption.y + caption.h;
    }
    void drag_window() {
        HWND r = root();
        POINT p;
        GetCursorPos(&p);
        SendMessageW(r, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(p.x, p.y));
    }
    int message_box(const std::string& text, const std::string& caption, int type) override {
        return MessageBoxW(hwnd ? GetAncestor(hwnd, GA_ROOT) : nullptr, widen(text).c_str(), widen(caption).c_str(), (UINT)(type & 0xFFFF));
    }
};

class JsElementInstance : public ui_element_instance {
public:
    JsElementInstance(HWND parent, ui_element_config::ptr cfg, ui_element_instance_callback_ptr callback)
        : m_callback(callback), m_path(config_path(cfg)) {
        register_class();
        CreateWindowExW(0, kClassName, L"JS Panel", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 0, 0,
                        parent, nullptr, core_api::get_my_instance(), this);
    }
    ~JsElementInstance() {
        if (m_surface.hwnd) DestroyWindow(m_surface.hwnd);
    }

    fb2k::hwnd_t get_wnd() override { return m_surface.hwnd; }
    void set_configuration(ui_element_config::ptr cfg) override {
        m_path = config_path(cfg);
        if (m_panel) m_panel->set_script(m_path);
    }
    ui_element_config::ptr get_configuration() override { return make_config(m_path); }
    GUID get_guid() override { return kElementGuid; }
    GUID get_subclass() override { return ui_element_subclass_utility; }

private:
    static void register_class() {
        static bool done = false;
        if (done) return;
        done = true;
        WNDCLASSW wc{};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &JsElementInstance::proc;
        wc.hInstance = core_api::get_my_instance();
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassW(&wc);
    }

    static LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_NCCREATE) {
            auto* self = static_cast<JsElementInstance*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)self);
            self->m_surface.hwnd = hwnd;
        }
        auto* self = reinterpret_cast<JsElementInstance*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return DefWindowProcW(hwnd, msg, wp, lp);
        LRESULT result = 0;
        if (log::guarded("js", "window message", [&] { result = self->handle(msg, wp, lp); })) return result;
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp) {
        HWND hwnd = m_surface.hwnd;
        const int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        const unsigned mask = (unsigned)(wp & 0xffff);
        switch (msg) {
        case WM_CREATE:
            m_panel = std::make_unique<Panel>(m_surface, m_path.empty() ? default_script() : m_path,
                                              [this](const std::string& p) { m_path = p; });
            return 0;
        case WM_DESTROY:
            m_surface.close_editor();
            m_panel.reset();
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            m_surface.hwnd = nullptr;
            return DefWindowProcW(hwnd, msg, wp, lp);
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (m_panel) m_panel->resize(LOWORD(lp), HIWORD(lp));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int w = rc.right, h = rc.bottom;
            if (w > 0 && h > 0 && m_panel) {
                HDC mem = CreateCompatibleDC(dc);
                HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
                HGDIOBJ old = SelectObject(mem, bmp);
                {
                    pui::gfx::GdiCanvas canvas(mem, w, h);
                    m_panel->paint(canvas);
                }
                BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
                SelectObject(mem, old);
                DeleteObject(bmp);
                DeleteDC(mem);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (m_panel) m_panel->timer((int)wp);
            return 0;
        case WM_MOUSEMOVE:
            if (!m_tracking) {
                TRACKMOUSEEVENT tme{ sizeof tme, TME_LEAVE, hwnd, 0 };
                m_tracking = TrackMouseEvent(&tme) != FALSE;
            }
            if (m_panel) m_panel->mouse(Mouse::Move, x, y, mask);
            return 0;
        case WM_MOUSELEAVE:
            m_tracking = false;
            if (m_panel) m_panel->mouse(Mouse::Leave, 0, 0, 0);
            return 0;
        case WM_LBUTTONDOWN:
            if (m_surface.in_caption(x, y)) {
                m_surface.drag_window();
                return 0;
            }
            SetFocus(hwnd);
            SetCapture(hwnd);
            if (m_panel) m_panel->mouse(Mouse::LeftDown, x, y, mask);
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == hwnd) ReleaseCapture();
            if (m_panel) m_panel->mouse(Mouse::LeftUp, x, y, mask);
            return 0;
        case WM_LBUTTONDBLCLK:
            if (m_surface.in_caption(x, y)) {
                m_surface.set_window_state(m_surface.window_state() == 2 ? 0 : 2);
                return 0;
            }
            if (m_panel) m_panel->mouse(Mouse::LeftDouble, x, y, mask);
            return 0;
        case WM_RBUTTONDOWN:
            SetFocus(hwnd);
            if (m_panel) m_panel->mouse(Mouse::RightDown, x, y, mask);
            return 0;
        case WM_MBUTTONDOWN:
            SetFocus(hwnd);
            if (m_panel) m_panel->mouse(Mouse::MiddleDown, x, y, mask);
            return 0;
        case WM_MBUTTONUP:
            if (m_panel) m_panel->mouse(Mouse::MiddleUp, x, y, mask);
            return 0;
        case WM_MBUTTONDBLCLK:
            if (m_panel) m_panel->mouse(Mouse::MiddleDouble, x, y, mask);
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && m_surface.cursor) {
                SetCursor(m_surface.cursor);
                return TRUE;
            }
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            if (m_panel) m_panel->focus(msg == WM_SETFOCUS);
            return 0;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (m_panel && m_panel->key_up((int)wp)) return 0;
            break;
        case WM_CHAR: {
            const wchar_t c = (wchar_t)wp;
            if (c >= 0xD800 && c <= 0xDBFF) {
                m_highSurrogate = c;
                return 0;
            }
            unsigned cp = c;
            if (c >= 0xDC00 && c <= 0xDFFF && m_highSurrogate) cp = 0x10000 + ((m_highSurrogate - 0xD800u) << 10) + (c - 0xDC00u);
            m_highSurrogate = 0;
            if (m_panel) m_panel->char_input(cp);
            return 0;
        }
        case WM_RBUTTONUP:
            if (m_callback.is_valid() && m_callback->is_edit_mode_enabled()) break;
            if (m_panel) m_panel->mouse(Mouse::RightUp, x, y, mask);
            return 0;
        case WM_CONTEXTMENU:
            if (m_callback.is_valid() && m_callback->is_edit_mode_enabled()) break;
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (m_panel && m_panel->key_down((int)wp)) return 0;
            if (keyboard_shortcut_manager::get()->on_keydown_auto(wp)) return 0;
            break;
        case WM_MOUSEWHEEL:
            if (m_panel) m_panel->wheel(GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    ui_element_instance_callback_ptr m_callback;
    std::string m_path;
    WinSurface m_surface;
    std::unique_ptr<Panel> m_panel;
    bool m_tracking = false;
    wchar_t m_highSurrogate = 0;
};

class JsElement : public ui_element {
public:
    GUID get_guid() override { return kElementGuid; }
    GUID get_subclass() override { return ui_element_subclass_utility; }
    void get_name(pfc::string_base& out) override { out = js_panel::element_name; }
    bool get_description(pfc::string_base& out) override {
        out = "A panel drawn by a JavaScript file (Spider Monkey Panel compatible API).";
        return true;
    }
    ui_element_instance_ptr instantiate(fb2k::hwnd_t parent, ui_element_config::ptr cfg,
                                        ui_element_instance_callback_ptr callback) override {
        return fb2k::service_new<JsElementInstance>(parent, cfg, callback);
    }
    ui_element_config::ptr get_default_configuration() override { return make_config(std::string()); }
    ui_element_children_enumerator_ptr enumerate_children(ui_element_config::ptr) override { return nullptr; }
};
FB2K_SERVICE_FACTORY(JsElement);

}

}
