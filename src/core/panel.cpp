#include "panel.h"
#include "bundled_js.h"
#include "fs_util.h"
#include "js_api_util.h"
#include "js_panel_api.h"
#include "log.h"
#include "navidrome_library_api.h"
#include "navidrome_rating_api.h"
#include "panels_ui_api.h"
#include "platform.h"
#include "platform_tracks.h"
#include "sample_script.h"
#include "settings.h"
#include <algorithm>
#include <filesystem>
#include <thread>
#include <unordered_set>

namespace jsp {

namespace {

enum PanelCommand { kEdit = 1, kChoose, kReload, kSample, kSamples = 100 };
constexpr int kWatchTimer = 0x7FFF0001;
constexpr int kWatchMs = 1000;

long long modified(const std::string& path) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(fs_path(path), ec);
    return ec ? -1 : (long long)t.time_since_epoch().count();
}

std::string label(const std::string& path) { return path.empty() ? std::string("built-in sample") : path; }

struct SdkTrack : Track {
    metadb_handle_ptr h;
    explicit SdkTrack(metadb_handle_ptr handle) : h(std::move(handle)) {}
};

metadb_handle_ptr handle_of(const TrackPtr& t) {
    auto* s = dynamic_cast<SdkTrack*>(t.get());
    return s ? s->h : metadb_handle_ptr();
}

TrackPtr wrap(const metadb_handle_ptr& h) { return h.is_valid() ? std::make_shared<SdkTrack>(h) : nullptr; }

pfc::list_t<metadb_handle_ptr> handles_of(const Tracks& tracks) {
    pfc::list_t<metadb_handle_ptr> out;
    for (const auto& t : tracks)
        if (auto h = handle_of(t); h.is_valid()) out.add_item(h);
    return out;
}

GUID art_guid(int artId) {
    switch (artId) {
    case 1: return album_art_ids::cover_back;
    case 2: return album_art_ids::disc;
    case 3: return album_art_ids::icon;
    case 4: return album_art_ids::artist;
    default: return album_art_ids::cover_front;
    }
}

pui::gfx::ImagePtr decode(const album_art_data::ptr& data) {
    return data.is_valid() && data->size() ? pui::gfx::decode_image_memory(data->data(), data->size()) : nullptr;
}

struct NavidromeLibrary {
    enum class State { Idle, Loading, Ready, Failed } state = State::Idle;
    metadb_handle_list tracks;
    std::unordered_set<const metadb_handle*> members;
};

NavidromeLibrary& navidrome_library() {
    static NavidromeLibrary lib;
    return lib;
}

struct TrackCollector : navidrome::library_track_sink {
    metadb_handle_list tracks;
    void on_track(const metadb_handle_ptr& track) override { tracks.add_item(track); }
};

navidrome::navidrome_library_api::ptr navidrome_api() {
    service_enum_t<navidrome::navidrome_library_api> e;
    navidrome::navidrome_library_api::ptr api;
    e.next(api);
    return api;
}

struct AlbumCollector : navidrome::library_album_sink, navidrome::library_album_sink_v2 {
    std::vector<LibraryAlbum> albums;
    static std::string s(const char* p) { return p ? p : ""; }
    void on_album(const char* albumId, const char* albumName, const char* artistName, const char* artistId,
                  const char* coverArtId, int year, int songCount) override {
        on_album(albumId, albumName, artistName, artistId, coverArtId, year, songCount, nullptr);
    }
    void on_album(const char* albumId, const char* albumName, const char* artistName, const char* artistId,
                  const char* coverArtId, int year, int songCount, const char* created) override {
        albums.push_back({ s(albumId), s(albumName), s(artistName), s(artistId), s(coverArtId), s(created), year, songCount });
    }
};

Tracks wrap_all(metadb_handle_list_cref items) {
    Tracks out;
    out.reserve(items.get_count());
    for (size_t i = 0; i < items.get_count(); ++i) out.push_back(wrap(items[i]));
    return out;
}

int index_or_none(size_t i) { return i == pfc_infinite ? -1 : (int)i; }

std::string native_dir(const char* path) {
    std::string p = filesystem::g_get_native_path(path).c_str();
    if (!p.empty() && p.back() != kPathSep) p += kPathSep;
    return p;
}

std::string properties_file(const std::string& scriptPath) {
    unsigned long long h = 1469598103934665603ull;
    for (unsigned char c : scriptPath) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char name[40];
    snprintf(name, sizeof name, "%016llx.json", h);
    return native_dir(core_api::pathInProfile("foo_js_panel")) + "properties" + kPathSep + (scriptPath.empty() ? std::string("sample.json") : name);
}

std::string strip_accel(const char* s) {
    std::string out;
    for (; *s; ++s)
        if (*s != '&' || s[1] == '&') out += *s;
    return out;
}

bool same_text(const std::string& a, const std::string& b) { return stricmp_utf8(a.c_str(), b.c_str()) == 0; }

std::string main_menu_prefix(GUID parent) {
    struct Group {
        GUID guid, parent;
        std::string name;
    };
    static std::vector<Group> groups;
    if (groups.empty()) {
        for (auto g : mainmenu_group::enumerate()) {
            Group e{ g->get_guid(), g->get_parent(), {} };
            mainmenu_group_popup::ptr popup;
            if (g->service_query_t(popup)) {
                pfc::string8 n;
                popup->get_display_string(n);
                e.name = strip_accel(n.c_str());
            }
            groups.push_back(std::move(e));
        }
    }
    std::string prefix;
    for (int depth = 0; depth < 16 && parent != pfc::guid_null; ++depth) {
        auto it = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.guid == parent; });
        if (it == groups.end()) break;
        if (!it->name.empty()) prefix = it->name + "/" + prefix;
        parent = it->parent;
    }
    return prefix;
}

contextmenu_node* find_context_command(contextmenu_node* node, const std::string& prefix, const std::string& path) {
    if (!node) return nullptr;
    for (size_t i = 0; i < node->get_num_children(); ++i) {
        contextmenu_node* child = node->get_child(i);
        if (!child) continue;
        const std::string name = prefix + strip_accel(child->get_name());
        if (child->get_type() == contextmenu_item_node::type_group) {
            if (auto* hit = find_context_command(child, name + "/", path)) return hit;
        } else if (child->get_type() == contextmenu_item_node::type_command && same_text(name, path)) {
            return child;
        }
    }
    return nullptr;
}

class SdkContextMenu : public MenuSource {
public:
    explicit SdkContextMenu(contextmenu_manager::ptr cm) : m_cm(std::move(cm)) {}
    Menu build(int baseId, int maxId) override {
        m_commands.clear();
        return convert(m_cm->get_root(), baseId, maxId, 0);
    }
    bool execute(int id) override {
        if (id < 0 || id >= (int)m_commands.size()) return false;
        m_commands[(size_t)id]->execute();
        return true;
    }

private:
    Menu convert(contextmenu_node* node, int base, int maxId, int depth) {
        Menu out;
        if (!node || depth > 16) return out;
        for (size_t i = 0; i < node->get_num_children(); ++i) {
            contextmenu_node* c = node->get_child(i);
            if (!c) continue;
            MenuItem m;
            m.text = c->get_name() ? c->get_name() : "";
            const unsigned f = c->get_type() == contextmenu_item_node::type_separator ? 0 : c->get_display_flags();
            m.disabled = (f & (contextmenu_item_node::FLAG_DISABLED | contextmenu_item_node::FLAG_GRAYED)) != 0;
            m.checked = (f & (contextmenu_item_node::FLAG_CHECKED | contextmenu_item_node::FLAG_RADIOCHECKED)) != 0;
            m.radio = (f & contextmenu_item_node::FLAG_RADIOCHECKED) != 0;
            switch (c->get_type()) {
            case contextmenu_item_node::type_separator: m.separator = true; break;
            case contextmenu_item_node::type_group:
                m.submenu = true;
                m.children = convert(c, base, maxId, depth + 1);
                break;
            case contextmenu_item_node::type_command:
                m.id = base + (int)m_commands.size();
                if (maxId >= 0 && m.id > maxId) continue;
                m_commands.push_back(c);
                break;
            }
            out.push_back(std::move(m));
        }
        return out;
    }

    contextmenu_manager::ptr m_cm;
    std::vector<contextmenu_node*> m_commands;
};

class SdkMainMenu : public MenuSource {
public:
    explicit SdkMainMenu(menu_tree_item::ptr root) : m_root(std::move(root)) {}
    Menu build(int baseId, int maxId) override {
        m_commands.clear();
        return convert(m_root, baseId, maxId, 0);
    }
    bool execute(int id) override {
        if (id < 0 || id >= (int)m_commands.size()) return false;
        m_commands[(size_t)id]->execute(nullptr);
        return true;
    }

private:
    Menu convert(const menu_tree_item::ptr& node, int base, int maxId, int depth) {
        Menu out;
        if (node.is_empty() || depth > 16) return out;
        for (size_t i = 0; i < node->childCount(); ++i) {
            menu_tree_item::ptr c = node->childAt(i);
            if (c.is_empty()) continue;
            MenuItem m;
            m.text = c->name() ? c->name() : "";
            const menu_flags_t f = c->flags();
            if (f & menu_flags::defaulthidden) continue;
            m.disabled = (f & menu_flags::disabled) != 0;
            m.checked = (f & (menu_flags::checked | menu_flags::radiochecked)) != 0;
            m.radio = (f & menu_flags::radiochecked) != 0;
            switch (c->type()) {
            case menu_tree_item::itemSeparator: m.separator = true; break;
            case menu_tree_item::itemSubmenu:
                m.submenu = true;
                m.children = convert(c, base, maxId, depth + 1);
                break;
            case menu_tree_item::itemCommand:
                m.id = base + (int)m_commands.size();
                if (maxId >= 0 && m.id > maxId) continue;
                m_commands.push_back(c);
                break;
            }
            out.push_back(std::move(m));
        }
        return out;
    }

    menu_tree_item::ptr m_root;
    std::vector<menu_tree_item::ptr> m_commands;
};

const GUID* main_menu_root(const std::string& name) {
    static const std::pair<const char*, const GUID*> roots[] = {
        { "File", &mainmenu_groups::file },         { "Edit", &mainmenu_groups::edit },
        { "View", &mainmenu_groups::view },         { "Playback", &mainmenu_groups::playback },
        { "Library", &mainmenu_groups::library },   { "Help", &mainmenu_groups::help },
    };
    for (const auto& [n, g] : roots)
        if (stricmp_utf8(n, name.c_str()) == 0) return g;
    return nullptr;
}

HttpResponse run_http(const HttpRequest& r, abort_callback& abort) {
    HttpResponse out;
    try {
        const bool post = r.method == "POST";
        http_request::ptr req = http_client::get()->create_request(post ? "POST" : "GET");
        std::string contentType = "application/x-www-form-urlencoded";
        for (const auto& h : r.headers) {
            std::string name = h.substr(0, h.find(':'));
            for (char& ch : name) ch = (char)std::tolower((unsigned char)ch);
            if (name == "content-type" && h.find(':') != std::string::npos) contentType = h.substr(h.find(':') + 1);
            else req->add_header(h.c_str());
        }
        while (!contentType.empty() && contentType.front() == ' ') contentType.erase(0, 1);
        if (post) {
            http_request_post_v2::ptr v2;
            if (req->service_query_t(v2)) v2->set_post_data(r.body.data(), r.body.size(), contentType.c_str());
        }
        file::ptr f = req->run_ex(r.url.c_str(), abort);
        pfc::array_t<uint8_t> data;
        f->read_till_eof(data, abort);
        out.text.assign(reinterpret_cast<const char*>(data.get_ptr()), data.get_size());
        http_reply::ptr reply;
        if (f->service_query_t(reply)) {
            pfc::string8 status;
            reply->get_status(status);
            const char* p = status.c_str();
            while (*p && !(p[0] >= '1' && p[0] <= '5' && p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9')) ++p;
            out.status = *p ? atoi(p) : 200;
        } else {
            out.status = 200;
        }
    } catch (const std::exception& e) {
        out.error = e.what();
    }
    return out;
}

std::string sample_path(const Sample& sample) {
    const std::string dir = std::string(filesystem::g_get_native_path(core_api::pathInProfile("foo_js_panel")).c_str()) + kPathSep + "samples";
    const std::string path = dir + kPathSep + sample.file;
    std::error_code ec;
    std::filesystem::create_directories(fs_path(dir), ec);
    if (std::filesystem::exists(fs_path(path), ec)) return path;
    std::string source = sample.source;
    if (!source.empty() && source[0] == '\n') source.erase(0, 1);
    if (!write_file(path, source)) {
        log::warn("js", "can't write the sample " + path);
        return {};
    }
    return path;
}

std::string new_script_path() {
    const pfc::string8 dir = filesystem::g_get_native_path(core_api::pathInProfile("foo_js_panel"));
    std::error_code ec;
    std::filesystem::create_directories(fs_path(dir.c_str()), ec);
    for (int n = 1; n < 1000; ++n) {
        const std::string path = std::string(dir.c_str()) + kPathSep + "script" + (n > 1 ? "-" + std::to_string(n) : "") + ".js";
        if (!std::filesystem::exists(fs_path(path), ec)) return path;
    }
    return {};
}

}

struct Panel::Callbacks : playlist_callback_impl_base, metadb_io_callback_dynamic_impl_base, ui_selection_callback_impl_base,
                          ui_config_callback_impl {
    Panel& p;
    explicit Callbacks(Panel& panel)
        : playlist_callback_impl_base(flag_on_items_added | flag_on_items_reordered | flag_on_items_removed |
                                      flag_on_items_selection_change | flag_on_item_focus_change | flag_on_item_ensure_visible |
                                      flag_on_playlist_activate | flag_on_playlist_created | flag_on_playlists_reorder |
                                      flag_on_playlists_removed | flag_on_playlist_renamed | flag_on_playback_order_changed |
                                      flag_on_playlist_locked),
          p(panel) {}
    void send(const char* name, std::vector<Arg> args = {}) { p.post(name, std::move(args)); }
    void on_selection_changed(metadb_handle_list_cref) override { send("on_selection_changed"); }
    void ui_colors_changed() override { send("on_colours_changed"); }
    void ui_fonts_changed() override { send("on_font_changed"); }
    void on_items_added(t_size pl, t_size, metadb_handle_list_cref, const bit_array&) override { send("on_playlist_items_added", { (int)pl }); }
    void on_items_reordered(t_size pl, const t_size*, t_size) override { send("on_playlist_items_reordered", { (int)pl }); }
    void on_items_removed(t_size pl, const bit_array&, t_size, t_size count) override { send("on_playlist_items_removed", { (int)pl, (int)count }); }
    void on_items_selection_change(t_size, const bit_array&, const bit_array&) override { send("on_playlist_items_selection_change"); }
    void on_item_focus_change(t_size pl, t_size from, t_size to) override {
        send("on_item_focus_change", { (int)pl, index_or_none(from), index_or_none(to) });
    }
    void on_item_ensure_visible(t_size pl, t_size item) override { send("on_playlist_item_ensure_visible", { (int)pl, (int)item }); }
    void on_playlist_activate(t_size, t_size) override { send("on_playlist_switch"); }
    void on_playlist_created(t_size, const char*, t_size) override { send("on_playlists_changed"); }
    void on_playlists_reorder(const t_size*, t_size) override { send("on_playlists_changed"); }
    void on_playlists_removed(const bit_array&, t_size, t_size) override { send("on_playlists_changed"); }
    void on_playlist_renamed(t_size, const char*, t_size) override { send("on_playlists_changed"); }
    void on_playlist_locked(t_size, bool) override { send("on_playlists_changed"); }
    void on_playback_order_changed(t_size order) override { send("on_playback_order_changed", { (int)order }); }
    void on_changed_sorted(metadb_handle_list_cref items, bool fromhook) override { send("on_metadb_changed", { wrap_all(items), fromhook }); }
};

std::vector<Panel*>& Panel::live() {
    static std::vector<Panel*> panels;
    return panels;
}

void Panel::broadcast(const char* name, const std::vector<Arg>& args) {
    for (Panel* p : live()) p->post(name, args);
}

void Panel::skin_message(const std::string& name, const std::string& text) {
    for (Panel* p : live())
        p->later([p, name, text]() {
            if (p->m_engine) p->m_engine->skin_message(name, text);
        });
}

void Panel::post(const char* name, std::vector<Arg> args) {
    later([this, name, args = std::move(args)]() { invoke(name, args); });
}

namespace {

class FlagNotify : public config_object_notify {
    struct Watch {
        const GUID* id;
        const char* callback;
    };
    static const Watch* watches() {
        static const Watch w[] = {
            { &standard_config_objects::bool_ui_always_on_top, "on_always_on_top_changed" },
            { &standard_config_objects::bool_playlist_stop_after_current, "on_playlist_stop_after_current_changed" },
            { &standard_config_objects::bool_cursor_follows_playback, "on_cursor_follow_playback_changed" },
            { &standard_config_objects::bool_playback_follows_cursor, "on_playback_follow_cursor_changed" },
        };
        return w;
    }

public:
    t_size get_watched_object_count() override { return 4; }
    GUID get_watched_object(t_size i) override { return *watches()[i].id; }
    void on_watched_object_changed(const service_ptr_t<config_object>& o) override {
        const GUID id = o->get_guid();
        for (t_size i = 0; i < get_watched_object_count(); ++i)
            if (*watches()[i].id == id) Panel::broadcast(watches()[i].callback, { o->get_data_bool_simple(false) });
    }
};
FB2K_SERVICE_FACTORY(FlagNotify);

class DspNotify : public dsp_config_callback {
public:
    void on_core_settings_change(const dsp_chain_config&) override { Panel::broadcast("on_dsp_preset_changed"); }
};
FB2K_SERVICE_FACTORY(DspNotify);

class QueueNotify : public playback_queue_callback {
public:
    void on_changed(t_change_origin origin) override { Panel::broadcast("on_playback_queue_changed", { (int)origin }); }
};
FB2K_SERVICE_FACTORY(QueueNotify);

class SkinNotify : public js_panel::js_panel_notify_api {
public:
    void notify(const char* name, const char* info) override { Panel::skin_message(name ? name : "", info ? info : ""); }
    void variable_changed(const char* name, const char* value) override {
        Panel::broadcast("on_panelsui_variable_changed", { std::string(name ? name : ""), std::string(value ? value : "") });
    }
};
FB2K_SERVICE_FACTORY(SkinNotify);

class StateNotify : public initquit, private replaygain_core_settings_notify {
    service_ptr m_output;

    void on_changed(const t_replaygain_config& cfg) override { Panel::broadcast("on_replaygain_mode_changed", { (int)cfg.m_source_mode }); }

public:
    void on_init() override {
        if (auto rg = replaygain_manager_v2::tryGet(); rg.is_valid()) rg->add_notify(this);
        if (auto out = output_manager_v2::tryGet(); out.is_valid())
            m_output = out->addCallback([]() { Panel::broadcast("on_output_device_changed"); });
    }
    void on_quit() override {
        if (auto rg = replaygain_manager_v2::tryGet(); rg.is_valid()) rg->remove_notify(this);
        m_output.release();
    }
};
FB2K_SERVICE_FACTORY(StateNotify);

class JsLock : public playlist_lock {
public:
    explicit JsLock(t_uint32 mask) : m_mask(mask) {}
    size_t index = SIZE_MAX;

    bool query_items_add(t_size, const pfc::list_base_const_t<metadb_handle_ptr>&, const bit_array&) override { return !(m_mask & filter_add); }
    bool query_items_reorder(const t_size*, t_size) override { return !(m_mask & filter_reorder); }
    bool query_items_remove(const bit_array&, bool) override { return !(m_mask & filter_remove); }
    bool query_item_replace(t_size, const metadb_handle_ptr&, const metadb_handle_ptr&) override { return !(m_mask & filter_replace); }
    bool query_playlist_rename(const char*, t_size) override { return !(m_mask & filter_rename); }
    bool query_playlist_remove() override { return !(m_mask & filter_remove_playlist); }
    bool execute_default_action(t_size) override { return false; }
    void on_playlist_index_change(t_size i) override { index = i; }
    void on_playlist_remove() override;
    void get_lock_name(pfc::string_base& out) override { out = "JS Panel"; }
    void show_ui() override {}
    t_uint32 get_filter_mask() override { return m_mask; }

private:
    t_uint32 m_mask;
};

std::vector<service_ptr_t<JsLock>>& js_locks() {
    static std::vector<service_ptr_t<JsLock>> locks;
    return locks;
}

void JsLock::on_playlist_remove() {
    auto& l = js_locks();
    l.erase(std::remove_if(l.begin(), l.end(), [this](const service_ptr_t<JsLock>& k) { return k.get_ptr() == this; }), l.end());
}

bool config_bool(const GUID& id) {
    service_ptr_t<config_object> o;
    return config_object::g_find(o, id) && o->get_data_bool_simple(false);
}

void set_config_bool(const GUID& id, bool on) {
    service_ptr_t<config_object> o;
    if (config_object::g_find(o, id)) o->set_data_bool(on);
}

GUID guid_of(const std::string& s) { return pfc::GUID_from_text(s.c_str()); }

std::string text_of(const GUID& g) { return "{" + std::string(pfc::print_guid(g).c_str()) + "}"; }

}

Panel::Panel(Surface& surface, std::string scriptPath, std::function<void(const std::string&)> onScriptChanged)
    : play_callback_impl_base(flag_on_playback_starting | flag_on_playback_new_track | flag_on_playback_stop |
                              flag_on_playback_seek | flag_on_playback_pause | flag_on_playback_edited |
                              flag_on_playback_dynamic_info | flag_on_playback_dynamic_info_track |
                              flag_on_playback_time | flag_on_volume_change),
      m_surface(surface), m_path(std::move(scriptPath)), m_onScriptChanged(std::move(onScriptChanged)) {
    live().push_back(this);
    m_artNotify = now_playing_album_art_notify_manager::get()->add([this](album_art_data::ptr data) { on_now_playing_art(data); });
    m_callbacks = std::make_unique<Callbacks>(*this);
    reload();
}

Panel::~Panel() {
    m_abort->set();
    if (m_artNotify) now_playing_album_art_notify_manager::get()->remove(m_artNotify);
    m_callbacks.reset();
    m_selection.release();
    m_engine.reset();
    auto& l = live();
    l.erase(std::remove(l.begin(), l.end(), this), l.end());
}

std::string Panel::status() const {
    if (!m_engine) return "not loaded";
    if (m_engine->failed()) {
        const std::string& e = m_engine->error();
        return "error: " + e.substr(0, e.find('\n'));
    }
    return "running";
}

PanelInfo Panel::info() const {
    PanelInfo out{ script_path(), status() };
    if (m_engine) {
        out.memoryBytes = m_engine->memory_used();
        out.calls = m_engine->profile();
    }
    return out;
}

void Panel::set_script(const std::string& path) {
    m_path = path;
    if (m_onScriptChanged) m_onScriptChanged(m_path);
    reload();
}

void Panel::reload() {
    if (m_engine && m_engine->in_modal()) {
        later([this]() { reload(); });
        return;
    }
    m_selection.release();
    m_engine.reset();
    m_surface.set_tooltip({}, false, 0);
    m_tf.clear();
    m_engine = std::make_unique<JsEngine>(*this);
    std::string source;
    if (m_path.empty()) source = sample_script();
    else if (!read_text_file(m_path, source)) {
        log::console(log::Level::Warn, "js", "can't read the script " + m_path + ", running the built-in sample");
        source = sample_script();
    }
    const unsigned long long t0 = tick_ms();
    bool ok = false;
    try {
        ok = m_engine->load(source, m_path);
    } catch (const std::exception& e) {
        log::console(log::Level::Error, "js", label(m_path) + " stopped: " + e.what());
        m_engine.reset();
    }
    log::info("js", "loaded " + label(m_path) + " in " + std::to_string(tick_ms() - t0) + " ms" +
                        (ok ? "" : " (failed)"));
    watch_files();
    if (m_w > 0 && m_h > 0) {
        const int w = m_w, h = m_h;
        m_w = m_h = 0;
        resize(w, h);
    }
    m_surface.invalidate();
}

void Panel::paint(pui::gfx::Canvas& canvas) {
    if (m_engine) m_engine->paint(canvas);
}

void Panel::resize(int w, int h) {
    if (w == m_w && h == m_h) return;
    m_w = w;
    m_h = h;
    if (m_engine) m_engine->resize(w, h);
}

void Panel::mouse(Mouse kind, int x, int y, unsigned mask) {
    if (!m_engine) return;
    const bool handled = m_engine->mouse(kind, x, y, mask);
    if (kind == Mouse::RightUp && !handled) context_menu(x, y);
}

void Panel::wheel(int step, bool horizontal) {
    if (m_engine) m_engine->wheel(step, horizontal);
}

bool Panel::key_down(int vk) { return m_engine && m_engine->key_down(vk); }

bool Panel::key_up(int vk) { return vk > 0 && m_engine && m_engine->invoke("on_key_up", { vk }); }

void Panel::char_input(unsigned codepoint) {
    if (codepoint) invoke("on_char", { (int)codepoint });
}

void Panel::focus(bool focused) { invoke("on_focus", { focused }); }

bool Panel::key_pressed(int vk) { return m_surface.key_pressed(vk); }

void Panel::watch_files() {
    m_watched.clear();
    m_surface.stop_timer(kWatchTimer);
    if (!m_engine || m_path.empty() || !hot_reload()) return;
    for (const std::string& f : m_engine->source_files()) m_watched.emplace_back(f, modified(f));
    m_surface.start_timer(kWatchTimer, kWatchMs);
}

bool Panel::files_changed() {
    for (const auto& [file, stamp] : m_watched)
        if (modified(file) != stamp) {
            log::info("js", "reloading " + label(m_path) + ": " + file + " changed");
            return true;
        }
    return false;
}

void Panel::timer(int id) {
    if (id == kWatchTimer) {
        if (!hot_reload()) m_surface.stop_timer(kWatchTimer);
        else if (m_engine && !m_engine->in_modal() && files_changed()) reload();
        return;
    }
    if (m_engine) m_engine->timer(id);
    else m_surface.stop_timer(id);
}

void Panel::context_menu(int x, int y) {
    Menu items;
    auto add = [&](int id, const char* text) {
        MenuItem m;
        m.id = id;
        m.text = text;
        items.push_back(m);
    };
    add(kEdit, "Edit script…");
    add(kChoose, "Choose script file…");
    add(kReload, "Reload script");
    if (!m_path.empty()) add(kSample, "Use the built-in sample");
    MenuItem gallery;
    gallery.text = "Samples";
    gallery.submenu = true;
    const auto& list = samples();
    for (size_t i = 0; i < list.size(); ++i) {
        MenuItem m;
        m.id = kSamples + (int)i;
        m.text = list[i].name;
        gallery.children.push_back(m);
    }
    items.push_back(gallery);
    const int picked = popup_menu(x, y, items);
    if (picked >= kSamples && picked < kSamples + (int)list.size()) {
        const std::string path = sample_path(list[picked - kSamples]);
        if (!path.empty()) set_script(path);
        return;
    }
    switch (picked) {
    case kChoose: {
        const std::string path = m_surface.choose_script_file();
        if (!path.empty()) set_script(path);
        break;
    }
    case kEdit: edit_script(); break;
    case kReload: reload(); break;
    case kSample: set_script(std::string()); break;
    default: break;
    }
}

std::string Panel::current_source() {
    std::string source;
    if (m_path.empty() || !read_text_file(m_path, source)) source = sample_script();
    if (!source.empty() && source[0] == '\n') source.erase(0, 1);
    return source;
}

void Panel::edit_script() {
    const std::string title = "JS Panel — " + (m_path.empty() ? std::string("built-in sample (saved as a new file on apply)") : m_path);
    const int line = m_engine && m_engine->failed() ? error_line(m_engine->error(), m_path.empty() ? "<sample>" : m_path) : 0;
    m_surface.open_editor(title, current_source(),
                          { [this](const std::string& src) { return apply_source(src); },
                            [this](const std::string& src) { return check_syntax(src, m_path.empty() ? "script.js" : m_path); }, line });
}

std::string Panel::bundled_import(const std::string& name) { return name == "lodash" ? std::string(lodash_js()) : std::string(); }

std::string Panel::jsp3_path() { return native_dir(core_api::pathInProfile("foo_js_panel")) + "jsp3" + kPathSep; }

void Panel::edit_properties(const std::string& json) {
    m_surface.open_editor("JS Panel properties — " + label(m_path), json,
                          { [this](const std::string& text) {
                               const std::string err = check_properties(text);
                               if (!err.empty()) return "Not applied: " + err;
                               m_engine.reset();
                               save_properties(text);
                               reload();
                               return std::string("Applied");
                           },
                            [](const std::string& text) { return check_properties(text); } });
}

std::string Panel::apply_source(const std::string& source) {
    std::string path = m_path;
    if (path.empty()) path = new_script_path();
    if (path.empty() || !write_file(path, source)) return "Couldn't write " + label(path);
    log::info("js", "script saved: " + path);
    if (path != m_path) set_script(path);
    else reload();
    if (m_engine && m_engine->failed()) return "Applied, the script stopped: " + m_engine->error().substr(0, m_engine->error().find('\n'));
    return "Applied";
}

void Panel::repaint() { m_surface.invalidate(); }
void Panel::repaint_rect(const pui::gfx::Rect& r) { m_surface.invalidate_rect(r); }
void Panel::set_timer(int id, int ms) { m_surface.start_timer(id, ms); }
void Panel::kill_timer(int id) { m_surface.stop_timer(id); }

titleformat_object::ptr Panel::compiled(const std::string& expr) {
    auto it = m_tf.find(expr);
    if (it == m_tf.end()) {
        titleformat_object::ptr obj;
        titleformat_compiler::get()->compile_safe(obj, with_fallback_rating(expr).c_str());
        it = m_tf.emplace(expr, obj).first;
    }
    return it->second;
}

std::string Panel::titleformat(const std::string& expr) {
    const titleformat_object::ptr obj = compiled(expr);
    pfc::string8 out;
    auto pc = playback_control::get();
    if (pc->is_playing()) {
        pc->playback_format_title(nullptr, out, obj, nullptr, playback_control::display_level_all);
        return out.c_str();
    }
    metadb_handle_ptr focus;
    if (playlist_manager::get()->activeplaylist_get_focus_item_handle(focus) && focus.is_valid())
        focus->format_title(nullptr, out, obj, nullptr);
    return out.c_str();
}

bool Panel::is_playing() { return playback_control::get()->is_playing(); }
bool Panel::is_paused() { return playback_control::get()->is_paused(); }
double Panel::playback_time() { return playback_control::get()->playback_get_position(); }
double Panel::playback_length() { return playback_control::get()->playback_get_length(); }

void Panel::seek(double t) { playback_control::get()->playback_seek(t); }

void Panel::playback(Playback cmd) {
    auto pc = playback_control::get();
    switch (cmd) {
    case Playback::Random: pc->start(playback_control::track_command_rand); break;
    case Playback::Play: pc->start(); break;
    case Playback::Pause: pc->pause(true); break;
    case Playback::PlayOrPause: pc->play_or_pause(); break;
    case Playback::Stop: pc->stop(); break;
    case Playback::Next: pc->next(); break;
    case Playback::Prev: pc->previous(); break;
    }
}

void Panel::console(const std::string& text) {
    const bool err = text.rfind("error: ", 0) == 0;
    log::console(err ? log::Level::Error : log::Level::Info, "js", "[" + label(m_path) + "] " + text);
}

bool Panel::read_text_file(const std::string& path, std::string& out) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(fs_path(path), ec)) return false;
    out = read_file(path);
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB &&
        (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return true;
}

LibraryState Panel::navidrome_state() {
    auto api = navidrome_api();
    if (api.is_empty()) return LibraryState::Missing;
    return api->is_configured() ? LibraryState::Ready : LibraryState::NotConfigured;
}

void Panel::navidrome_albums(AlbumsDone done) {
    auto api = navidrome_api();
    if (api.is_empty()) {
        done({}, "foo_navidrome isn't installed");
        return;
    }
    if (!api->is_configured()) {
        done({}, "foo_navidrome has no server configured");
        return;
    }
    auto abort = m_abort;
    const std::string who = label(m_path);
    std::thread([api, abort, who, done = std::move(done)]() mutable {
        AlbumCollector sink;
        pfc::string8 err;
        bool ok = false;
        try {
            navidrome::navidrome_library_api_v2::ptr v2;
            if (api->service_query_t(v2)) ok = v2->list_albums_v2(static_cast<navidrome::library_album_sink_v2&>(sink), *abort, err);
            else ok = api->list_albums(static_cast<navidrome::library_album_sink&>(sink), *abort, err);
        } catch (const std::exception& e) {
            err = e.what();
        } catch (...) {
            err = "the Navidrome library request failed";
        }
        if (abort->is_aborting()) return;
        std::string error = ok ? std::string() : std::string(err.is_empty() ? "the Navidrome library request failed" : err.c_str());
        if (!ok) log::warn("navidrome", "[" + who + "] album list failed: " + error);
        else log::info("navidrome", "[" + who + "] album list: " + std::to_string(sink.albums.size()) + " albums");
        auto albums = std::make_shared<std::vector<LibraryAlbum>>(std::move(sink.albums));
        fb2k::inMainThread([abort, albums, error, done = std::move(done)]() {
            if (!abort->is_aborting()) done(std::move(*albums), error);
        });
    }).detach();
}

void Panel::navidrome_play(bool artist, const std::string& id, bool replace, bool play) {
    auto api = navidrome_api();
    if (api.is_empty()) {
        console("navidrome: foo_navidrome isn't installed");
        return;
    }
    if (artist) api->play_artist(id.c_str(), replace, play);
    else api->play_album(id.c_str(), replace, play);
}

void Panel::navidrome_artist_info(const std::string& name, ArtistInfoDone done) {
    auto api = navidrome_api();
    navidrome::navidrome_library_api_v3::ptr v3;
    if (api.is_empty() || !api->service_query_t(v3)) {
        done({}, api.is_empty() ? "foo_navidrome isn't installed" : "this foo_navidrome has no artist info; update it");
        return;
    }
    auto abort = m_abort;
    const std::string who = label(m_path);
    std::thread([v3, abort, who, name, done = std::move(done)]() mutable {
        struct Sink : navidrome::artist_info_sink {
            ArtistInfo info;
            void on_artist_info(const char* id, const char* bio, const char* image, const char* lastfm) override {
                info = { id ? id : "", bio ? bio : "", image ? image : "", lastfm ? lastfm : "" };
            }
        } sink;
        pfc::string8 err;
        bool ok = false;
        try {
            ok = v3->artist_info(name.c_str(), sink, *abort, err);
        } catch (const std::exception& e) {
            err = e.what();
        } catch (...) {
            err = "the Navidrome artist request failed";
        }
        if (abort->is_aborting()) return;
        const std::string error = ok ? std::string() : std::string(err.is_empty() ? "the Navidrome artist request failed" : err.c_str());
        log::info("navidrome", "[" + who + "] artist info for \"" + name + "\": " + (ok ? (sink.info.imageUrl.empty() ? "no image" : "image") : error));
        auto info = std::make_shared<ArtistInfo>(std::move(sink.info));
        fb2k::inMainThread([abort, info, error, done = std::move(done)]() {
            if (!abort->is_aborting()) done(std::move(*info), error);
        });
    }).detach();
}

void Panel::navidrome_search(const std::string& query, SearchDone done) {
    auto api = navidrome_api();
    navidrome::navidrome_library_api_v4::ptr v4;
    if (api.is_empty() || !api->service_query_t(v4)) {
        done({}, api.is_empty() ? "foo_navidrome isn't installed" : "this foo_navidrome has no search; update it");
        return;
    }
    auto abort = m_abort;
    const std::string who = label(m_path);
    std::thread([v4, abort, who, query, done = std::move(done)]() mutable {
        struct Sink : navidrome::library_search_sink {
            SearchResult result;
            metadb_handle_list tracks;
            void on_artist(const char* id, const char* name, const char* cover, int albums) override {
                result.artists.push_back({ id ? id : "", name ? name : "", cover ? cover : "", albums, 0 });
            }
            void on_album(const char* id, const char* name, const char* artist, const char* artistId, const char* cover, int year,
                          int songs, const char* created) override {
                result.albums.push_back({ id ? id : "", name ? name : "", artist ? artist : "", artistId ? artistId : "", cover ? cover : "",
                                          created ? created : "", year, songs });
            }
            void on_track(const metadb_handle_ptr& track) override { tracks.add_item(track); }
        };
        auto sink = std::make_shared<Sink>();
        pfc::string8 err;
        bool ok = false;
        try {
            ok = v4->search(query.c_str(), *sink, *abort, err);
        } catch (const std::exception& e) {
            err = e.what();
        } catch (...) {
            err = "the Navidrome search failed";
        }
        if (abort->is_aborting()) return;
        const std::string error = ok ? std::string() : std::string(err.is_empty() ? "the Navidrome search failed" : err.c_str());
        log::info("navidrome", "[" + who + "] search \"" + query + "\": " +
                                   (ok ? std::to_string(sink->result.artists.size()) + " artists, " + std::to_string(sink->result.albums.size()) +
                                             " albums, " + std::to_string(sink->tracks.get_count()) + " tracks"
                                       : error));
        fb2k::inMainThread([abort, sink, error, done = std::move(done)]() {
            if (abort->is_aborting()) return;
            sink->result.tracks = wrap_all(sink->tracks);
            done(std::move(sink->result), error);
        });
    }).detach();
}

TrackPtr Panel::now_playing() {
    metadb_handle_ptr h;
    return playback_control::get()->get_now_playing(h) ? wrap(h) : nullptr;
}

void Panel::load_navidrome_library() {
    NavidromeLibrary& lib = navidrome_library();
    if (lib.state != NavidromeLibrary::State::Idle || !navidrome_in_library()) return;
    auto api = navidrome_api();
    navidrome::navidrome_library_api_v3::ptr v3;
    if (api.is_empty() || !api->service_query_t(v3) || !v3->is_configured()) return;
    lib.state = NavidromeLibrary::State::Loading;
    log::info("navidrome", "loading the Navidrome library for fb.GetLibraryItems()");
    std::thread([v3]() {
        TrackCollector sink;
        pfc::string8 err;
        bool ok = false;
        try {
            ok = v3->list_tracks(sink, fb2k::noAbort, err);
        } catch (const std::exception& e) {
            err = e.what();
        }
        auto tracks = std::make_shared<metadb_handle_list>(std::move(sink.tracks));
        const std::string error = ok ? std::string() : std::string(err.c_str());
        fb2k::inMainThread([tracks, error]() {
            NavidromeLibrary& lib = navidrome_library();
            if (!error.empty()) {
                lib.state = NavidromeLibrary::State::Failed;
                log::warn("navidrome", "the Navidrome library couldn't be loaded: " + error);
                return;
            }
            lib.tracks = *tracks;
            for (size_t i = 0; i < lib.tracks.get_count(); ++i) lib.members.insert(lib.tracks[i].get_ptr());
            lib.state = NavidromeLibrary::State::Ready;
            log::info("navidrome", "Navidrome library: " + std::to_string(lib.tracks.get_count()) + " tracks");
            for (Panel* p : live()) p->library_event(JsEngine::LibraryChange::Added, lib.tracks);
        });
    }).detach();
}

Tracks Panel::library_items() {
    pfc::list_t<metadb_handle_ptr> all;
    library_manager::get()->get_all_items(all);
    load_navidrome_library();
    const NavidromeLibrary& lib = navidrome_library();
    Tracks out;
    out.reserve(all.get_count() + lib.tracks.get_count());
    for (size_t i = 0; i < all.get_count(); ++i) out.push_back(wrap(all[i]));
    if (lib.state == NavidromeLibrary::State::Ready)
        for (size_t i = 0; i < lib.tracks.get_count(); ++i) out.push_back(wrap(lib.tracks[i]));
    return out;
}

std::string Panel::track_path(const TrackPtr& t) {
    auto h = handle_of(t);
    return h.is_valid() ? std::string(h->get_path()) : std::string();
}

bool Panel::same_track(const TrackPtr& a, const TrackPtr& b) {
    auto x = handle_of(a), y = handle_of(b);
    return x.is_valid() && x == y;
}

std::vector<std::string> Panel::format_tracks(const std::string& expr, const Tracks& tracks) {
    const titleformat_object::ptr obj = compiled(expr);
    std::vector<std::string> out;
    out.reserve(tracks.size());
    pfc::string8 s;
    for (const auto& t : tracks) {
        auto h = handle_of(t);
        s.reset();
        if (h.is_valid() && obj.is_valid()) h->format_title(nullptr, s, obj, nullptr);
        out.emplace_back(s.c_str());
    }
    return out;
}

void Panel::on_now_playing_art(album_art_data::ptr data) {
    if (m_artWaiting.empty()) return;
    auto waiting = std::move(m_artWaiting);
    m_artWaiting.clear();
    auto np = now_playing();
    const auto img = decode(data);
    for (auto& w : waiting) {
        if (!*w.done) continue;
        if (np && same_track(np, w.track)) {
            auto done = std::move(*w.done);
            *w.done = nullptr;
            done(img, std::string());
        } else {
            m_artWaiting.push_back(std::move(w));
        }
    }
}

void Panel::album_art(const TrackPtr& t, int artId, ArtDone doneIn) {
    auto h = handle_of(t);
    auto done = std::make_shared<ArtDone>(std::move(doneIn));
    if (!h.is_valid()) {
        (*done)(nullptr, std::string());
        return;
    }
    auto np = now_playing();
    const bool nowPlaying = artId == 0 && np && same_track(np, t);
    if (nowPlaying) {
        if (auto img = decode(now_playing_album_art_notify_manager::get()->current())) {
            auto abort = m_abort;
            fb2k::inMainThread([abort, done, img]() {
                if (!abort->is_aborting() && *done) {
                    auto d = std::move(*done);
                    *done = nullptr;
                    d(img, std::string());
                }
            });
            return;
        }
        m_artWaiting.push_back({ t, artId, done });
    }
    auto abort = m_abort;
    const GUID id = art_guid(artId);
    std::thread([h, id, abort, done, nowPlaying]() {
        pui::gfx::ImagePtr img;
        try {
            pfc::list_t<GUID> ids;
            ids.add_item(id);
            auto extractor = album_art_manager_v2::get()->open(pfc::list_single_ref_t<metadb_handle_ptr>(h), ids, *abort);
            img = decode(extractor->query(id, *abort));
        } catch (...) {
        }
        if (abort->is_aborting()) return;
        fb2k::inMainThread([abort, done, img, nowPlaying]() {
            if (abort->is_aborting() || !*done) return;
            if (!img && nowPlaying) return;
            auto d = std::move(*done);
            *done = nullptr;
            d(img, std::string());
        });
    }).detach();
}

pui::gfx::ImagePtr Panel::load_image(const std::string& path) { return pui::gfx::decode_image_file(path); }

bool Panel::image_pixels(const pui::gfx::Image& img, pui::gfx::Pixels& out) { return pui::gfx::image_pixels(img, out); }
pui::gfx::ImagePtr Panel::image_from_pixels(const pui::gfx::Pixels& px) { return pui::gfx::image_from_pixels(px); }
std::unique_ptr<pui::gfx::Canvas> Panel::pixels_canvas(pui::gfx::Pixels& px, double scale) { return pui::gfx::pixels_canvas(px, scale); }

bool Panel::save_png(const pui::gfx::Image& img, const std::string& path) {
    return pui::gfx::encode_png_file(img, path, std::max(1, img.width()));
}

pui::gfx::ImagePtr Panel::album_art_sync(const TrackPtr& t, int artId, std::string& path) {
    auto h = handle_of(t);
    if (!h.is_valid()) return nullptr;
    path = h->get_path();
    auto np = now_playing();
    if (artId == 0 && np && same_track(np, t))
        if (auto img = decode(now_playing_album_art_notify_manager::get()->current())) return img;
    try {
        pfc::list_t<GUID> ids;
        ids.add_item(art_guid(artId));
        auto extractor = album_art_manager_v2::get()->open(pfc::list_single_ref_t<metadb_handle_ptr>(h), ids, *m_abort);
        return decode(extractor->query(art_guid(artId), *m_abort));
    } catch (...) {
        return nullptr;
    }
}

void Panel::http_request(const HttpRequest& req, HttpDone done) {
    auto abort = m_abort;
    const std::string who = label(m_path);
    std::thread([abort, req, who, done = std::move(done)]() mutable {
        HttpResponse r = run_http(req, *abort);
        if (abort->is_aborting()) return;
        if (!r.error.empty()) log::info("js", "[" + who + "] http " + req.method + " failed: " + r.error);
        fb2k::inMainThread([abort, r, done = std::move(done)]() {
            if (!abort->is_aborting()) done(r);
        });
    }).detach();
}

HttpResponse Panel::http_request_sync(const HttpRequest& req) { return run_http(req, *m_abort); }

bool Panel::shell_run(const std::string& command) { return ui::shell_run(command); }
std::string Panel::clipboard_text() { return ui::clipboard_text(); }
bool Panel::set_clipboard_text(const std::string& text) { return ui::copy_to_clipboard(text); }
void Panel::ui_selection(SelectionMode mode, const Tracks& tracks) {
    if (mode == SelectionMode::Released) {
        m_selection.release();
        return;
    }
    if (m_selection.is_empty()) m_selection = ui_selection_manager::get()->acquire();
    switch (mode) {
    case SelectionMode::Tracks: m_selection->set_selection(handles_of(tracks)); break;
    case SelectionMode::PlaylistSelection: m_selection->set_playlist_selection_tracking(); break;
    case SelectionMode::Playlist: m_selection->set_playlist_tracking(); break;
    case SelectionMode::Released: break;
    }
}

bool Panel::update_tags(const Tracks& tracks, const std::vector<TagSet>& sets) {
    navidrome::navidrome_rating_api::ptr rating;
    {
        service_enum_t<navidrome::navidrome_rating_api> e;
        e.next(rating);
    }
    metadb_handle_list local;
    pfc::list_t<file_info_impl> infos;
    for (size_t i = 0; i < tracks.size() && i < sets.size(); ++i) {
        auto h = handle_of(tracks[i]);
        if (!h.is_valid()) continue;
        if (rating.is_valid() && rating->is_navidrome_track(h)) {
            for (const auto& [key, values] : sets[i])
                if (stricmp_utf8(key.c_str(), "rating") == 0) rating->set_rating_async(h, values.empty() ? 0 : atoi(values.front().c_str()));
            continue;
        }
        file_info_impl info;
        if (!h->get_info(info)) continue;
        for (const auto& [key, values] : sets[i]) {
            info.meta_remove_field(key.c_str());
            for (const auto& v : values) info.meta_add(key.c_str(), v.c_str());
        }
        local.add_item(h);
        infos.add_item(info);
    }
    if (local.get_count() == 0) return true;
    pfc::list_t<const file_info*> ptrs;
    for (size_t i = 0; i < infos.get_count(); ++i) ptrs.add_item(&infos[i]);
    metadb_io_v2::get()->update_info_async_simple(local, ptrs, core_api::get_main_window(), metadb_io_v2::op_flag_delay_ui, nullptr);
    return true;
}

namespace {

panels_ui::panels_ui_api::ptr panels_ui_service() {
    service_enum_t<panels_ui::panels_ui_api> e;
    panels_ui::panels_ui_api::ptr api;
    e.next(api);
    return api;
}

}

bool Panel::panels_ui_available() {
    auto api = panels_ui_service();
    return api.is_valid() && api->is_active();
}

bool Panel::panels_ui_variable(const std::string& name, std::string& out) {
    auto api = panels_ui_service();
    pfc::string8 v;
    if (api.is_empty() || !api->get_variable(name.c_str(), v)) return false;
    out = v.c_str();
    return true;
}

void Panel::set_panels_ui_variable(const std::string& name, const std::string& value) {
    if (auto api = panels_ui_service(); api.is_valid()) api->set_variable(name.c_str(), value.c_str());
}

bool Panel::panels_ui_action(const std::string& action) {
    auto api = panels_ui_service();
    return api.is_valid() && api->run_action(action.c_str());
}

Tracks Panel::get_ui_selection() {
    metadb_handle_list list;
    ui_selection_manager::get()->get_selection(list);
    return wrap_all(list);
}

int Panel::message_box(const std::string& text, const std::string& caption, int type) { return m_surface.message_box(text, caption, type); }

void Panel::load_image_async(const std::string& path, ArtDone done) {
    auto abort = m_abort;
    std::thread([abort, path, done = std::move(done)]() mutable {
        auto img = pui::gfx::decode_image_file(path);
        if (abort->is_aborting()) return;
        fb2k::inMainThread([abort, img, path, done = std::move(done)]() {
            if (!abort->is_aborting()) done(img, path);
        });
    }).detach();
}

int Panel::playlist_count() { return (int)playlist_manager::get()->get_playlist_count(); }

int Panel::active_playlist() {
    const size_t p = playlist_manager::get()->get_active_playlist();
    return p == pfc_infinite ? -1 : (int)p;
}

void Panel::set_active_playlist(int p) {
    if (p >= 0 && p < playlist_count()) playlist_manager::get()->set_active_playlist((size_t)p);
}

int Panel::playing_playlist() {
    const size_t p = playlist_manager::get()->get_playing_playlist();
    return p == pfc_infinite ? -1 : (int)p;
}

std::string Panel::playlist_name(int p) {
    pfc::string8 n;
    if (p >= 0 && p < playlist_count()) playlist_manager::get()->playlist_get_name((size_t)p, n);
    return n.c_str();
}

int Panel::find_or_create_playlist(const std::string& name, bool unlocked) {
    auto pm = playlist_manager::get();
    const size_t p = unlocked ? pm->find_or_create_playlist_unlocked(name.c_str()) : pm->find_or_create_playlist(name.c_str());
    return p == pfc_infinite ? -1 : (int)p;
}

void Panel::clear_playlist(int p) {
    if (p >= 0 && p < playlist_count()) playlist_manager::get()->playlist_clear((size_t)p);
}

void Panel::insert_playlist_items(int p, int base, const Tracks& tracks, bool select) {
    if (p < 0 || p >= playlist_count()) return;
    const auto list = handles_of(tracks);
    auto pm = playlist_manager::get();
    const size_t at = std::min((size_t)std::max(0, base), pm->playlist_get_item_count((size_t)p));
    pm->playlist_insert_items((size_t)p, at, list, bit_array_val(select));
}

void Panel::execute_default_action(int p, int item) {
    if (p >= 0 && p < playlist_count() && item >= 0) playlist_manager::get()->playlist_execute_default_action((size_t)p, (size_t)item);
}

void Panel::library_event(JsEngine::LibraryChange kind, metadb_handle_list_cref items) {
    if (m_engine) m_engine->library_changed(kind, wrap_all(items));
}

void Panel::on_items_added(metadb_handle_list_cref items) { library_event(JsEngine::LibraryChange::Added, items); }
void Panel::on_items_removed(metadb_handle_list_cref items) { library_event(JsEngine::LibraryChange::Removed, items); }
void Panel::on_items_modified(metadb_handle_list_cref items) { library_event(JsEngine::LibraryChange::Changed, items); }

void Panel::on_playback_starting(play_control::t_track_command cmd, bool paused) {
    if (m_engine) m_engine->playback_starting((int)cmd, paused);
}

void Panel::on_playback_new_track(metadb_handle_ptr track) {
    if (m_engine) m_engine->playback_new_track(wrap(track));
}

void Panel::on_playback_stop(play_control::t_stop_reason reason) {
    if (m_engine) m_engine->playback_stop((int)reason);
}

void Panel::on_playback_pause(bool paused) {
    if (m_engine) m_engine->playback_pause(paused);
}

void Panel::on_playback_time(double t) {
    if (m_engine) m_engine->playback_time(t);
}

void Panel::on_playback_seek(double t) { invoke("on_playback_seek", { t }); }
void Panel::on_playback_edited(metadb_handle_ptr track) { invoke("on_playback_edited", { wrap(track) }); }
void Panel::on_playback_dynamic_info(const file_info&) { invoke("on_playback_dynamic_info"); }
void Panel::on_playback_dynamic_info_track(const file_info&) { invoke("on_playback_dynamic_info_track"); }
void Panel::on_volume_change(float db) { invoke("on_volume_change", { (double)db }); }

void Panel::invoke(const char* name, const std::vector<Arg>& args) {
    if (m_engine) m_engine->invoke(name, args);
}

void Panel::later(std::function<void()> fn) {
    auto abort = m_abort;
    fb2k::inMainThread([abort, fn = std::move(fn)]() {
        if (!abort->is_aborting()) fn();
    });
}

bool Panel::valid(int p) { return p >= 0 && p < playlist_count(); }

std::string Panel::load_properties() {
    const std::string path = properties_file(m_path);
    std::error_code ec;
    return std::filesystem::is_regular_file(fs_path(path), ec) ? read_file(path) : std::string();
}

void Panel::save_properties(const std::string& json) {
    const std::string path = properties_file(m_path);
    std::error_code ec;
    std::filesystem::create_directories(fs_path(path).parent_path(), ec);
    if (!write_file(path, json)) log::warn("js", "can't save the properties of " + label(m_path) + " to " + path);
}

void Panel::notify_others(const std::string& name, const std::string& json, const std::vector<NotifyItem>& items) {
    for (Panel* other : live()) {
        if (other == this) continue;
        other->later([other, name, json, items]() {
            if (other->m_engine) other->m_engine->notify_data(name, json, items);
        });
    }
}

void Panel::reload_script() {
    later([this]() { reload(); });
}

std::string Panel::profile_path() { return native_dir(core_api::get_profile_path()); }

std::string Panel::component_path() {
    const std::string p = core_api::get_my_full_path();
    return p.substr(0, p.find_last_of("/\\") + 1);
}

std::string Panel::foobar_path() { return ui::app_dir(); }

std::string Panel::version() { return core_version_info_v2::get()->get_version_as_text(); }

double Panel::volume() { return playback_control::get()->get_volume(); }
void Panel::set_volume(double db) { playback_control::get()->set_volume((float)db); }

bool Panel::flag(Flag f) {
    switch (f) {
    case Flag::StopAfterCurrent: return playback_control::get()->get_stop_after_current();
    case Flag::CursorFollowPlayback: return config_bool(standard_config_objects::bool_cursor_follows_playback);
    case Flag::PlaybackFollowCursor: return config_bool(standard_config_objects::bool_playback_follows_cursor);
    case Flag::AlwaysOnTop: {
        service_ptr_t<config_object> o;
        return config_object::g_find(o, standard_config_objects::bool_ui_always_on_top) ? o->get_data_bool_simple(false) : m_surface.always_on_top();
    }
    }
    return false;
}

void Panel::set_flag(Flag f, bool on) {
    switch (f) {
    case Flag::StopAfterCurrent: playback_control::get()->set_stop_after_current(on); break;
    case Flag::CursorFollowPlayback: set_config_bool(standard_config_objects::bool_cursor_follows_playback, on); break;
    case Flag::PlaybackFollowCursor: set_config_bool(standard_config_objects::bool_playback_follows_cursor, on); break;
    case Flag::AlwaysOnTop: {
        service_ptr_t<config_object> o;
        if (config_object::g_find(o, standard_config_objects::bool_ui_always_on_top)) o->set_data_bool(on);
        else if (on != m_surface.always_on_top()) {
            m_surface.set_always_on_top(on);
            broadcast("on_always_on_top_changed", { on });
        }
        break;
    }
    }
}

bool Panel::run_main_menu_command(const std::string& path) {
    const bool nameOnly = path.find('/') == std::string::npos;
    for (auto cmds : mainmenu_commands::enumerate()) {
        const std::string prefix = nameOnly ? std::string() : main_menu_prefix(cmds->get_parent());
        for (t_uint32 i = 0; i < cmds->get_command_count(); ++i) {
            pfc::string8 name;
            cmds->get_name(i, name);
            if (!same_text(prefix + strip_accel(name.c_str()), path)) continue;
            cmds->execute(i, nullptr);
            return true;
        }
    }
    log::info("js", "[" + label(m_path) + "] no main menu command " + path);
    return false;
}

bool Panel::run_context_command(const std::string& path, const Tracks* tracks) {
    contextmenu_manager::ptr cm = contextmenu_manager::g_create();
    if (tracks) cm->init_context(handles_of(*tracks), 0);
    else cm->init_context_playlist(0);
    contextmenu_node* node = find_context_command(cm->get_root(), {}, path);
    if (!node) {
        log::info("js", "[" + label(m_path) + "] no context menu command " + path);
        return false;
    }
    node->execute();
    return true;
}

void Panel::popup_message(const std::string& text, const std::string& title) { popup_message::g_show(text.c_str(), title.c_str()); }

TrackPtr Panel::focus_item() {
    metadb_handle_ptr h;
    return playlist_manager::get()->activeplaylist_get_focus_item_handle(h) ? wrap(h) : nullptr;
}

Tracks Panel::query_items(const Tracks& tracks, const std::string& query, std::string& error) {
    try {
        search_filter::ptr filter = search_filter_manager::get()->create(query.c_str());
        const auto list = handles_of(tracks);
        auto hit = std::make_unique<bool[]>(list.get_count());
        filter->test_multi(list, hit.get());
        Tracks out;
        for (size_t i = 0; i < list.get_count(); ++i)
            if (hit[i]) out.push_back(wrap(list[i]));
        return out;
    } catch (const std::exception& e) {
        error = e.what();
    }
    return {};
}

bool Panel::in_library(const TrackPtr& t) {
    auto h = handle_of(t);
    if (!h.is_valid()) return false;
    return library_manager::get()->is_item_in_library(h) || navidrome_library().members.count(h.get_ptr()) > 0;
}

std::string Panel::library_relative_path(const TrackPtr& t) {
    auto h = handle_of(t);
    pfc::string8 out;
    if (!h.is_valid()) return {};
    if (library_manager::get()->get_relative_path(h, out)) return windows_separators(out.c_str());
    if (!navidrome_library().members.count(h.get_ptr())) return {};
    const titleformat_object::ptr obj = compiled(
        "$replace($if2(%album artist%,?),\\,-)\\$replace($if2(%album%,?),\\,-)\\$replace([$num(%tracknumber%,2) - ]%title%,\\,-)");
    if (obj.is_valid()) h->format_title(nullptr, out, obj, nullptr);
    return out.c_str();
}

std::string Panel::path_sort_key(const TrackPtr& t) {
    auto h = handle_of(t);
    if (!h.is_valid()) return {};
    if (!navidrome_library().members.count(h.get_ptr())) return h->get_path();
    return "navidrome://" + library_relative_path(t);
}

bool Panel::has_component(const std::string& name, bool dll) {
    for (auto c : componentversion::enumerate()) {
        pfc::string8 file, component;
        c->get_file_name(file);
        std::string stem = file.c_str();
        if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) stem.erase(dot);
        if (same_text(stem, name) || same_text(file.c_str(), name)) return true;
        if (!dll) {
            c->get_component_name(component);
            if (same_text(component.c_str(), name)) return true;
        }
    }
    return false;
}

void Panel::exit_app() {
    later([]() { standard_commands::main_exit(); });
}

TrackInfo Panel::track_info(const TrackPtr& t) {
    TrackInfo out;
    auto h = handle_of(t);
    if (!h.is_valid()) return out;
    out.size = (long long)h->get_filesize();
    out.subsong = h->get_subsong_index();
    const t_filetimestamp ts = h->get_filetimestamp();
    if (ts != filetimestamp_invalid && ts > 116444736000000000ull) out.modified = (double)((ts - 116444736000000000ull) / 10000000ull);
    metadb_info_container::ptr info;
    if (!h->get_info_ref(info)) return out;
    const file_info& fi = info->info();
    out.length = fi.get_length();
    for (size_t i = 0; i < fi.meta_get_count(); ++i) {
        std::vector<std::string> values;
        for (size_t v = 0; v < fi.meta_enum_value_count(i); ++v) values.emplace_back(fi.meta_enum_value(i, v));
        out.meta.emplace_back(fi.meta_enum_name(i), std::move(values));
    }
    for (size_t i = 0; i < fi.info_get_count(); ++i) out.info.emplace_back(fi.info_enum_name(i), fi.info_enum_value(i));
    return out;
}

bool Panel::write_text_file(const std::string& path, const std::string& text) { return write_file(path, text); }

int Panel::playlist_item_count(int p) { return valid(p) ? (int)playlist_manager::get()->playlist_get_item_count((size_t)p) : 0; }

Tracks Panel::playlist_items(int p, bool selectedOnly) {
    if (!valid(p)) return {};
    metadb_handle_list list;
    if (selectedOnly) playlist_manager::get()->playlist_get_selected_items((size_t)p, list);
    else playlist_manager::get()->playlist_get_all_items((size_t)p, list);
    return wrap_all(list);
}

std::vector<bool> Panel::playlist_selection(int p) {
    if (!valid(p)) return {};
    auto pm = playlist_manager::get();
    const size_t n = pm->playlist_get_item_count((size_t)p);
    pfc::bit_array_bittable mask(n);
    pm->playlist_get_selection_mask((size_t)p, mask);
    std::vector<bool> out(n);
    for (size_t i = 0; i < n; ++i) out[i] = mask.get(i);
    return out;
}

void Panel::set_playlist_selection(int p, const std::vector<int>& items, bool state) {
    if (!valid(p)) return;
    auto pm = playlist_manager::get();
    const size_t n = pm->playlist_get_item_count((size_t)p);
    pfc::bit_array_bittable affected(n);
    for (int i : items)
        if (i >= 0 && (size_t)i < n) affected.set((size_t)i, true);
    pm->playlist_set_selection((size_t)p, affected, pfc::bit_array_val(state));
}

void Panel::clear_playlist_selection(int p) {
    if (valid(p)) playlist_manager::get()->playlist_clear_selection((size_t)p);
}

void Panel::remove_playlist_selection(int p, bool crop) {
    if (valid(p)) playlist_manager::get()->playlist_remove_selection((size_t)p, crop);
}

void Panel::move_playlist_selection(int p, int delta) {
    if (valid(p)) playlist_manager::get()->playlist_move_selection((size_t)p, delta);
}

int Panel::playlist_focus(int p) { return valid(p) ? index_or_none(playlist_manager::get()->playlist_get_focus_item((size_t)p)) : -1; }

void Panel::set_playlist_focus(int p, int item) {
    if (valid(p) && item >= 0 && item < playlist_item_count(p)) playlist_manager::get()->playlist_set_focus_item((size_t)p, (size_t)item);
}

void Panel::ensure_visible(int p, int item) {
    if (valid(p) && item >= 0 && item < playlist_item_count(p)) playlist_manager::get()->playlist_ensure_visible((size_t)p, (size_t)item);
}

int Panel::create_playlist(int p, const std::string& name) {
    auto pm = playlist_manager::get();
    const size_t at = p < 0 || p > playlist_count() ? pfc_infinite : (size_t)p;
    return index_or_none(name.empty() ? pm->create_playlist_autoname(at) : pm->create_playlist(name.c_str(), name.size(), at));
}

int Panel::duplicate_playlist(int p, const std::string& name) {
    if (!valid(p)) return -1;
    auto pm = playlist_manager::get();
    metadb_handle_list items;
    pm->playlist_get_all_items((size_t)p, items);
    const int at = create_playlist(p + 1, name.empty() ? playlist_name(p) : name);
    if (at >= 0) pm->playlist_insert_items((size_t)at, 0, items, pfc::bit_array_false());
    return at;
}

bool Panel::remove_playlist(int p, bool switchActive) {
    if (!valid(p)) return false;
    auto pm = playlist_manager::get();
    return switchActive ? pm->remove_playlist_switch((size_t)p) : pm->remove_playlist((size_t)p);
}

bool Panel::rename_playlist(int p, const std::string& name) {
    return valid(p) && playlist_manager::get()->playlist_rename((size_t)p, name.c_str(), name.size());
}

bool Panel::move_playlist(int from, int to) {
    const int n = playlist_count();
    if (!valid(from) || to < 0 || to >= n) return false;
    std::vector<size_t> order;
    for (int i = 0; i < n; ++i)
        if (i != from) order.push_back((size_t)i);
    order.insert(order.begin() + to, (size_t)from);
    return playlist_manager::get()->reorder(order.data(), order.size());
}

bool Panel::playlist_is_auto(int p) { return valid(p) && autoplaylist_manager::get()->is_client_present((size_t)p); }
bool Panel::playlist_is_locked(int p) { return valid(p) && playlist_manager::get()->playlist_lock_is_present((size_t)p); }

int Panel::create_autoplaylist(int p, const std::string& name, const std::string& query, const std::string& sort, int flags) {
    const int at = create_playlist(p, name);
    if (at < 0) return -1;
    try {
        autoplaylist_manager::get()->add_client_simple(query.c_str(), sort.c_str(), (size_t)at, (t_uint32)flags);
    } catch (const std::exception& e) {
        console("error: CreateAutoPlaylist: " + std::string(e.what()));
        playlist_manager::get()->remove_playlist((size_t)at);
        return -1;
    }
    return at;
}

void Panel::show_autoplaylist_ui(int p) {
    if (!playlist_is_auto(p)) return;
    try {
        autoplaylist_manager::get()->query_client((size_t)p)->show_ui((size_t)p);
    } catch (const std::exception& e) {
        log::warn("js", "autoplaylist UI: " + std::string(e.what()));
    }
}

int Panel::playback_order() { return (int)playlist_manager::get()->playback_order_get_active(); }

void Panel::set_playback_order(int order) {
    auto pm = playlist_manager::get();
    if (order >= 0 && (size_t)order < pm->playback_order_get_count()) pm->playback_order_set_active((size_t)order);
}

ItemLocation Panel::playing_location() {
    size_t pl = 0, item = 0;
    if (!playlist_manager::get()->get_playing_item_location(&pl, &item)) return {};
    return { (int)pl, (int)item };
}

bool Panel::sort_playlist(int p, const std::string& pattern, bool selOnly) {
    return valid(p) && playlist_manager::get()->playlist_sort_by_format((size_t)p, pattern.empty() ? nullptr : pattern.c_str(), selOnly);
}

bool Panel::reorder_playlist(int p, const std::vector<int>& order) {
    if (!valid(p) || (int)order.size() != playlist_item_count(p)) return false;
    std::vector<size_t> o(order.begin(), order.end());
    return playlist_manager::get()->playlist_reorder_items((size_t)p, o.data(), o.size());
}

void Panel::undo_backup(int p) {
    if (valid(p)) playlist_manager::get()->playlist_undo_backup((size_t)p);
}

void Panel::queue_add(const TrackPtr& t) {
    if (auto h = handle_of(t); h.is_valid()) playlist_manager::get()->queue_add_item(h);
}

void Panel::queue_add_playlist_item(int p, int item) {
    if (valid(p) && item >= 0 && item < playlist_item_count(p)) playlist_manager::get()->queue_add_item_playlist((size_t)p, (size_t)item);
}

void Panel::queue_flush() { playlist_manager::get()->queue_flush(); }

void Panel::queue_remove(const std::vector<int>& indices) {
    auto pm = playlist_manager::get();
    const size_t n = pm->queue_get_count();
    pfc::bit_array_bittable mask(n);
    bool any = false;
    for (int i : indices)
        if (i >= 0 && (size_t)i < n) mask.set((size_t)i, any = true);
    if (any) pm->queue_remove_mask(mask);
}

int Panel::popup_menu(int x, int y, const Menu& menu) { return m_surface.popup_menu(x, y, menu); }

MenuSourcePtr Panel::context_menu(ContextKind kind, const Tracks& tracks) {
    contextmenu_manager::ptr cm = contextmenu_manager::g_create();
    switch (kind) {
    case ContextKind::Tracks: cm->init_context(handles_of(tracks), contextmenu_manager::flag_show_shortcuts); break;
    case ContextKind::Playlist: cm->init_context_playlist(contextmenu_manager::flag_show_shortcuts); break;
    case ContextKind::NowPlaying: {
        metadb_handle_ptr h;
        if (playback_control::get()->get_now_playing(h)) cm->init_context(pfc::list_single_ref_t<metadb_handle_ptr>(h), contextmenu_manager::flag_show_shortcuts);
        else cm->init_context(metadb_handle_list(), contextmenu_manager::flag_show_shortcuts);
        break;
    }
    }
    return std::make_shared<SdkContextMenu>(cm);
}

MenuSourcePtr Panel::main_menu(const std::string& root) {
    const GUID* guid = main_menu_root(root);
    if (!guid) return nullptr;
    auto mm = standard_api_create_t<mainmenu_manager>();
    mainmenu_manager_v2::ptr v2;
    if (!mm->service_query_t(v2)) return nullptr;
    v2->instantiate(*guid);
    return std::make_shared<SdkMainMenu>(v2->generate_menu(mainmenu_manager::flag_show_shortcuts));
}

void Panel::set_cursor(int idc) { m_surface.set_cursor(idc); }

bool Panel::input_box(const std::string& prompt, const std::string& caption, std::string& value) {
    return m_surface.input_box(prompt, caption, value);
}

std::vector<QueueItem> Panel::queue_items() {
    pfc::list_t<t_playback_queue_item> items;
    playlist_manager::get()->queue_get_contents(items);
    std::vector<QueueItem> out;
    for (size_t i = 0; i < items.get_count(); ++i) {
        const auto& q = items[i];
        const bool in_playlist = q.m_playlist != SIZE_MAX && q.m_item != SIZE_MAX;
        out.push_back({ wrap(q.m_handle), in_playlist ? (int)q.m_playlist : -1, in_playlist ? (int)q.m_item : -1 });
    }
    return out;
}

int Panel::replaygain_mode() { return (int)replaygain_manager::get()->get_core_settings().m_source_mode; }

void Panel::set_replaygain_mode(int mode) {
    auto rg = replaygain_manager::get();
    t_replaygain_config cfg = rg->get_core_settings();
    cfg.m_source_mode = (t_uint32)mode;
    rg->set_core_settings(cfg);
}

std::vector<OutputDevice> Panel::output_devices() {
    std::vector<OutputDevice> out;
    auto api = output_manager_v2::tryGet();
    if (!api.is_valid()) return out;
    const outputCoreConfig_t cur = api->getCoreConfig();
    api->listDevices([&](const char* name, const GUID& output, const GUID& device) {
        out.push_back({ name, text_of(output), text_of(device), output == cur.m_output && device == cur.m_device });
    });
    return out;
}

bool Panel::set_output_device(const std::string& output, const std::string& device) {
    auto api = output_manager_v2::tryGet();
    if (!api.is_valid()) return false;
    api->setCoreConfigDevice(guid_of(output), guid_of(device));
    return true;
}

std::vector<std::string> Panel::dsp_presets(int& selected) {
    std::vector<std::string> out;
    selected = -1;
    auto api = dsp_config_manager_v2::tryGet();
    if (!api.is_valid()) return out;
    for (size_t i = 0, n = api->get_preset_count(); i < n; ++i) {
        pfc::string8 name;
        api->get_preset_name(i, name);
        out.push_back(name.c_str());
    }
    selected = index_or_none(api->get_selected_preset());
    return out;
}

bool Panel::set_dsp_preset(int index) {
    auto api = dsp_config_manager_v2::tryGet();
    if (!api.is_valid() || index < 0 || (size_t)index >= api->get_preset_count()) return false;
    api->select_preset((size_t)index);
    return true;
}

bool Panel::library_search(const std::string& query) {
    auto api = library_search_ui::tryGet();
    if (!api.is_valid()) return false;
    api->show(query.c_str());
    return true;
}

unsigned Panel::playlist_lock_mask(int p) { return valid(p) ? playlist_manager::get()->playlist_lock_get_filter_mask((size_t)p) : 0; }

std::string Panel::playlist_lock_name(int p) {
    pfc::string8 name;
    if (!valid(p) || !playlist_manager::get()->playlist_lock_query_name((size_t)p, name)) return {};
    return name.c_str();
}

bool Panel::set_playlist_lock(int p, unsigned mask) {
    if (!valid(p)) return false;
    auto pm = playlist_manager::get();
    auto& locks = js_locks();
    auto it = std::find_if(locks.begin(), locks.end(), [p](const service_ptr_t<JsLock>& l) { return l->index == (size_t)p; });
    if (it == locks.end() && pm->playlist_lock_is_present((size_t)p)) return false;
    if (it != locks.end()) {
        service_ptr_t<JsLock> old = *it;
        locks.erase(it);
        pm->playlist_lock_uninstall((size_t)p, old);
    }
    if (!mask) return true;
    auto lock = fb2k::service_new<JsLock>((t_uint32)mask);
    lock->index = (size_t)p;
    if (!pm->playlist_lock_install((size_t)p, lock)) return false;
    locks.push_back(lock);
    return true;
}

void Panel::drag(Drag kind, int x, int y, unsigned mask, DropAction& action) {
    if (m_engine) m_engine->drag(kind, x, y, mask, action);
    else action.effect = 0;
}

process_locations_notify::ptr Panel::drop_notify(const DropAction& action) {
    auto abort = m_abort;
    return process_locations_notify::create([this, abort, action](metadb_handle_list_cref items) {
        if (!abort->is_aborting() && items.get_count()) insert_playlist_items(action.playlist, action.base, wrap_all(items), action.toSelect);
    });
}

void Panel::drop_locations(const std::vector<std::string>& paths, const DropAction& action) {
    if (paths.empty()) return;
    pfc::list_t<const char*> urls;
    for (const auto& p : paths) urls.add_item(p.c_str());
    playlist_incoming_item_filter_v2::get()->process_locations_async(urls, playlist_incoming_item_filter_v2::op_flag_delay_ui, nullptr,
                                                                     nullptr, nullptr, drop_notify(action));
}

void Panel::add_locations(int p, const std::vector<std::string>& paths, bool select) {
    if (!valid(p)) return;
    DropAction action;
    action.playlist = p;
    action.base = playlist_item_count(p);
    action.toSelect = select;
    drop_locations(paths, action);
}

int Panel::drag_tracks(const Tracks& tracks, int effects) {
    auto handles = handles_of(tracks);
    return handles.get_count() ? m_surface.drag_tracks(handles, effects) : 0;
}

bool Panel::copy_tracks(const Tracks& tracks) {
    auto handles = handles_of(tracks);
    return handles.get_count() && ui::copy_tracks(handles);
}

bool Panel::clipboard_has_tracks() { return ui::clipboard_has_tracks(); }

Tracks Panel::clipboard_tracks() {
    metadb_handle_list out;
    return ui::clipboard_tracks(out) ? wrap_all(out) : Tracks();
}

}
