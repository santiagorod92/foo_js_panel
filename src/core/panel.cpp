#include "panel.h"
#include "fs_util.h"
#include "log.h"
#include "navidrome_library_api.h"
#include "sample_script.h"
#include <algorithm>
#include <filesystem>
#include <thread>

namespace jsp {

namespace {

enum MenuItem { kEdit, kChoose, kReload, kSample };

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

std::vector<Panel*>& Panel::live() {
    static std::vector<Panel*> panels;
    return panels;
}

Panel::Panel(Surface& surface, std::string scriptPath, std::function<void(const std::string&)> onScriptChanged)
    : play_callback_impl_base(flag_on_playback_starting | flag_on_playback_new_track | flag_on_playback_stop |
                              flag_on_playback_pause | flag_on_playback_time),
      m_surface(surface), m_path(std::move(scriptPath)), m_onScriptChanged(std::move(onScriptChanged)) {
    live().push_back(this);
    m_artNotify = now_playing_album_art_notify_manager::get()->add([this](album_art_data::ptr data) { on_now_playing_art(data); });
    reload();
}

Panel::~Panel() {
    m_abort->set();
    if (m_artNotify) now_playing_album_art_notify_manager::get()->remove(m_artNotify);
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

void Panel::set_script(const std::string& path) {
    m_path = path;
    if (m_onScriptChanged) m_onScriptChanged(m_path);
    reload();
}

void Panel::reload() {
    m_engine.reset();
    m_tf.clear();
    m_engine = std::make_unique<JsEngine>(*this);
    std::string source;
    if (m_path.empty()) source = sample_script();
    else if (!read_text_file(m_path, source)) {
        log::console(log::Level::Warn, "js", "can't read the script " + m_path + ", running the built-in sample");
        source = sample_script();
    }
    const unsigned long long t0 = tick_ms();
    const bool ok = m_engine->load(source, m_path);
    log::info("js", "loaded " + label(m_path) + " in " + std::to_string(tick_ms() - t0) + " ms" +
                        (ok ? "" : " (failed)"));
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

void Panel::wheel(int step) {
    if (m_engine) m_engine->wheel(step);
}

bool Panel::key_down(int vk) { return m_engine && m_engine->key_down(vk); }

bool Panel::key_pressed(int vk) { return m_surface.key_pressed(vk); }

void Panel::timer(int id) {
    if (m_engine) m_engine->timer(id);
    else m_surface.stop_timer(id);
}

void Panel::context_menu(int x, int y) {
    std::vector<std::string> items = { "Edit script…", "Choose script file…", "Reload script" };
    if (!m_path.empty()) items.push_back("Use the built-in sample");
    switch (m_surface.popup_menu(x, y, items)) {
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
    m_surface.open_editor(title, current_source(),
                          { [this](const std::string& src) { return apply_source(src); },
                            [this](const std::string& src) { return check_syntax(src, m_path.empty() ? "script.js" : m_path); } });
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
        titleformat_compiler::get()->compile_safe(obj, expr.c_str());
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

void Panel::playback(Playback cmd) {
    auto pc = playback_control::get();
    switch (cmd) {
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

TrackPtr Panel::now_playing() {
    metadb_handle_ptr h;
    return playback_control::get()->get_now_playing(h) ? wrap(h) : nullptr;
}

Tracks Panel::library_items() {
    pfc::list_t<metadb_handle_ptr> all;
    library_manager::get()->get_all_items(all);
    Tracks out;
    out.reserve(all.get_count());
    for (size_t i = 0; i < all.get_count(); ++i) out.push_back(wrap(all[i]));
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
    if (!m_engine) return;
    Tracks tracks;
    tracks.reserve(items.get_count());
    for (size_t i = 0; i < items.get_count(); ++i) tracks.push_back(wrap(items[i]));
    m_engine->library_changed(kind, std::move(tracks));
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

}
