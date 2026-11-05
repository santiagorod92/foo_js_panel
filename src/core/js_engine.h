#pragma once
#include "gfx/canvas.h"
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace jsp {

enum class Playback { Play, Pause, PlayOrPause, Stop, Next, Prev, Random };

struct LibraryAlbum {
    std::string id, name, artist, artistId, coverArtId, created;
    int year = 0, songCount = 0;
};

struct LibraryArtist {
    std::string id, name;
    int albumCount = 0, songCount = 0;
};

struct ArtistInfo {
    std::string id, biography, imageUrl, lastFmUrl;
};
using ArtistInfoDone = std::function<void(ArtistInfo info, std::string error)>;

struct Track {
    virtual ~Track() = default;
};
using TrackPtr = std::shared_ptr<Track>;
using Tracks = std::vector<TrackPtr>;
using ArtDone = std::function<void(pui::gfx::ImagePtr image, std::string path)>;

struct TrackInfo {
    std::vector<std::pair<std::string, std::vector<std::string>>> meta;
    std::vector<std::pair<std::string, std::string>> info;
    double length = 0;
    long long size = -1;
    unsigned subsong = 0;
    double modified = 0;
};

enum class Flag { StopAfterCurrent, CursorFollowPlayback, PlaybackFollowCursor, AlwaysOnTop };

struct OutputDevice {
    std::string name, output, device;
    bool active = false;
};

struct DropAction {
    int effect = 0, playlist = -1, base = 0;
    bool toSelect = true;
    std::string text;
};

enum class Drag { Enter, Over, Leave, Drop };

enum class UiColour { Text, Background, Highlight, Selection, SelectionText, InactiveSelection };

struct UiFont {
    std::string face;
    float px = 12;
    int style = 0;
};

struct ItemLocation {
    int playlist = -1, item = -1;
};

struct MenuItem {
    std::string text;
    int id = 0;
    bool separator = false, submenu = false, checked = false, radio = false, disabled = false;
    std::vector<MenuItem> children;
};
using Menu = std::vector<MenuItem>;

class MenuSource {
public:
    virtual ~MenuSource() = default;
    virtual Menu build(int baseId, int maxId) = 0;
    virtual bool execute(int id) = 0;
};
using MenuSourcePtr = std::shared_ptr<MenuSource>;

enum class ContextKind { Tracks, Playlist, NowPlaying };

struct HttpRequest {
    std::string method, url, body;
    std::vector<std::string> headers;
};
struct HttpResponse {
    int status = 0;
    std::string text, error;
};
using HttpDone = std::function<void(HttpResponse)>;

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
    virtual void seek(double) {}
    virtual void console(const std::string& text) = 0;
    virtual bool read_text_file(const std::string& path, std::string& out) = 0;
    virtual LibraryState navidrome_state() { return LibraryState::Missing; }
    virtual void navidrome_albums(AlbumsDone done) { done({}, "foo_navidrome isn't installed"); }
    virtual void navidrome_play(bool, const std::string&, bool, bool) {}
    virtual void navidrome_artist_info(const std::string&, ArtistInfoDone done) { done({}, "foo_navidrome isn't installed"); }
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

    virtual std::string load_properties() { return {}; }
    virtual void save_properties(const std::string&) {}
    struct NotifyItem {
        Tracks tracks;
        bool single = false;
    };
    virtual void notify_others(const std::string&, const std::string&, const std::vector<NotifyItem>&) {}
    virtual void reload_script() {}
    virtual void edit_script() {}
    virtual std::string profile_path() { return {}; }
    virtual std::string component_path() { return {}; }
    virtual std::string foobar_path() { return {}; }
    virtual std::string version() { return {}; }
    virtual double volume() { return 0; }
    virtual void set_volume(double) {}
    virtual bool flag(Flag) { return false; }
    virtual void set_flag(Flag, bool) {}
    virtual bool run_main_menu_command(const std::string&) { return false; }
    virtual bool run_context_command(const std::string&, const Tracks*) { return false; }
    virtual void popup_message(const std::string&, const std::string&) {}
    virtual TrackPtr focus_item() { return nullptr; }
    virtual Tracks query_items(const Tracks&, const std::string&, std::string& error) {
        error = "queries aren't available";
        return {};
    }
    virtual bool in_library(const TrackPtr&) { return false; }
    virtual std::string library_relative_path(const TrackPtr&) { return {}; }
    virtual std::string path_sort_key(const TrackPtr& t) { return track_path(t); }
    virtual bool has_component(const std::string&, bool) { return false; }
    virtual void exit_app() {}
    virtual TrackInfo track_info(const TrackPtr&) { return {}; }
    virtual bool write_text_file(const std::string&, const std::string&) { return false; }

    virtual int playlist_item_count(int) { return 0; }
    virtual Tracks playlist_items(int, bool) { return {}; }
    virtual std::vector<bool> playlist_selection(int) { return {}; }
    virtual void set_playlist_selection(int, const std::vector<int>&, bool) {}
    virtual void clear_playlist_selection(int) {}
    virtual void remove_playlist_selection(int, bool) {}
    virtual void move_playlist_selection(int, int) {}
    virtual int playlist_focus(int) { return -1; }
    virtual void set_playlist_focus(int, int) {}
    virtual void ensure_visible(int, int) {}
    virtual int create_playlist(int, const std::string&) { return -1; }
    virtual int duplicate_playlist(int, const std::string&) { return -1; }
    virtual bool remove_playlist(int, bool) { return false; }
    virtual bool rename_playlist(int, const std::string&) { return false; }
    virtual bool move_playlist(int, int) { return false; }
    virtual bool playlist_is_auto(int) { return false; }
    virtual bool playlist_is_locked(int) { return false; }
    virtual int create_autoplaylist(int, const std::string&, const std::string&, const std::string&, int) { return -1; }
    virtual void show_autoplaylist_ui(int) {}
    virtual int playback_order() { return 0; }
    virtual void set_playback_order(int) {}
    virtual ItemLocation playing_location() { return {}; }
    virtual bool sort_playlist(int, const std::string&, bool) { return false; }
    virtual bool reorder_playlist(int, const std::vector<int>&) { return false; }
    virtual void undo_backup(int) {}
    virtual void queue_add(const TrackPtr&) {}
    virtual void queue_add_playlist_item(int, int) {}
    virtual void queue_flush() {}
    virtual Tracks queue_items() { return {}; }

    virtual int popup_menu(int, int, const Menu&) { return 0; }
    virtual MenuSourcePtr context_menu(ContextKind, const Tracks&) { return nullptr; }
    virtual MenuSourcePtr main_menu(const std::string&) { return nullptr; }
    virtual void set_cursor(int) {}
    virtual bool input_box(const std::string&, const std::string&, std::string&) { return false; }

    virtual bool image_pixels(const pui::gfx::Image&, pui::gfx::Pixels&) { return false; }
    virtual pui::gfx::ImagePtr image_from_pixels(const pui::gfx::Pixels&) { return nullptr; }
    virtual std::unique_ptr<pui::gfx::Canvas> pixels_canvas(pui::gfx::Pixels&, double) { return nullptr; }
    virtual bool save_png(const pui::gfx::Image&, const std::string&) { return false; }
    virtual pui::gfx::ImagePtr album_art_sync(const TrackPtr&, int, std::string&) { return nullptr; }
    virtual void load_image_async(const std::string& path, ArtDone done) { done(load_image(path), path); }

    virtual void http_request(const HttpRequest&, HttpDone done) { done({ 0, {}, "no network" }); }
    virtual HttpResponse http_request_sync(const HttpRequest&) { return { 0, {}, "no network" }; }
    virtual bool shell_run(const std::string&) { return false; }
    virtual std::string clipboard_text() { return {}; }
    virtual bool set_clipboard_text(const std::string&) { return false; }
    virtual int message_box(const std::string&, const std::string&, int) { return 1; }
    virtual bool may_write(const std::string&) { return true; }
    virtual bool panels_ui_available() { return false; }
    virtual bool panels_ui_variable(const std::string&, std::string&) { return false; }
    virtual void set_panels_ui_variable(const std::string&, const std::string&) {}
    virtual bool panels_ui_action(const std::string&) { return false; }
    virtual bool visible() { return true; }
    virtual int window_state() { return 0; }
    virtual void set_window_state(int) {}
    virtual bool fullscreen() { return false; }
    virtual void set_fullscreen(bool) {}
    virtual void set_window_limits(int, int, int, int) {}
    virtual void set_caption(const pui::gfx::Rect&) {}
    using TagSet = std::vector<std::pair<std::string, std::vector<std::string>>>;
    virtual bool update_tags(const Tracks&, const std::vector<TagSet>&) { return false; }
    enum class SelectionMode { Tracks, PlaylistSelection, Playlist, Released };
    virtual void ui_selection(SelectionMode, const Tracks&) {}
    virtual Tracks get_ui_selection() { return {}; }
    virtual int replaygain_mode() { return 0; }
    virtual void set_replaygain_mode(int) {}
    virtual std::vector<OutputDevice> output_devices() { return {}; }
    virtual bool set_output_device(const std::string&, const std::string&) { return false; }
    virtual std::vector<std::string> dsp_presets(int& selected) {
        selected = -1;
        return {};
    }
    virtual bool set_dsp_preset(int) { return false; }
    virtual bool library_search(const std::string&) { return false; }
    virtual unsigned playlist_lock_mask(int) { return 0; }
    virtual std::string playlist_lock_name(int) { return {}; }
    virtual bool set_playlist_lock(int, unsigned) { return false; }
    virtual int drag_tracks(const Tracks&, int) { return 0; }
    virtual bool copy_tracks(const Tracks&) { return false; }
    virtual bool clipboard_has_tracks() { return false; }
    virtual Tracks clipboard_tracks() { return {}; }
    virtual void tooltip(const std::string&, bool, int) {}
    virtual bool ui_colour(UiColour, uint32_t&) { return false; }
    virtual bool ui_font(int, UiFont&) { return false; }
    virtual bool sys_colour(int, uint32_t&) { return false; }
    virtual bool pick_colour(uint32_t&) { return false; }
    virtual void edit_properties(const std::string&) {}
    virtual std::string jsp3_path() { return {}; }
    virtual std::string bundled_import(const std::string&) { return {}; }
};

using Arg = std::variant<int, double, bool, std::string, TrackPtr, Tracks>;

enum class Mouse { Move, LeftDown, LeftUp, LeftDouble, RightDown, RightUp, Leave, MiddleDown, MiddleUp, MiddleDouble };

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
    void drag(Drag kind, int x, int y, unsigned mask, DropAction& action);
    bool key_down(int vk);
    void timer(int id);
    void playback_new_track(const TrackPtr& track = nullptr);
    enum class LibraryChange { Added, Removed, Changed };
    void library_changed(LibraryChange kind, Tracks tracks);
    void playback_stop(int reason);
    void playback_pause(bool paused);
    void playback_time(double t);
    void playback_starting(int cmd, bool paused);
    bool invoke(const char* name, const std::vector<Arg>& args = {});
    bool in_modal() const;
    const std::vector<std::string>& source_files() const;
    void notify_data(const std::string& name, const std::string& json, const std::vector<JsHost::NotifyItem>& items = {});

    struct Impl;

private:
    std::unique_ptr<Impl> m;
};

std::string script_dir_of(const std::string& path);

std::string check_syntax(const std::string& source, const std::string& name);
std::string check_properties(const std::string& json);

struct TextPos {
    int line = 1, col = 1;
};
TextPos text_pos(const std::string& utf8, std::size_t byteOffset);
std::string editor_status(const TextPos& pos, const std::string& syntaxError, const std::string& applied);
int error_line(const std::string& message, const std::string& file);
std::size_t line_start(const std::string& utf8, int line);

}
