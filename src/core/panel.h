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
};

class Surface {
public:
    virtual ~Surface() = default;
    virtual void invalidate() = 0;
    virtual void invalidate_rect(const pui::gfx::Rect& r) = 0;
    virtual void start_timer(int id, int ms) = 0;
    virtual void stop_timer(int id) = 0;
    virtual std::string choose_script_file() = 0;
    virtual int popup_menu(int x, int y, const std::vector<std::string>& items) = 0;
    virtual void open_editor(const std::string& title, const std::string& text, EditorHooks hooks) = 0;
    virtual bool key_pressed(int vk) = 0;
};

class Panel : public JsHost, private play_callback_impl_base, private library_callback_dynamic_impl_base {
public:
    Panel(Surface& surface, std::string scriptPath, std::function<void(const std::string&)> onScriptChanged);
    ~Panel() override;

    const std::string& script_path() const { return m_path; }
    std::string status() const;
    void set_script(const std::string& path);
    void reload();
    void edit_script();
    std::string apply_source(const std::string& source);

    void paint(pui::gfx::Canvas& canvas);
    void resize(int w, int h);
    void mouse(Mouse kind, int x, int y, unsigned mask);
    void wheel(int step);
    bool key_down(int vk);
    void timer(int id);

    static std::vector<Panel*>& live();

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
    void console(const std::string& text) override;
    bool read_text_file(const std::string& path, std::string& out) override;
    LibraryState navidrome_state() override;
    void navidrome_albums(AlbumsDone done) override;
    void navidrome_play(bool artist, const std::string& id, bool replace, bool play) override;
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
    void execute_default_action(int p, int item) override;
    bool key_pressed(int vk) override;

private:
    void context_menu(int x, int y);
    std::string current_source();
    titleformat_object::ptr compiled(const std::string& expr);
    void on_now_playing_art(album_art_data::ptr data);

    void on_playback_starting(play_control::t_track_command cmd, bool paused) override;
    void on_playback_new_track(metadb_handle_ptr track) override;
    void on_playback_stop(play_control::t_stop_reason reason) override;
    void on_playback_seek(double) override {}
    void on_playback_pause(bool paused) override;
    void on_playback_edited(metadb_handle_ptr) override {}
    void on_playback_dynamic_info(const file_info&) override {}
    void on_playback_dynamic_info_track(const file_info&) override {}
    void on_playback_time(double t) override;
    void on_volume_change(float) override {}
    void on_items_added(metadb_handle_list_cref items) override;
    void on_items_removed(metadb_handle_list_cref items) override;
    void on_items_modified(metadb_handle_list_cref items) override;
    void library_event(JsEngine::LibraryChange kind, metadb_handle_list_cref items);

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
};

}
