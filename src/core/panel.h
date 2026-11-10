#pragma once
#include "../fb2k.h"
#include "js_engine.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace jsp {

struct EditorHooks {
    std::function<std::string(const std::string&)> apply;
    std::function<std::string(const std::string&)> check;
    int line = 0;
};

class Surface {
public:
    virtual ~Surface() = default;
    virtual void invalidate() = 0;
    virtual void invalidate_rect(const pui::gfx::Rect& r) = 0;
    virtual void start_timer(int id, int ms) = 0;
    virtual void stop_timer(int id) = 0;
    virtual std::string choose_script_file() = 0;
    virtual int popup_menu(int x, int y, const Menu& menu) = 0;
    virtual void set_cursor(int idc) = 0;
    virtual bool input_box(const std::string& prompt, const std::string& caption, std::string& value) = 0;
    virtual int message_box(const std::string& text, const std::string& caption, int type) = 0;
    virtual bool visible() = 0;
    virtual int window_state() = 0;
    virtual void set_window_state(int state) = 0;
    virtual bool fullscreen() = 0;
    virtual void set_fullscreen(bool on) = 0;
    virtual bool always_on_top() = 0;
    virtual int drag_tracks(metadb_handle_list_cref tracks, int effects) = 0;
    virtual void set_tooltip(const std::string& text, bool active, int maxWidth) = 0;
    virtual bool ui_colour(UiColour kind, uint32_t& argb) = 0;
    virtual bool ui_font(int kind, UiFont& font) = 0;
    virtual bool sys_colour(int index, uint32_t& argb) = 0;
    virtual bool pick_colour(uint32_t& argb) = 0;
    virtual void set_always_on_top(bool on) = 0;
    virtual void set_window_limits(int minW, int minH, int maxW, int maxH) = 0;
    virtual void set_caption(const pui::gfx::Rect& r) = 0;
    virtual void open_editor(const std::string& title, const std::string& text, EditorHooks hooks) = 0;
    virtual bool key_pressed(int vk) = 0;
    virtual void* native() = 0;
};

class Panel : public JsHost, private play_callback_impl_base, private library_callback_dynamic_impl_base {
public:
    Panel(Surface& surface, std::string scriptPath, std::function<void(const std::string&)> onScriptChanged);
    ~Panel() override;

    const std::string& script_path() const { return m_path; }
    std::string status() const;
    PanelInfo info() const;
    void set_script(const std::string& path);
    void reload();
    void edit_script() override;
    void edit_properties(const std::string& json) override;
    std::string jsp3_path() override;
    std::string bundled_import(const std::string& name) override;
    void missing_api(const std::string& name) override;
    std::string apply_source(const std::string& source);

    void paint(pui::gfx::Canvas& canvas);
    void resize(int w, int h);
    void mouse(Mouse kind, int x, int y, unsigned mask);
    void wheel(int step, bool horizontal = false);
    bool key_down(int vk);
    bool key_up(int vk);
    void char_input(unsigned codepoint);
    void focus(bool focused);
    void timer(int id);
    void drag(Drag kind, int x, int y, unsigned mask, DropAction& action);
    process_locations_notify::ptr drop_notify(const DropAction& action);
    void drop_locations(const std::vector<std::string>& paths, const DropAction& action);

    static std::vector<Panel*>& live();
    static void broadcast(const char* name, const std::vector<Arg>& args = {});
    static void skin_message(const std::string& name, const std::string& text);
    void post(const char* name, std::vector<Arg> args = {});

    void repaint() override;
    void repaint_rect(const pui::gfx::Rect& r) override;
    void set_timer(int id, int ms) override;
    void kill_timer(int id) override;
    std::string titleformat(const std::string& expr) override;
    bool is_playing() override;
    bool is_paused() override;
    double playback_time() override;
    double playback_length() override;
    void playback(Playback cmd) override;
    void seek(double t) override;
    void console(const std::string& text) override;
    bool read_text_file(const std::string& path, std::string& out) override;
    LibraryState navidrome_state() override;
    void navidrome_albums(AlbumsDone done) override;
    void navidrome_play(bool artist, const std::string& id, bool replace, bool play) override;
    void navidrome_artist_info(const std::string& name, ArtistInfoDone done) override;
    void navidrome_search(const std::string& query, SearchDone done) override;
    TrackPtr now_playing() override;
    Tracks library_items() override;
    std::string track_path(const TrackPtr& t) override;
    bool same_track(const TrackPtr& a, const TrackPtr& b) override;
    std::vector<std::string> format_tracks(const std::string& expr, const Tracks& tracks) override;
    void album_art(const TrackPtr& t, int artId, ArtDone done) override;
    pui::gfx::ImagePtr load_image(const std::string& path) override;
    int playlist_count() override;
    int active_playlist() override;
    void set_active_playlist(int p) override;
    int playing_playlist() override;
    std::string playlist_name(int p) override;
    int find_or_create_playlist(const std::string& name, bool unlocked) override;
    void clear_playlist(int p) override;
    void insert_playlist_items(int p, int base, const Tracks& tracks, bool select) override;
    void add_locations(int p, const std::vector<std::string>& paths, bool select) override;
    void execute_default_action(int p, int item) override;
    bool key_pressed(int vk) override;

    std::string load_properties() override;
    void save_properties(const std::string& json) override;
    void notify_others(const std::string& name, const std::string& json, const std::vector<NotifyItem>& items) override;
    void reload_script() override;
    std::string profile_path() override;
    std::string component_path() override;
    std::string foobar_path() override;
    std::string version() override;
    double volume() override;
    void set_volume(double db) override;
    bool flag(Flag f) override;
    void set_flag(Flag f, bool on) override;
    bool run_main_menu_command(const std::string& path) override;
    bool main_menu_command_checked(const std::string& path) override;
    bool run_context_command(const std::string& path, const Tracks* tracks) override;
    void popup_message(const std::string& text, const std::string& title) override;
    TrackPtr focus_item() override;
    Tracks query_items(const Tracks& tracks, const std::string& query, std::string& error) override;
    bool in_library(const TrackPtr& t) override;
    std::string library_relative_path(const TrackPtr& t) override;
    std::string path_sort_key(const TrackPtr& t) override;
    bool has_component(const std::string& name, bool dll) override;
    void exit_app() override;
    TrackInfo track_info(const TrackPtr& t) override;
    bool write_text_file(const std::string& path, const std::string& text) override;

    int playlist_item_count(int p) override;
    Tracks playlist_items(int p, bool selectedOnly) override;
    std::vector<bool> playlist_selection(int p) override;
    void set_playlist_selection(int p, const std::vector<int>& items, bool state) override;
    void clear_playlist_selection(int p) override;
    void remove_playlist_selection(int p, bool crop) override;
    void move_playlist_selection(int p, int delta) override;
    int playlist_focus(int p) override;
    void set_playlist_focus(int p, int item) override;
    void ensure_visible(int p, int item) override;
    int create_playlist(int p, const std::string& name) override;
    int duplicate_playlist(int p, const std::string& name) override;
    bool remove_playlist(int p, bool switchActive) override;
    bool rename_playlist(int p, const std::string& name) override;
    bool move_playlist(int from, int to) override;
    bool playlist_is_auto(int p) override;
    bool playlist_is_locked(int p) override;
    int create_autoplaylist(int p, const std::string& name, const std::string& query, const std::string& sort, int flags) override;
    void show_autoplaylist_ui(int p) override;
    int playback_order() override;
    void set_playback_order(int order) override;
    ItemLocation playing_location() override;
    bool sort_playlist(int p, const std::string& pattern, bool selOnly) override;
    bool reorder_playlist(int p, const std::vector<int>& order) override;
    void undo_backup(int p) override;
    void queue_add(const TrackPtr& t) override;
    void queue_add_playlist_item(int p, int item) override;
    void queue_flush() override;
    void queue_remove(const std::vector<int>& indices) override;
    std::vector<QueueItem> queue_items() override;
    int popup_menu(int x, int y, const Menu& menu) override;
    MenuSourcePtr context_menu(ContextKind kind, const Tracks& tracks) override;
    MenuSourcePtr main_menu(const std::string& root) override;
    void set_cursor(int idc) override;
    bool input_box(const std::string& prompt, const std::string& caption, std::string& value) override;
    bool image_pixels(const pui::gfx::Image& img, pui::gfx::Pixels& out) override;
    pui::gfx::ImagePtr image_from_pixels(const pui::gfx::Pixels& px) override;
    std::unique_ptr<pui::gfx::Canvas> pixels_canvas(pui::gfx::Pixels& px, double scale) override;
    bool save_png(const pui::gfx::Image& img, const std::string& path) override;
    pui::gfx::ImagePtr album_art_sync(const TrackPtr& t, int artId, std::string& path) override;
    void load_image_async(const std::string& path, ArtDone done) override;
    void http_request(const HttpRequest& req, HttpDone done) override;
    HttpResponse http_request_sync(const HttpRequest& req) override;
    bool shell_run(const std::string& command) override;
    std::string clipboard_text() override;
    bool set_clipboard_text(const std::string& text) override;
    int message_box(const std::string& text, const std::string& caption, int type) override;
    void ui_selection(SelectionMode mode, const Tracks& tracks) override;
    bool update_tags(const Tracks& tracks, const std::vector<TagSet>& sets) override;
    bool panels_ui_available() override;
    bool panels_ui_variable(const std::string& name, std::string& out) override;
    void set_panels_ui_variable(const std::string& name, const std::string& value) override;
    bool panels_ui_action(const std::string& action) override;
    std::string panels_ui_panel_name() override;
    bool panels_ui_eval(const std::string& expr, std::string& out) override;
    bool panels_ui_value(const std::string& name, std::string& out) override;
    void set_panels_ui_value(const std::string& name, const std::string& value) override;
    bool panels_ui_panel_rect(const std::string& name, pui::gfx::Rect& out) override;
    bool panels_ui_transparent() override;
    bool paint_backdrop(pui::gfx::Canvas& canvas) override;
    int window_state() override { return m_surface.window_state(); }
    void set_window_state(int state) override { m_surface.set_window_state(state); }
    bool fullscreen() override { return m_surface.fullscreen(); }
    bool visible() override { return m_surface.visible(); }
    void set_fullscreen(bool on) override { m_surface.set_fullscreen(on); }
    void set_window_limits(int minW, int minH, int maxW, int maxH) override { m_surface.set_window_limits(minW, minH, maxW, maxH); }
    void set_caption(const pui::gfx::Rect& r) override { m_surface.set_caption(r); }
    Tracks get_ui_selection() override;
    int replaygain_mode() override;
    void set_replaygain_mode(int mode) override;
    std::vector<OutputDevice> output_devices() override;
    bool set_output_device(const std::string& output, const std::string& device) override;
    std::vector<std::string> dsp_presets(int& selected) override;
    bool set_dsp_preset(int index) override;
    bool library_search(const std::string& query) override;
    unsigned playlist_lock_mask(int p) override;
    std::string playlist_lock_name(int p) override;
    bool set_playlist_lock(int p, unsigned mask) override;
    int drag_tracks(const Tracks& tracks, int effects) override;
    bool copy_tracks(const Tracks& tracks) override;
    bool clipboard_has_tracks() override;
    Tracks clipboard_tracks() override;
    void tooltip(const std::string& text, bool active, int maxWidth) override { m_surface.set_tooltip(text, active, maxWidth); }
    bool ui_colour(UiColour kind, uint32_t& argb) override;
    bool ui_font(int kind, UiFont& font) override;
    bool sys_colour(int index, uint32_t& argb) override { return m_surface.sys_colour(index, argb); }
    bool pick_colour(uint32_t& argb) override { return m_surface.pick_colour(argb); }

private:
    struct Callbacks;
    void later(std::function<void()> fn);
    void invoke(const char* name, const std::vector<Arg>& args = {});
    bool valid(int p);
    void context_menu(int x, int y);
    std::string current_source();
    titleformat_object::ptr compiled(const std::string& expr);
    void on_now_playing_art(album_art_data::ptr data);

    void on_playback_starting(play_control::t_track_command cmd, bool paused) override;
    void on_playback_new_track(metadb_handle_ptr track) override;
    void on_playback_stop(play_control::t_stop_reason reason) override;
    void on_playback_seek(double t) override;
    void on_playback_pause(bool paused) override;
    void on_playback_edited(metadb_handle_ptr track) override;
    void on_playback_dynamic_info(const file_info&) override;
    void on_playback_dynamic_info_track(const file_info&) override;
    void on_playback_time(double t) override;
    void on_volume_change(float db) override;
    void on_items_added(metadb_handle_list_cref items) override;
    void on_items_removed(metadb_handle_list_cref items) override;
    void on_items_modified(metadb_handle_list_cref items) override;
    void library_event(JsEngine::LibraryChange kind, metadb_handle_list_cref items);
    void load_navidrome_library();

    Surface& m_surface;
    std::string m_path;
    std::function<void(const std::string&)> m_onScriptChanged;
    std::unique_ptr<JsEngine> m_engine;
    std::map<std::string, titleformat_object::ptr> m_tf;
    int m_w = 0, m_h = 0;
    std::shared_ptr<abort_callback_impl> m_abort = std::make_shared<abort_callback_impl>();
    now_playing_album_art_notify* m_artNotify = nullptr;
    struct ArtWait {
        TrackPtr track;
        int artId;
        std::shared_ptr<ArtDone> done;
    };
    std::vector<ArtWait> m_artWaiting;
    std::unique_ptr<Callbacks> m_callbacks;
    ui_selection_holder::ptr m_selection;
    std::vector<std::pair<std::string, long long>> m_watched;
    std::string m_puiName;
    void watch_files();
    bool files_changed();
};

}
