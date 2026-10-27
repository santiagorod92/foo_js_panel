#pragma once
#include "core/js_engine.h"
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace t {

struct FakeTrack : jsp::Track {
    std::string path;
    std::map<std::string, std::string> fields;
};

struct FakeImage : pui::gfx::Image {
    int w, h;
    FakeImage(int w_, int h_) : w(w_), h(h_) {}
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

struct FakeHost : jsp::JsHost {
    int repaints = 0;
    std::vector<pui::gfx::Rect> rects;
    std::map<int, int> timers;
    std::vector<std::string> console_lines;
    std::vector<jsp::Playback> commands;
    std::map<std::string, std::string> files;
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
    void navidrome_play(bool artist, const std::string& id, bool replace, bool play) override {
        played.push_back(std::string(artist ? "artist " : "album ") + id + (replace ? " replace" : " add") + (play ? " play" : ""));
    }
    jsp::TrackPtr now_playing() override { return playingTrack; }
    jsp::Tracks library_items() override { return libraryTracks; }
    std::string track_path(const jsp::TrackPtr& t) override {
        auto* f = dynamic_cast<FakeTrack*>(t.get());
        return f ? f->path : std::string();
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
    void insert_playlist_items(int p, int base, const jsp::Tracks& tracks, bool) override {
        auto& v = playlistItems[p];
        v.insert(v.begin() + std::min<size_t>((size_t)std::max(0, base), v.size()), tracks.begin(), tracks.end());
        actions.push_back("insert " + std::to_string(p) + " " + std::to_string(tracks.size()));
    }
    void execute_default_action(int p, int i) override { actions.push_back("play " + std::to_string(p) + " " + std::to_string(i)); }
    bool key_pressed(int vk) override { return keys[vk]; }
    bool read_text_file(const std::string& path, std::string& out) override {
        auto it = files.find(path);
        if (it == files.end()) return false;
        out = it->second;
        return true;
    }
};

}
