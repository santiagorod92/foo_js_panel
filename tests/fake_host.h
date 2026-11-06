#pragma once
#include "core/js_engine.h"
#include "recording_canvas.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace t {

struct FakeTrack : jsp::Track {
    std::string path;
    std::map<std::string, std::string> fields;
    jsp::TrackInfo info;
};

struct FakeImage : pui::gfx::Image {
    int w, h;
    pui::gfx::Pixels px;
    FakeImage(int w_, int h_, uint32_t fill = 0xFF102030u) : w(w_), h(h_) {
        px.w = w_;
        px.h = h_;
        px.pargb.assign((size_t)w_ * h_, fill);
    }
    explicit FakeImage(pui::gfx::Pixels p) : w(p.w), h(p.h), px(std::move(p)) {}
    int width() const override { return w; }
    int height() const override { return h; }
    pui::gfx::Color average_color() const override { return {}; }
};

inline std::shared_ptr<FakeTrack> track(const std::string& path, std::map<std::string, std::string> fields = {}) {
    auto t = std::make_shared<FakeTrack>();
    t->path = path;
    t->fields = std::move(fields);
    return t;
}

struct SoftCanvas : RecordingCanvas {
    pui::gfx::Pixels& px;
    explicit SoftCanvas(pui::gfx::Pixels& p) : px(p) {}
    int width() const override { return px.w; }
    int height() const override { return px.h; }
    void put(const pui::gfx::Rect& r, pui::gfx::Color c, int a) {
        for (int y = std::max(0, r.y); y < std::min(px.h, r.bottom()); ++y)
            for (int x = std::max(0, r.x); x < std::min(px.w, r.right()); ++x) {
                uint32_t& d = px.pargb[(size_t)y * px.w + x];
                auto mix = [&](int s, int v) { return (uint32_t)((v * a + (int)((d >> s) & 0xFF) * (255 - a)) / 255) << s; };
                d = mix(24, 255) | mix(16, c.r) | mix(8, c.g) | mix(0, c.b);
            }
    }
    void fill_rect(const pui::gfx::Rect& r, pui::gfx::Color c) override {
        RecordingCanvas::fill_rect(r, c);
        put(r, c, 255);
    }
    void fill_rect_alpha(const pui::gfx::Rect& r, pui::gfx::Color c, int a) override {
        RecordingCanvas::fill_rect_alpha(r, c, a);
        put(r, c, a);
    }
};

struct FakeMenu : jsp::MenuSource {
    std::string name;
    std::vector<std::string> executed;
    jsp::Menu build(int base, int maxId) override {
        std::vector<std::string> cmds = { "Play", "Properties" };
        jsp::Menu out;
        jsp::MenuItem group;
        group.text = name;
        group.submenu = true;
        for (size_t i = 0; i < cmds.size(); ++i) {
            const int id = base + (int)i;
            if (maxId >= 0 && id > maxId) break;
            jsp::MenuItem m;
            m.text = cmds[i];
            m.id = id;
            m.checked = i == 0;
            group.children.push_back(m);
        }
        out.push_back(group);
        return out;
    }
    bool execute(int id) override {
        executed.push_back(name + " " + std::to_string(id));
        return id >= 0 && id < 2;
    }
};

struct FakeHost : jsp::JsHost {
    int repaints = 0;
    std::vector<pui::gfx::Rect> rects;
    std::map<int, int> timers;
    std::vector<std::string> console_lines;
    std::vector<jsp::Playback> commands;
    std::map<std::string, std::string> files;
    std::string profilePath = "/profile/";
    std::map<std::string, std::string> fields;
    bool playing = false, paused = false;
    jsp::LibraryState library = jsp::LibraryState::Missing;
    std::vector<jsp::LibraryAlbum> albums;
    std::string libraryError;
    bool deferLibrary = false;
    std::vector<jsp::AlbumsDone> waiting;
    std::vector<std::string> played;
    jsp::TrackPtr playingTrack;
    jsp::Tracks libraryTracks;
    std::map<std::string, pui::gfx::ImagePtr> art;
    std::vector<std::pair<jsp::TrackPtr, jsp::ArtDone>> artWaiting;
    bool deferArt = false;
    std::vector<std::string> playlists = { "Default" };
    std::map<int, jsp::Tracks> playlistItems;
    int active = 0;
    std::vector<std::string> actions;
    std::map<int, bool> keys;
    double time = 0, length = 0;
    std::string properties;
    int propertySaves = 0;
    std::vector<std::pair<std::string, std::string>> notes;
    double vol = 0;
    std::map<jsp::Flag, bool> flags;
    int rgMode = 0;
    std::vector<jsp::OutputDevice> outputs;
    std::vector<std::string> dsp;
    int dspSelected = -1;
    std::vector<std::string> searches;
    std::map<int, unsigned> locks;
    std::map<int, std::string> foreignLocks;
    std::vector<std::pair<size_t, int>> drags;
    int dragResult = 0;
    jsp::Tracks clipTracks;
    std::vector<std::string> tips;
    bool themed = false;
    uint32_t picked = 0;
    std::string editedProperties;
    std::vector<double> seeks;
    std::set<std::string> missingFonts;
    std::vector<std::string> commands_run;
    std::map<int, std::vector<bool>> selection;
    std::map<int, int> focus;
    int order = 0;
    jsp::ItemLocation location;
    std::vector<jsp::QueueItem> queue;
    jsp::Menu shownMenu;
    std::function<int(const jsp::Menu&)> menuChoice = [](const jsp::Menu&) { return 0; };
    std::shared_ptr<FakeMenu> lastSource;
    std::vector<int> cursors;
    bool inputOk = true;
    std::string inputValue, inputPrompt;
    std::vector<std::pair<std::string, jsp::ArtDone>> loads;
    std::vector<std::string> saved;
    std::vector<std::string> canvasOps;
    std::map<std::string, jsp::HttpResponse> web;
    std::vector<std::pair<jsp::HttpRequest, jsp::HttpDone>> httpWaiting;
    std::vector<std::string> requests, runs, boxes;
    std::string clip;
    int boxAnswer = 6;
    std::vector<std::string> selectionLog;
    jsp::Tracks uiSelection;
    std::vector<std::string> tagWrites;
    std::map<std::string, std::string> puiVars;
    std::vector<std::string> puiActions, windowCalls;
    int winState = 0;
    bool full = false;

    void repaint() override { ++repaints; }
    void repaint_rect(const pui::gfx::Rect& r) override { rects.push_back(r); }
    void set_timer(int id, int ms) override { timers[id] = ms; }
    void kill_timer(int id) override { timers.erase(id); }
    std::string titleformat(const std::string& expr) override {
        auto it = fields.find(expr);
        return it == fields.end() ? "?" : it->second;
    }
    bool is_playing() override { return playing; }
    bool is_paused() override { return paused; }
    double playback_time() override { return time; }
    double playback_length() override { return length; }
    void playback(jsp::Playback cmd) override { commands.push_back(cmd); }
    void console(const std::string& text) override { console_lines.push_back(text); }
    jsp::LibraryState navidrome_state() override { return library; }
    void navidrome_albums(jsp::AlbumsDone done) override {
        if (deferLibrary) waiting.push_back(std::move(done));
        else done(albums, libraryError);
    }
    std::map<std::string, jsp::ArtistInfo> artistInfos;
    void navidrome_artist_info(const std::string& name, jsp::ArtistInfoDone done) override {
        auto it = artistInfos.find(name);
        if (it == artistInfos.end()) done({}, "no artist named \"" + name + "\" on the server");
        else done(it->second, "");
    }
    std::map<std::string, jsp::SearchResult> navidromeResults;
    void navidrome_search(const std::string& query, jsp::SearchDone done) override {
        auto it = navidromeResults.find(query);
        if (it == navidromeResults.end()) done({}, "search failed for \"" + query + "\"");
        else done(it->second, "");
    }
    void navidrome_play(bool artist, const std::string& id, bool replace, bool play) override {
        played.push_back(std::string(artist ? "artist " : "album ") + id + (replace ? " replace" : " add") + (play ? " play" : ""));
    }
    jsp::TrackPtr now_playing() override { return playingTrack; }
    jsp::Tracks library_items() override { return libraryTracks; }
    std::string track_path(const jsp::TrackPtr& t) override {
        auto* f = dynamic_cast<FakeTrack*>(t.get());
        return f ? f->path : std::string();
    }
    std::map<std::string, std::string> sortKeys;
    std::string path_sort_key(const jsp::TrackPtr& t) override {
        auto it = sortKeys.find(track_path(t));
        return it == sortKeys.end() ? track_path(t) : it->second;
    }
    std::vector<std::string> format_tracks(const std::string& expr, const jsp::Tracks& tracks) override {
        std::vector<std::string> out;
        for (const auto& t : tracks) {
            auto* f = dynamic_cast<FakeTrack*>(t.get());
            auto it = f ? f->fields.find(expr) : decltype(f->fields.end()){};
            out.push_back(f && it != f->fields.end() ? it->second : std::string());
        }
        return out;
    }
    void album_art(const jsp::TrackPtr& t, int, jsp::ArtDone done) override {
        if (deferArt) {
            artWaiting.emplace_back(t, std::move(done));
            return;
        }
        auto it = art.find(track_path(t));
        done(it == art.end() ? nullptr : it->second, it == art.end() ? std::string() : track_path(t) + ".jpg");
    }
    pui::gfx::ImagePtr load_image(const std::string& path) override {
        auto it = art.find(path);
        return it == art.end() ? nullptr : it->second;
    }
    int playlist_count() override { return (int)playlists.size(); }
    int active_playlist() override { return active; }
    void set_active_playlist(int p) override { active = p; actions.push_back("activate " + std::to_string(p)); }
    int playing_playlist() override { return -1; }
    std::string playlist_name(int p) override { return p >= 0 && p < (int)playlists.size() ? playlists[(size_t)p] : std::string(); }
    int find_or_create_playlist(const std::string& name, bool) override {
        for (size_t i = 0; i < playlists.size(); ++i)
            if (playlists[i] == name) return (int)i;
        playlists.push_back(name);
        return (int)playlists.size() - 1;
    }
    void clear_playlist(int p) override { playlistItems[p].clear(); actions.push_back("clear " + std::to_string(p)); }
    void add_locations(int p, const std::vector<std::string>& paths, bool select) override {
        std::string all;
        for (const auto& path : paths) all += " " + path;
        actions.push_back("locations " + std::to_string(p) + all + (select ? " select" : ""));
    }
    void insert_playlist_items(int p, int base, const jsp::Tracks& tracks, bool) override {
        auto& v = playlistItems[p];
        v.insert(v.begin() + std::min<size_t>((size_t)std::max(0, base), v.size()), tracks.begin(), tracks.end());
        actions.push_back("insert " + std::to_string(p) + " " + std::to_string(tracks.size()));
    }
    void execute_default_action(int p, int i) override { actions.push_back("play " + std::to_string(p) + " " + std::to_string(i)); }
    bool key_pressed(int vk) override { return keys[vk]; }
    std::string load_properties() override { return properties; }
    void save_properties(const std::string& json) override {
        properties = json;
        ++propertySaves;
    }
    std::vector<std::vector<NotifyItem>> noteItems;
    void notify_others(const std::string& name, const std::string& json, const std::vector<NotifyItem>& items) override {
        notes.emplace_back(name, json);
        noteItems.push_back(items);
    }
    std::string profile_path() override { return profilePath; }
    double volume() override { return vol; }
    void set_volume(double v) override { vol = v; }
    bool flag(jsp::Flag f) override { return flags[f]; }
    void set_flag(jsp::Flag f, bool v) override { flags[f] = v; }
    bool run_main_menu_command(const std::string& path) override {
        commands_run.push_back("main " + path);
        return path == "View/Console";
    }
    bool run_context_command(const std::string& path, const jsp::Tracks* tracks) override {
        commands_run.push_back("context " + path + " " + std::to_string(tracks ? tracks->size() : 0));
        return true;
    }
    jsp::Tracks query_items(const jsp::Tracks& tracks, const std::string& query, std::string& error) override {
        if (query == "bad(") {
            error = "syntax error";
            return {};
        }
        jsp::Tracks out;
        for (const auto& t : tracks)
            if (track_path(t).find(query) != std::string::npos) out.push_back(t);
        return out;
    }
    bool has_component(const std::string& name, bool) override { return name == "foo_playcount"; }
    jsp::TrackInfo track_info(const jsp::TrackPtr& t) override {
        auto* f = dynamic_cast<FakeTrack*>(t.get());
        return f ? f->info : jsp::TrackInfo{};
    }
    bool write_text_file(const std::string& path, const std::string& text) override {
        files[path] = text;
        return true;
    }
    int playlist_item_count(int p) override { return (int)playlistItems[p].size(); }
    jsp::Tracks playlist_items(int p, bool selected) override {
        jsp::Tracks out;
        const auto& items = playlistItems[p];
        for (size_t i = 0; i < items.size(); ++i)
            if (!selected || (i < selection[p].size() && selection[p][i])) out.push_back(items[i]);
        return out;
    }
    std::vector<bool> playlist_selection(int p) override {
        auto& sel = selection[p];
        sel.resize(playlistItems[p].size());
        return sel;
    }
    void set_playlist_selection(int p, const std::vector<int>& items, bool state) override {
        auto& sel = selection[p];
        sel.resize(playlistItems[p].size());
        for (int i : items)
            if (i >= 0 && i < (int)sel.size()) sel[(size_t)i] = state;
    }
    void clear_playlist_selection(int p) override { selection[p].assign(playlistItems[p].size(), false); }
    int playlist_focus(int p) override { return focus.count(p) ? focus[p] : -1; }
    void set_playlist_focus(int p, int i) override { focus[p] = i; }
    int playback_order() override { return order; }
    void set_playback_order(int o) override { order = o; }
    jsp::ItemLocation playing_location() override { return location; }
    bool reorder_playlist(int p, const std::vector<int>& order_) override {
        jsp::Tracks out;
        for (int i : order_) out.push_back(playlistItems[p][(size_t)i]);
        playlistItems[p] = out;
        return true;
    }
    void queue_add(const jsp::TrackPtr& t) override { queue.push_back({ t }); }
    void queue_add_playlist_item(int p, int item) override { queue.push_back({ playlistItems[p].at((size_t)item), p, item }); }
    void queue_flush() override { queue.clear(); }
    void queue_remove(const std::vector<int>& indices) override {
        std::vector<jsp::QueueItem> kept;
        for (size_t i = 0; i < queue.size(); ++i)
            if (std::find(indices.begin(), indices.end(), (int)i) == indices.end()) kept.push_back(queue[i]);
        queue = kept;
    }
    std::vector<jsp::QueueItem> queue_items() override { return queue; }
    int popup_menu(int, int, const jsp::Menu& menu) override {
        shownMenu = menu;
        return menuChoice(menu);
    }
    jsp::MenuSourcePtr context_menu(jsp::ContextKind kind, const jsp::Tracks& tracks) override {
        lastSource = std::make_shared<FakeMenu>();
        lastSource->name = kind == jsp::ContextKind::Tracks ? "tracks:" + std::to_string(tracks.size())
                         : kind == jsp::ContextKind::Playlist ? "playlist" : "nowplaying";
        return lastSource;
    }
    jsp::MenuSourcePtr main_menu(const std::string& root) override {
        if (root != "Playback") return nullptr;
        lastSource = std::make_shared<FakeMenu>();
        lastSource->name = root;
        return lastSource;
    }
    void set_cursor(int idc) override { cursors.push_back(idc); }
    bool input_box(const std::string& prompt, const std::string&, std::string& value) override {
        inputPrompt = prompt;
        if (inputOk) value = inputValue;
        return inputOk;
    }
    bool image_pixels(const pui::gfx::Image& img, pui::gfx::Pixels& out) override {
        auto* f = dynamic_cast<const FakeImage*>(&img);
        if (!f) return false;
        out = f->px;
        return true;
    }
    pui::gfx::ImagePtr image_from_pixels(const pui::gfx::Pixels& px) override { return std::make_shared<FakeImage>(px); }
    struct LoggingSoftCanvas : SoftCanvas {
        std::vector<std::string>& sink;
        LoggingSoftCanvas(pui::gfx::Pixels& p, std::vector<std::string>& s) : SoftCanvas(p), sink(s) {}
        ~LoggingSoftCanvas() override { sink.insert(sink.end(), ops.begin(), ops.end()); }
    };
    std::unique_ptr<pui::gfx::Canvas> pixels_canvas(pui::gfx::Pixels& px, double) override {
        auto c = std::make_unique<LoggingSoftCanvas>(px, canvasOps);
        c->missingFonts = missingFonts;
        return c;
    }
    bool save_png(const pui::gfx::Image& img, const std::string& path) override {
        saved.push_back(path + " " + std::to_string(img.width()) + "x" + std::to_string(img.height()));
        return true;
    }
    pui::gfx::ImagePtr album_art_sync(const jsp::TrackPtr& t, int, std::string& path) override {
        auto it = art.find(track_path(t));
        if (it == art.end()) return nullptr;
        path = track_path(t) + ".jpg";
        return it->second;
    }
    void load_image_async(const std::string& path, jsp::ArtDone done) override { loads.emplace_back(path, std::move(done)); }
    void finish_loads() {
        auto pending = std::move(loads);
        loads.clear();
        for (auto& [path, done] : pending) done(load_image(path), path);
    }
    jsp::HttpResponse respond(const jsp::HttpRequest& r) {
        std::string line = r.method + " " + r.url;
        for (const auto& h : r.headers) line += " [" + h + "]";
        if (!r.body.empty()) line += " body=" + r.body;
        requests.push_back(line);
        auto it = web.find(r.url);
        return it == web.end() ? jsp::HttpResponse{ 404, "not found", {} } : it->second;
    }
    void http_request(const jsp::HttpRequest& r, jsp::HttpDone done) override { httpWaiting.emplace_back(r, std::move(done)); }
    jsp::HttpResponse http_request_sync(const jsp::HttpRequest& r) override { return respond(r); }
    void finish_http() {
        auto pending = std::move(httpWaiting);
        httpWaiting.clear();
        for (auto& [r, done] : pending) done(respond(r));
    }
    bool shell_run(const std::string& command) override {
        runs.push_back(command);
        return command.rfind("http", 0) == 0;
    }
    std::string clipboard_text() override { return clip; }
    void ui_selection(SelectionMode mode, const jsp::Tracks& tracks) override {
        selectionLog.push_back(std::to_string((int)mode) + ":" + std::to_string(tracks.size()));
        if (mode == SelectionMode::Tracks) uiSelection = tracks;
    }
    jsp::Tracks get_ui_selection() override { return uiSelection; }
    bool panels_ui_available() override { return true; }
    bool panels_ui_variable(const std::string& n, std::string& out) override {
        auto it = puiVars.find(n);
        if (it == puiVars.end()) return false;
        out = it->second;
        return true;
    }
    void set_panels_ui_variable(const std::string& n, const std::string& v) override { puiVars[n] = v; }
    bool panels_ui_action(const std::string& a) override {
        puiActions.push_back(a);
        return true;
    }
    bool shown = true;
    bool visible() override { return shown; }
    int window_state() override { return winState; }
    void set_window_state(int s) override { winState = s; windowCalls.push_back("state " + std::to_string(s)); }
    bool fullscreen() override { return full; }
    void set_fullscreen(bool f) override { full = f; windowCalls.push_back(std::string("full ") + (f ? "1" : "0")); }
    void set_caption(const pui::gfx::Rect& r) override {
        windowCalls.push_back("caption " + std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," + std::to_string(r.h));
    }
    void set_window_limits(int a, int b, int c, int d) override {
        windowCalls.push_back("limits " + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c) + "," + std::to_string(d));
    }
    bool update_tags(const jsp::Tracks& tracks, const std::vector<TagSet>& sets) override {
        for (size_t i = 0; i < tracks.size(); ++i) {
            std::string line = track_path(tracks[i]);
            for (const auto& [k, vs] : sets[i]) {
                line += " " + k + "=";
                for (size_t j = 0; j < vs.size(); ++j) line += (j ? "|" : "") + vs[j];
            }
            tagWrites.push_back(line);
        }
        return true;
    }
    bool set_clipboard_text(const std::string& t) override {
        clip = t;
        return true;
    }
    int message_box(const std::string& text, const std::string& caption, int type) override {
        boxes.push_back(caption + ": " + text + " (" + std::to_string(type) + ")");
        return boxAnswer;
    }
    int replaygain_mode() override { return rgMode; }
    void set_replaygain_mode(int m) override { rgMode = m; }
    std::vector<jsp::OutputDevice> output_devices() override { return outputs; }
    bool set_output_device(const std::string& o, const std::string& d) override {
        for (auto& x : outputs) x.active = x.output == o && x.device == d;
        return true;
    }
    std::vector<std::string> dsp_presets(int& selected) override {
        selected = dspSelected;
        return dsp;
    }
    bool set_dsp_preset(int i) override {
        if (i < 0 || i >= (int)dsp.size()) return false;
        dspSelected = i;
        return true;
    }
    bool library_search(const std::string& q) override {
        searches.push_back(q);
        return true;
    }
    unsigned playlist_lock_mask(int p) override { return foreignLocks.count(p) ? 0x7fu : locks[p]; }
    std::string playlist_lock_name(int p) override { return foreignLocks.count(p) ? foreignLocks[p] : locks[p] ? "JS Panel" : ""; }
    bool set_playlist_lock(int p, unsigned mask) override {
        if (foreignLocks.count(p)) return false;
        locks[p] = mask;
        return true;
    }
    int drag_tracks(const jsp::Tracks& t, int effects) override {
        drags.push_back({ t.size(), effects });
        return dragResult;
    }
    bool copy_tracks(const jsp::Tracks& t) override {
        clipTracks = t;
        return true;
    }
    bool clipboard_has_tracks() override { return !clipTracks.empty(); }
    bool ui_colour(jsp::UiColour kind, uint32_t& argb) override {
        if (!themed) return false;
        argb = 0xff000000u + (uint32_t)kind;
        return true;
    }
    bool ui_font(int kind, jsp::UiFont& font) override {
        if (!themed) return false;
        font = { "Face" + std::to_string(kind), 10.0f + kind, kind == 3 ? 1 : 0 };
        return true;
    }
    bool sys_colour(int index, uint32_t& argb) override {
        if (index != 5) return false;
        argb = 0xffabcdefu;
        return true;
    }
    void edit_properties(const std::string& json) override { editedProperties = json; }
    void seek(double t) override { seeks.push_back(t); }
    std::string jsp3_path() override { return "/jsp3/"; }
    std::string bundledLodash;
    std::string bundled_import(const std::string& name) override { return name == "lodash" ? bundledLodash : std::string(); }
    bool pick_colour(uint32_t& argb) override {
        if (!picked) return false;
        argb = picked;
        return true;
    }
    void tooltip(const std::string& text, bool active, int maxWidth) override {
        tips.push_back(text + (active ? " on " : " off ") + std::to_string(maxWidth));
    }
    jsp::Tracks clipboard_tracks() override { return clipTracks; }
    bool read_text_file(const std::string& path, std::string& out) override {
        std::string key = path;
        std::replace(key.begin(), key.end(), '\\', '/');
        auto it = files.find(key);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
};

}
