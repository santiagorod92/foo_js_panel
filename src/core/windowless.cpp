#include "windowless.h"
#include "js_api_util.h"
#include "js_panel_api.h"
#include "log.h"
#include "platform.h"
#include "settings.h"
#include <algorithm>
#include <cstring>
#include <map>

namespace jsp {

namespace {

class WindowlessSurface : public Surface {
public:
    WindowlessSurface(std::unique_ptr<Surface> owner, js_panel::js_panel_windowless_host* host, std::string name)
        : m_owner(std::move(owner)), m_host(host), m_name(std::move(name)) {}
    ~WindowlessSurface() override { detach(); }

    std::function<void(int)> on_timer;

    void detach() {
        *m_alive = false;
        m_timers.clear();
        m_host = nullptr;
    }

    void invalidate() override {
        if (m_host) m_host->invalidate(0, 0, 0, 0);
    }
    void invalidate_rect(const pui::gfx::Rect& r) override {
        if (m_host) m_host->invalidate(r.x, r.y, r.w, r.h);
    }
    void start_timer(int id, int ms) override {
        auto alive = m_alive;
        m_timers[id] = fb2k::registerTimer(std::max(ms, 1) / 1000.0, [this, alive, id] {
            if (*alive && on_timer) on_timer(id);
        });
    }
    void stop_timer(int id) override { m_timers.erase(id); }
    std::string choose_script_file() override { return m_owner->choose_script_file(); }
    int popup_menu(int x, int y, const Menu& menu) override {
        if (m_host) m_host->to_native(x, y);
        return m_owner->popup_menu(x, y, menu);
    }
    void set_cursor(int idc) override {
        if (m_host) m_host->set_cursor(idc);
    }
    bool input_box(const std::string& prompt, const std::string& caption, std::string& value) override {
        return m_owner->input_box(prompt, caption, value);
    }
    int message_box(const std::string& text, const std::string& caption, int type) override { return m_owner->message_box(text, caption, type); }
    bool visible() override { return m_host && m_host->visible(); }
    int window_state() override { return m_owner->window_state(); }
    void set_window_state(int state) override { m_owner->set_window_state(state); }
    bool fullscreen() override { return m_owner->fullscreen(); }
    void set_fullscreen(bool on) override { m_owner->set_fullscreen(on); }
    bool always_on_top() override { return m_owner->always_on_top(); }
    int drag_tracks(metadb_handle_list_cref tracks, int effects) override { return m_owner->drag_tracks(tracks, effects); }
    void set_tooltip(const std::string& text, bool active, int) override {
        if (m_host) m_host->set_tooltip(active ? text.c_str() : "");
    }
    bool ui_colour(UiColour kind, uint32_t& argb) override { return m_owner->ui_colour(kind, argb); }
    bool ui_font(int kind, UiFont& font) override { return m_owner->ui_font(kind, font); }
    bool sys_colour(int index, uint32_t& argb) override { return m_owner->sys_colour(index, argb); }
    bool pick_colour(uint32_t& argb) override { return m_owner->pick_colour(argb); }
    void set_always_on_top(bool on) override { m_owner->set_always_on_top(on); }
    void set_window_limits(int minW, int minH, int maxW, int maxH) override { m_owner->set_window_limits(minW, minH, maxW, maxH); }
    bool window_rect(pui::gfx::Rect& content, pui::gfx::Rect& work) override { return m_owner->window_rect(content, work); }
    void set_window_rect(const pui::gfx::Rect& content) override { m_owner->set_window_rect(content); }
    void set_caption(const pui::gfx::Rect& r) override { m_owner->set_caption(r); }
    bool caption_drag(bool double_click) override { return m_owner->caption_drag(double_click); }
    bool html_dialog(const HtmlDialog& dialog, const DialogCall& call) override { return m_owner->html_dialog(dialog, call); }
    void open_editor(const std::string& title, const std::string& text, EditorHooks hooks) override {
        m_owner->open_editor(title, text, std::move(hooks));
    }
    bool key_pressed(int vk) override { return m_owner->key_pressed(vk); }
    void* native() override { return m_owner->native(); }
    std::string panel_name() override { return m_name; }
    bool has_backdrop() override { return true; }

private:
    std::unique_ptr<Surface> m_owner;
    js_panel::js_panel_windowless_host* m_host;
    std::string m_name;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::map<int, fb2k::objRef> m_timers;
};

const Mouse kMouseKinds[] = { Mouse::Move,      Mouse::Leave,       Mouse::LeftDown,    Mouse::LeftUp,
                              Mouse::LeftDouble, Mouse::RightDown,  Mouse::RightUp,     Mouse::RightDouble,
                              Mouse::MiddleDown, Mouse::MiddleUp,   Mouse::MiddleDouble };

class WindowlessPanel : public js_panel::js_panel_windowless_v2 {
public:
    WindowlessPanel(const char* name, const char* script, js_panel::js_panel_windowless_host* host) {
        const std::string fixed = script ? script : "";
        const std::string path = fixed.empty() ? default_script()
                                               : resolve_script_path(fixed, ui::home_dir(),
                                                                     filesystem::g_get_native_path(core_api::get_profile_path()).c_str());
        m_surface = std::make_unique<WindowlessSurface>(make_owner_surface(host->native_window()), host, name ? name : "");
        m_surface->on_timer = [this](int id) {
            if (m_panel) m_panel->timer(id);
        };
        m_panel = std::make_unique<Panel>(*m_surface, path, [fixed](const std::string& p) {
            if (fixed.empty()) set_default_script(p);
        });
        log::info("js", std::string("windowless panel ") + (name ? name : "") + " runs " + path);
    }
    ~WindowlessPanel() { detach(); }

    void paint(t_uint32* pargb, int width, int height, double scale) override {
        if (!m_panel || !pargb || width <= 0 || height <= 0) return;
        pui::gfx::Pixels px;
        px.w = width;
        px.h = height;
        px.pargb.assign(pargb, pargb + (size_t)width * height);
        {
            auto canvas = pui::gfx::pixels_canvas(px, scale > 0 ? scale : 1.0);
            if (!canvas) return;
            log::guarded("js", "windowless paint", [&] { m_panel->paint(*canvas); });
        }
        std::copy(px.pargb.begin(), px.pargb.end(), pargb);
    }
    void resize(int width, int height) override {
        if (m_panel) m_panel->resize(width, height);
    }
    void mouse(int kind, int x, int y, unsigned mask) override {
        if (!m_panel || kind < 0 || kind >= (int)std::size(kMouseKinds)) return;
        if ((kind == mouse_left_down || kind == mouse_left_double) && m_surface->caption_drag(kind == mouse_left_double)) return;
        log::guarded("js", "windowless mouse", [&] { m_panel->mouse(kMouseKinds[kind], x, y, mask); });
    }
    void wheel(int step, bool horizontal) override {
        if (m_panel) log::guarded("js", "windowless wheel", [&] { m_panel->wheel(step, horizontal); });
    }
    bool key(const void* native_event) override {
        bool handled = false;
        if (m_panel && native_event) log::guarded("js", "windowless key", [&] { handled = windowless_key(*m_panel, native_event); });
        return handled;
    }
    void focus(bool focused) override {
        if (m_panel) m_panel->focus(focused);
    }
    int drag(int kind, int x, int y, unsigned keys, int effects, const void* native_data) override {
        static const Drag kinds[] = { Drag::Enter, Drag::Over, Drag::Leave, Drag::Drop };
        if (!m_panel || kind < 0 || kind >= (int)std::size(kinds)) return 0;
        int effect = 0;
        log::guarded("js", "windowless drag", [&] {
            effect = windowless_drag(*m_panel, m_dropAllowed, kinds[kind], x, y, keys, effects, native_data, m_surface->native());
        });
        return effect;
    }
    void detach() override {
        if (m_surface) m_surface->detach();
        m_panel.reset();
        m_surface.reset();
    }

private:
    std::unique_ptr<WindowlessSurface> m_surface;
    std::unique_ptr<Panel> m_panel;
    int m_dropAllowed = 0;
};

class WindowlessApi : public js_panel::js_panel_windowless_api {
public:
    service_ptr_t<js_panel::js_panel_windowless> create(const char* name, const char* script,
                                                        js_panel::js_panel_windowless_host* host) override {
        if (!host) return nullptr;
        service_ptr_t<js_panel::js_panel_windowless> out;
        log::guarded("js", "windowless create", [&] { out = fb2k::service_new<WindowlessPanel>(name, script, host); });
        return out;
    }
};
FB2K_SERVICE_FACTORY(WindowlessApi);

}

}
