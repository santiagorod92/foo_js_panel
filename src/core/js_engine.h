#pragma once
#include "gfx/canvas.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace jsp {

enum class Playback { Play, Pause, PlayOrPause, Stop, Next, Prev };

struct LibraryAlbum {
    std::string id, name, artist, artistId, coverArtId, created;
    int year = 0, songCount = 0;
};

struct LibraryArtist {
    std::string id, name;
    int albumCount = 0, songCount = 0;
};

struct Track {
    virtual ~Track() = default;
};
using TrackPtr = std::shared_ptr<Track>;
using Tracks = std::vector<TrackPtr>;
using ArtDone = std::function<void(pui::gfx::ImagePtr image, std::string path)>;

enum class LibraryState { Missing, NotConfigured, Ready };
using AlbumsDone = std::function<void(std::vector<LibraryAlbum> albums, std::string error)>;

std::vector<LibraryArtist> library_artists(const std::vector<LibraryAlbum>& albums);

class JsHost {
public:
    virtual ~JsHost() = default;
    virtual void repaint() = 0;
    virtual void repaint_rect(const pui::gfx::Rect& r) = 0;
    virtual void set_timer(int id, int ms) = 0;
    virtual void kill_timer(int id) = 0;
    virtual std::string titleformat(const std::string& expr) = 0;
    virtual bool is_playing() = 0;
    virtual bool is_paused() = 0;
    virtual double playback_time() = 0;
    virtual double playback_length() = 0;
    virtual void playback(Playback cmd) = 0;
    virtual void console(const std::string& text) = 0;
    virtual bool read_text_file(const std::string& path, std::string& out) = 0;
    virtual LibraryState navidrome_state() { return LibraryState::Missing; }
    virtual void navidrome_albums(AlbumsDone done) { done({}, "foo_navidrome isn't installed"); }
    virtual void navidrome_play(bool, const std::string&, bool, bool) {}
    virtual TrackPtr now_playing() { return nullptr; }
    virtual Tracks library_items() { return {}; }
    virtual std::string track_path(const TrackPtr&) { return {}; }
    virtual bool same_track(const TrackPtr& a, const TrackPtr& b) { return a == b; }
    virtual std::vector<std::string> format_tracks(const std::string&, const Tracks& tracks) {
        return std::vector<std::string>(tracks.size());
    }
    virtual void album_art(const TrackPtr&, int, ArtDone done) { done(nullptr, {}); }
    virtual pui::gfx::ImagePtr load_image(const std::string&) { return nullptr; }
    virtual int playlist_count() { return 0; }
    virtual int active_playlist() { return -1; }
    virtual void set_active_playlist(int) {}
    virtual int playing_playlist() { return -1; }
    virtual std::string playlist_name(int) { return {}; }
    virtual int find_or_create_playlist(const std::string&, bool) { return -1; }
    virtual void clear_playlist(int) {}
    virtual void insert_playlist_items(int, int, const Tracks&, bool) {}
    virtual void execute_default_action(int, int) {}
    virtual bool key_pressed(int) { return false; }
};

enum class Mouse { Move, LeftDown, LeftUp, LeftDouble, RightDown, RightUp, Leave };

class JsEngine {
public:
    static constexpr std::size_t kMemoryLimit = 512u * 1024 * 1024;
    static constexpr int kCallBudgetMs = 5000;

    explicit JsEngine(JsHost& host, int callBudgetMs = kCallBudgetMs);
    ~JsEngine();
    JsEngine(const JsEngine&) = delete;
    JsEngine& operator=(const JsEngine&) = delete;

    bool load(const std::string& source, const std::string& path);
    bool failed() const;
    const std::string& error() const;

    void paint(pui::gfx::Canvas& canvas);
    void resize(int w, int h);
    bool mouse(Mouse kind, int x, int y, unsigned mask);
    void wheel(int step);
    bool key_down(int vk);
    void timer(int id);
    void playback_new_track(const TrackPtr& track = nullptr);
    enum class LibraryChange { Added, Removed, Changed };
    void library_changed(LibraryChange kind, Tracks tracks);
    void playback_stop(int reason);
    void playback_pause(bool paused);
    void playback_time(double t);
    void playback_starting(int cmd, bool paused);

    struct Impl;

private:
    std::unique_ptr<Impl> m;
};

std::string script_dir_of(const std::string& path);

std::string check_syntax(const std::string& source, const std::string& name);

struct TextPos {
    int line = 1, col = 1;
};
TextPos text_pos(const std::string& utf8, std::size_t byteOffset);
std::string editor_status(const TextPos& pos, const std::string& syntaxError, const std::string& applied);

}
