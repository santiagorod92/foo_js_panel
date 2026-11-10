#include "test.h"
#include "fake_host.h"
#include "recording_canvas.h"
#include "core/js_api_util.h"
#include "core/sample_script.h"
#include "core/theme_patches.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace jsp;

namespace {

bool has_op(const t::RecordingCanvas& c, const std::string& op) {
    return std::find(c.ops.begin(), c.ops.end(), op) != c.ops.end();
}

bool has_prefix(const t::RecordingCanvas& c, const std::string& prefix) {
    return std::any_of(c.ops.begin(), c.ops.end(), [&](const std::string& o) { return o.rfind(prefix, 0) == 0; });
}

}

TEST("api util: ARGB colours split into colour and alpha") {
    CHECK(argb_color(0xff102030u) == pui::gfx::Color(0x10, 0x20, 0x30));
    CHECK_EQ(argb_alpha(0x80000000u), 128);
}

TEST("api util: DrawText flags map to canvas flags") {
    using namespace pui::gfx;
    CHECK_EQ(dt_to_text_flags(0x1 | 0x4 | 0x20 | 0x8000), (unsigned)(kAlignCenter | kVCenter | kSingleLine | kEndEllipsis));
    CHECK_EQ(dt_to_text_flags(0x2 | 0x10), (unsigned)(kAlignRight | kWordWrap));
}

TEST("api util: StringFormat packs alignment, line alignment and trimming") {
    using namespace pui::gfx;
    CHECK_EQ(string_format_to_text_flags((1u << 28) | (1u << 24) | (3u << 20) | 0x1000),
             (unsigned)(kAlignCenter | kVCenter | kEndEllipsis | kSingleLine));
    CHECK_EQ(string_format_to_text_flags(0), (unsigned)kWordWrap);
}

TEST("engine: on_paint draws through gr") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("function on_paint(gr) { gr.FillSolidRect(1, 2, 30, 40, 0xff112233); }", "a.js"));
    e.resize(100, 50);
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_op(c, "fill_rect 1,2,30,40 #112233"));
    CHECK(!e.failed());
}

TEST("engine: translucent fills use alpha, fully transparent ones draw nothing") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("function on_paint(gr) { gr.FillSolidRect(0, 0, 5, 5, 0x80ff0000); gr.FillSolidRect(0, 0, 9, 9, 0x00ffffff); }", "");
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_op(c, "fill_rect_alpha 0,0,5,5 #ff0000 a128"));
    CHECK(!has_prefix(c, "fill_rect 0,0,9,9"));
}

TEST("engine: GdiDrawText sets the font and maps flags") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("const f = gdi.Font('Arial', 14, 1);"
           "function on_paint(gr) { gr.GdiDrawText('hi', f, 0xffffffff, 0, 0, 50, 20, 0x25); }", "");
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_op(c, "font Arial 14 bold"));
    CHECK(has_prefix(c, "text \"hi\" 0,0,50,20 f" + std::to_string(pui::gfx::kAlignCenter | pui::gfx::kVCenter | pui::gfx::kSingleLine)));
}

TEST("engine: GdiDrawText with DT_CALCRECT grows the box to the text, bottom or centre aligned like SMP") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("const f = gdi.Font('Arial', 14);"
           "function on_paint(gr) { gr.GdiDrawText('a', f, 0xffffffff, 0, 30, 50, 4, 0x400 | 0x8);"
           " gr.GdiDrawText('b', f, 0xffffffff, 0, 0, 50, 40, 0x400 | 0x4 | 0x20);"
           " gr.GdiDrawText('c', f, 0xffffffff, 0, 0, 50, 40, 0x8 | 0x20); }", "");
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_prefix(c, "text \"a\" 0,22,50,12 f0"));
    CHECK(has_prefix(c, "text \"b\" 0,14,50,12 f" + std::to_string(pui::gfx::kSingleLine)));
    CHECK(has_prefix(c, "text \"c\" 0,28,50,12 f" + std::to_string(pui::gfx::kSingleLine)));
}

TEST("engine: DrawString centres or bottom-aligns wrapped text itself, as GDI+ does and the canvases don't") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("const f = gdi.Font('Arial', 14);"
           "function on_paint(gr) { gr.DrawString('a', f, 0xffffffff, 0, 0, 50, 40, 0x11000000);"
           " gr.DrawString('b', f, 0xffffffff, 0, 0, 50, 40, 0x02000000);"
           " gr.DrawString('c', f, 0xffffffff, 0, 0, 50, 40, 0x01001000); }", "");
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_prefix(c, "text \"a\" 0,14,50,12 f" + std::to_string(pui::gfx::kAlignCenter | pui::gfx::kWordWrap)));
    CHECK(has_prefix(c, "text \"b\" 0,28,50,12 f" + std::to_string(pui::gfx::kWordWrap)));
    CHECK(has_prefix(c, "text \"c\" 0,0,50,40 f" + std::to_string(pui::gfx::kVCenter | pui::gfx::kSingleLine)));
}

TEST("api: library relative paths use Windows separators, as SMP scripts split them on backslashes") {
#ifdef _WIN32
    CHECK_EQ(jsp::windows_separators("A\\B\\c.flac"), std::string("A\\B\\c.flac"));
#else
    CHECK_EQ(jsp::windows_separators("A/B/c.flac"), std::string("A\\B\\c.flac"));
#endif
}

TEST("handles: OrderByPath sorts by the host's path key, so server tracks group by artist and album") {
    t::FakeHost host;
    host.libraryTracks = { t::track("navidrome://3"), t::track("navidrome://1"), t::track("/music/a.flac"), t::track("navidrome://2") };
    host.sortKeys["navidrome://1"] = "navidrome://B\\X\\01";
    host.sortKeys["navidrome://2"] = "navidrome://A\\Y\\01";
    host.sortKeys["navidrome://3"] = "navidrome://B\\X\\02";
    JsEngine e(host);
    e.load("const l = fb.GetLibraryItems(); l.OrderByPath(); console.log(l.Convert().map(h => h.Path).join(' '));", "");
    CHECK_EQ(host.console_lines.back(), std::string("/music/a.flac navidrome://2 navidrome://1 navidrome://3"));
}

TEST("engine: gdi.Font exposes Name, Size and Style") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("const f = gdi.Font('Tahoma', 11, 3); console.log(f.Name, f.Size, f.Style);", "");
    CHECK_EQ(host.console_lines.size(), (size_t)1);
    CHECK_EQ(host.console_lines[0], std::string("Tahoma 11 3"));
}

TEST("engine: CalcTextWidth measures with the canvas") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("let w = 0; const f = gdi.Font('A', 10, 0);"
           "function on_paint(gr) { w = gr.CalcTextWidth('abcd', f); console.log(w); }", "");
    t::RecordingCanvas c;
    e.paint(c);
    CHECK_EQ(host.console_lines.back(), std::string("28"));
}

TEST("engine: gr outside on_paint is a script error") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("let keep; function on_paint(gr) { keep = gr; } function on_mouse_move() { keep.FillSolidRect(0,0,1,1,0); }", "");
    t::RecordingCanvas c;
    e.paint(c);
    e.mouse(Mouse::Move, 1, 1, 0);
    CHECK(e.failed());
    CHECK(e.error().find("on_paint") != std::string::npos);
}

TEST("engine: syntax errors fail the load and are painted") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(!e.load("function (", "broken.js"));
    CHECK(e.failed());
    CHECK(e.error().find("SyntaxError") != std::string::npos);
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_prefix(c, "text \"JS Panel: script error"));
    CHECK(!host.console_lines.empty());
}

TEST("engine: a runaway script is interrupted") {
    t::FakeHost host;
    JsEngine e(host, 200);
    CHECK(!e.load("for (;;) {}", ""));
    CHECK(e.error().find("longer than") != std::string::npos);
}

TEST("engine: window size and on_size") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("function on_size(w, h) { console.log(w + 'x' + h, window.Width, window.Height); }", "");
    e.resize(320, 200);
    CHECK_EQ(host.console_lines.back(), std::string("320x200 320 200"));
}

TEST("engine: timers fire, timeouts fire once, intervals until cleared") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("let n = 0, m = 0; const iv = window.SetInterval(() => { if (++n == 2) window.ClearInterval(iv); }, 100);"
           "window.SetTimeout(() => { ++m; console.log('once'); }, 50);"
           "function on_mouse_move() { console.log(n, m); }", "");
    CHECK_EQ(host.timers.size(), (size_t)2);
    CHECK_EQ(host.timers[1], 100);
    e.timer(2);
    CHECK_EQ(host.timers.count(2), (size_t)0);
    e.timer(1);
    e.timer(1);
    CHECK(host.timers.empty());
    e.timer(1);
    e.mouse(Mouse::Move, 0, 0, 0);
    CHECK_EQ(host.console_lines.back(), std::string("2 1"));
}

TEST("engine: errors stop the script's timers") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("window.SetInterval(() => { throw new Error('boom'); }, 100);", "");
    e.timer(1);
    CHECK(e.failed());
    CHECK(host.timers.empty());
    CHECK(e.error().find("boom") != std::string::npos);
}

TEST("engine: fb reads playback state and titleformat through the host") {
    t::FakeHost host;
    host.playing = true;
    host.time = 12.5;
    host.length = 200;
    host.fields["%title%"] = "Song";
    JsEngine e(host);
    e.load("console.log(fb.IsPlaying, fb.IsPaused, fb.PlaybackTime, fb.PlaybackLength, fb.TitleFormat('%title%').Eval());"
           "fb.PlayOrPause(); fb.Next();", "");
    CHECK_EQ(host.console_lines.back(), std::string("true false 12.5 200 Song"));
    CHECK_EQ(host.commands.size(), (size_t)2);
    CHECK(host.commands[0] == Playback::PlayOrPause);
    CHECK(host.commands[1] == Playback::Next);
}

TEST("engine: playback callbacks reach the script") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("function on_playback_new_track(h) { console.log('new', h); }"
           "function on_playback_stop(r) { console.log('stop', r); }"
           "function on_playback_pause(p) { console.log('pause', p); }"
           "function on_playback_time(t) { console.log('time', t); }"
           "function on_playback_starting(c, p) { console.log('start', c, p); }", "");
    e.playback_starting(0, false);
    e.playback_new_track();
    e.playback_time(3);
    e.playback_pause(true);
    e.playback_stop(1);
    const std::vector<std::string> want = { "start 0 false", "new null", "time 3", "pause true", "stop 1" };
    CHECK(host.console_lines == want);
}

TEST("engine: mouse callbacks get x, y, mask and report the return value") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("function on_mouse_lbtn_down(x, y, m) { console.log(x, y, m); }"
           "function on_mouse_rbtn_up() { return true; }", "");
    CHECK(!e.mouse(Mouse::LeftDown, 5, 6, 1));
    CHECK_EQ(host.console_lines.back(), std::string("5 6 1"));
    CHECK(e.mouse(Mouse::RightUp, 0, 0, 0));
    CHECK(!e.mouse(Mouse::LeftDouble, 0, 0, 0));
}

TEST("engine: include resolves relative to the script folder") {
    t::FakeHost host;
    host.files["/skins/lib/helpers.js"] = "function twice(x) { return 2 * x; }";
    JsEngine e(host);
    CHECK(e.load("include('lib/helpers.js'); console.log(twice(21));", "/skins/main.js"));
    CHECK_EQ(host.console_lines.back(), std::string("42"));
    JsEngine missing(host);
    CHECK(!missing.load("include('nope.js');", "/skins/main.js"));
    CHECK(missing.error().find("nope.js") != std::string::npos);
}

TEST("engine: promise jobs run after a callback") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("Promise.resolve(7).then(v => console.log('then', v));", "");
    CHECK_EQ(host.console_lines.back(), std::string("then 7"));
}

TEST("engine: on_script_unload runs when the engine goes away") {
    t::FakeHost host;
    {
        JsEngine e(host);
        e.load("function on_script_unload() { console.log('bye'); }", "");
    }
    CHECK_EQ(host.console_lines.back(), std::string("bye"));
}

TEST("engine: gradients, lines, ellipses and round rects reach the canvas") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("function on_paint(gr) {"
           "  gr.FillGradRect(0, 0, 10, 20, 90, 0xff000000, 0xffffffff);"
           "  gr.FillGradRect(0, 0, 3, 2, 0, 0xff000000, 0xffffffff);"
           "  gr.DrawLine(0, 0, 10, 10, 1, 0xffffffff); gr.DrawLine(0, 0, 10, 0, 4, 0xffffffff);"
           "  gr.FillEllipse(0, 0, 10, 8, 0xff00ff00); gr.FillRoundRect(0, 0, 10, 10, 2, 3, 0xff0000ff);"
           "  gr.DrawRect(0, 0, 9, 9, 1, 0xffff0000); gr.SetSmoothingMode(4); }", "");
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_op(c, "gradient 0,0,10,20 #000000>#ffffff"));
    CHECK(has_op(c, "fill_rect 0,0,1,2 #000000"));
    CHECK(has_op(c, "fill_rect 2,0,1,2 #ffffff"));
    CHECK(std::count(c.ops.begin(), c.ops.end(), std::string("polygon")) >= 2);
    CHECK(has_op(c, "round_rect 0,0,10,8 10,8 #00ff00"));
    CHECK(has_op(c, "round_rect 0,0,10,10 4,6 #0000ff"));
    CHECK(has_op(c, "frame_rect 0,0,10,10 #ff0000"));
    CHECK(!e.failed());
}

TEST("engine: the built-in sample script runs idle and playing") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load(sample_script(), ""));
    e.resize(400, 200);
    t::RecordingCanvas idle;
    e.paint(idle);
    CHECK(has_prefix(idle, "text \"Nothing playing"));
    host.playing = true;
    host.length = 100;
    host.time = 50;
    host.fields["%title%"] = "Track";
    host.fields["[%artist%][ - %album%]"] = "Artist - Album";
    t::RecordingCanvas playing;
    e.paint(playing);
    CHECK(has_prefix(playing, "text \"Track\""));
    CHECK(has_prefix(playing, "text \"0:50 / 1:40\""));
    e.mouse(Mouse::Move, 1, 1, 0);
    e.mouse(Mouse::LeftUp, 1, 1, 0);
    CHECK(!host.commands.empty() && host.commands.back() == Playback::PlayOrPause);
    CHECK(!e.failed());
}

TEST("engine: script_dir_of keeps the trailing separator") {
    CHECK_EQ(script_dir_of("/a/b/c.js"), std::string("/a/b/"));
    CHECK_EQ(script_dir_of("C:\\x\\y.js"), std::string("C:\\x\\"));
    CHECK_EQ(script_dir_of("y.js"), std::string());
}

namespace {

std::string template_js(const std::string& source, const std::string& tag) {
    const std::string open = "R\"" + tag + "(", close = ")" + tag + "\"";
    const auto a = source.find(open);
    if (a == std::string::npos) return {};
    const auto b = source.find(close, a);
    return b == std::string::npos ? std::string() : source.substr(a + open.size(), b - a - open.size());
}

}

std::string panels_ui_templates() {
    std::ifstream in(std::string(PANELS_UI_SRC_DIR) + "/core/skin_templates.cpp", std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), {});
}

void load_template_js(t::FakeHost& host) {
    const std::string src = panels_ui_templates();
    host.files["js/now_playing.js"] = template_js(src, "JSNOW");
    host.files["js/lib/palette.js"] = template_js(src, "JSPAL");
    host.files["js/library_tree.js"] = template_js(src, "JSLIB");
    host.files["js/navidrome_artists.js"] = template_js(src, "JSNDA");
    host.files["js/cover_overlay.js"] = template_js(src, "JSCOV");
}

TEST("Panels UI templates: cover_overlay.js shades the cover and toggles cover.full on click") {
    t::FakeHost host;
    load_template_js(host);
    CHECK(!host.files["js/cover_overlay.js"].empty());
    host.puiTransparent = true;
    JsEngine e(host);
    CHECK(e.load(host.files["js/cover_overlay.js"], "js/cover_overlay.js"));
    e.resize(550, 84);
    t::RecordingCanvas stopped;
    e.paint(stopped);
    CHECK(has_prefix(stopped, "text \"Nothing playing\""));
    host.playing = true;
    host.length = 240;
    host.time = 61;
    host.fields["%title%"] = "Harbor Lights";
    host.fields["[%artist%][  \u00b7  %album%]"] = "The Glass Pilots  \u00b7  Neon Harbor";
    t::RecordingCanvas playing;
    e.paint(playing);
    CHECK(has_prefix(playing, "text \"Harbor Lights\""));
    CHECK(has_prefix(playing, "text \"1:01 / 4:00\""));
    e.mouse(Mouse::LeftUp, 10, 10, 0);
    CHECK_EQ(host.puiValues["cover.full"], std::string("1"));
    e.mouse(Mouse::LeftUp, 10, 10, 0);
    CHECK_EQ(host.puiValues["cover.full"], std::string(""));
    CHECK(!e.failed());
}

TEST("Panels UI templates: now_playing.js runs stopped and playing, with the album cover") {
    t::FakeHost host;
    load_template_js(host);
    for (const char* f : { "js/now_playing.js", "js/lib/palette.js", "js/library_tree.js", "js/navidrome_artists.js" })
        CHECK(!host.files[f].empty());
    JsEngine e(host);
    CHECK(e.load(host.files["js/now_playing.js"], "js/now_playing.js"));
    e.resize(900, 56);
    t::RecordingCanvas stopped;
    e.paint(stopped);
    CHECK(has_prefix(stopped, "text \"Stopped\""));
    CHECK(!has_op(stopped, "image"));
    host.playing = true;
    host.length = 3725;
    host.time = 61;
    host.fields["%title%"] = "Song";
    host.fields["[%artist%][  ·  %album%][ (%date%)]"] = "Artist  ·  Album";
    host.fields["%codec%[  ·  %bitrate% kbps][  ·  %samplerate% Hz]"] = "FLAC";
    auto song = t::track("/m/song.flac");
    host.playingTrack = song;
    host.art["/m/song.flac"] = std::make_shared<t::FakeImage>(600, 600);
    e.playback_new_track(song);
    t::RecordingCanvas playing;
    e.paint(playing);
    CHECK(has_op(playing, "image"));
    CHECK(has_prefix(playing, "text \"Song\" 62,"));
    CHECK(has_prefix(playing, "text \"Playing  ·  FLAC\""));
    CHECK(has_prefix(playing, "text \"1:01\""));
    CHECK(has_prefix(playing, "text \"1:02:05\""));
    auto other = t::track("/m/none.flac");
    host.playingTrack = other;
    e.playback_new_track(other);
    t::RecordingCanvas noArt;
    e.paint(noArt);
    CHECK(!has_op(noArt, "image"));
    CHECK(has_prefix(noArt, "text \"\u266a\""));
    e.mouse(Mouse::LeftUp, 5, 5, 0);
    e.wheel(-1);
    CHECK(host.commands.size() == 2 && host.commands[0] == Playback::PlayOrPause && host.commands[1] == Playback::Next);
    CHECK(!e.failed());
}

TEST("Panels UI templates: library_tree.js groups the library and plays an album") {
    t::FakeHost host;
    load_template_js(host);
    const std::string group = "$if2(%album artist%,Unknown artist)^^$if2(%album%,Unknown album)^^%date%";
    const std::string sort = "%album artist%|%date%|%album%|$num(%discnumber%,2)|$num(%tracknumber%,3)|%title%";
    host.libraryTracks = { t::track("/b2", { { group, "Beta^^Two^^2020" }, { sort, "Beta|2020|Two|01|002|y" } }),
                           t::track("/b1", { { group, "Beta^^Two^^2020" }, { sort, "Beta|2020|Two|01|001|x" } }),
                           t::track("/a1", { { group, "alpha^^One^^2001" }, { sort, "alpha|2001|One|01|001|z" } }) };
    JsEngine e(host);
    CHECK(e.load(host.files["js/library_tree.js"], "js/library_tree.js"));
    e.resize(280, 300);
    t::RecordingCanvas loading;
    e.paint(loading);
    CHECK(has_prefix(loading, "text \"Loading the library"));
    CHECK_EQ(host.timers.size(), (size_t)1);
    e.timer(host.timers.begin()->first);
    t::RecordingCanvas tree;
    e.paint(tree);
    CHECK(has_prefix(tree, "text \"2 artists\""));
    CHECK(has_prefix(tree, "text \"alpha\" 24,30,"));
    CHECK(has_prefix(tree, "text \"Beta\" 24,54,"));
    e.mouse(Mouse::LeftDown, 40, 60, 1);
    t::RecordingCanvas open;
    e.paint(open);
    CHECK(has_prefix(open, "text \"Two  (2020)\" 36,78,"));
    e.mouse(Mouse::LeftDouble, 40, 82, 1);
    const int p = 1;
    CHECK_EQ(host.playlists.size(), (size_t)2);
    CHECK_EQ(host.playlists[1], std::string("Library selection"));
    CHECK_EQ(host.playlistItems[p].size(), (size_t)2);
    CHECK_EQ(host.track_path(host.playlistItems[p][0]), std::string("/b1"));
    CHECK(host.actions.back() == "play 1 0");
    e.mouse(Mouse::LeftDouble, 40, 34, 1 | 0x8);
    CHECK_EQ(host.playlistItems[p].size(), (size_t)3);
    CHECK(host.actions.back() == "activate 1");
    CHECK(!e.failed());
}

TEST("library events reach the script with the tracks") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("function on_library_items_added(l) { console.log('added', l.Count); }"
           "function on_library_items_removed(l) { console.log('removed', l.Count); }"
           "function on_library_items_changed(l) { console.log('changed', l.Item(0).Path); }", "");
    e.library_changed(JsEngine::LibraryChange::Added, { t::track("/a"), t::track("/b") });
    e.library_changed(JsEngine::LibraryChange::Removed, { t::track("/a") });
    e.library_changed(JsEngine::LibraryChange::Changed, { t::track("/c") });
    const std::vector<std::string> want = { "added 2", "removed 1", "changed /c" };
    CHECK(host.console_lines == want);
}

TEST("Panels UI templates: library_tree.js rebuilds when the library changes") {
    t::FakeHost host;
    load_template_js(host);
    const std::string group = "$if2(%album artist%,Unknown artist)^^$if2(%album%,Unknown album)^^%date%";
    JsEngine e(host);
    e.load(host.files["js/library_tree.js"], "js/library_tree.js");
    e.resize(280, 300);
    e.timer(host.timers.begin()->first);
    t::RecordingCanvas empty;
    e.paint(empty);
    CHECK(has_prefix(empty, "text \"The Media Library is empty: add music folders in Preferences"));
    host.libraryTracks = { t::track("/x", { { group, "Gamma^^G^^1999" } }) };
    e.library_changed(JsEngine::LibraryChange::Added, host.libraryTracks);
    CHECK(!host.timers.empty());
    for (const auto& [id, ms] : std::map<int, int>(host.timers)) e.timer(id);
    t::RecordingCanvas filled;
    e.paint(filled);
    CHECK(has_prefix(filled, "text \"Gamma\""));
    CHECK(!e.failed());
}

TEST("Panels UI templates: library_tree.js says so when the library is empty") {
    t::FakeHost host;
    load_template_js(host);
    JsEngine e(host);
    e.load(host.files["js/library_tree.js"], "js/library_tree.js");
    e.resize(280, 300);
    e.timer(host.timers.begin()->first);
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_prefix(c, "text \"The Media Library is empty: add music folders in Preferences"));
}

TEST("Panels UI templates: navidrome_artists.js lists, plays and adds artists") {
    t::FakeHost host;
    load_template_js(host);
    {
        JsEngine e(host);
        e.load(host.files["js/navidrome_artists.js"], "js/navidrome_artists.js");
        t::RecordingCanvas c;
        e.paint(c);
        CHECK(has_prefix(c, "text \"foo_navidrome isn\u2019t installed\""));
    }
    host.library = LibraryState::NotConfigured;
    {
        JsEngine e(host);
        e.load(host.files["js/navidrome_artists.js"], "js/navidrome_artists.js");
        t::RecordingCanvas c;
        e.paint(c);
        CHECK(has_prefix(c, "text \"Set up a server in foo_navidrome first\""));
    }
    host.library = LibraryState::Ready;
    host.albums = { { "al1", "Hybrid Theory", "Linkin Park", "ar1", "", "", 2000, 12 },
                    { "al2", "Meteora", "Linkin Park", "ar1", "", "", 2003, 13 },
                    { "al3", "Push Pull", " Hoobastank", "ar2", "", "", 2018, 11 } };
    JsEngine e(host);
    CHECK(e.load(host.files["js/navidrome_artists.js"], "js/navidrome_artists.js"));
    e.resize(280, 240);
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_prefix(c, "text \"Hoobastank\" 12,30,"));
    CHECK(has_prefix(c, "text \"Linkin Park\" 12,54,"));
    CHECK(has_prefix(c, "text \"2 albums\""));
    e.mouse(Mouse::LeftDouble, 20, 60, 1);
    e.mouse(Mouse::LeftDouble, 20, 36, 1 | 0x8);
    const std::vector<std::string> want = { "artist ar1 replace play", "artist ar2 add" };
    CHECK(host.played == want);
    host.libraryError = "server unreachable";
    JsEngine failing(host);
    failing.load(host.files["js/navidrome_artists.js"], "js/navidrome_artists.js");
    t::RecordingCanvas f;
    failing.paint(f);
    CHECK(has_prefix(f, "text \"Navidrome: server unreachable\""));
    CHECK(!e.failed());
}

TEST("editor: check_syntax reports the error and its line, nothing when the script compiles") {
    CHECK_EQ(check_syntax("function on_paint(gr) { gr.FillSolidRect(0, 0, 1, 1, 0); }", "a.js"), std::string());
    const std::string err = check_syntax("let x = 1;\nfunction (", "a.js");
    CHECK(err.find("SyntaxError") != std::string::npos);
    CHECK(err.find("(line 2") != std::string::npos);
    CHECK_EQ(check_syntax("for (;;) {}", "loop.js"), std::string());
}

TEST("editor: text_pos counts lines and UTF-8 characters") {
    const TextPos p = text_pos("ab\nñandú x", 10);
    CHECK_EQ(p.line, 2);
    CHECK_EQ(p.col, 6);
    CHECK_EQ(text_pos("", 5).line, 1);
    CHECK_EQ(editor_status(TextPos{ 3, 4 }, "", "Applied"), std::string("Line 3, col 4   Syntax OK   ·   Applied"));
    CHECK(editor_status(TextPos{}, "SyntaxError: x", "").find("SyntaxError: x") != std::string::npos);
}

TEST("navidrome: availability follows the host") {
    t::FakeHost host;
    {
        JsEngine e(host);
        e.load("console.log(navidrome.IsAvailable, navidrome.IsConfigured);", "");
        CHECK_EQ(host.console_lines.back(), std::string("false false"));
    }
    host.library = LibraryState::NotConfigured;
    {
        JsEngine e(host);
        e.load("console.log(navidrome.IsAvailable, navidrome.IsConfigured);", "");
        CHECK_EQ(host.console_lines.back(), std::string("true false"));
    }
}

TEST("navidrome: GetArtistInfoAsync resolves with the biography and image URL, rejects an unknown artist") {
    t::FakeHost host;
    host.library = jsp::LibraryState::Ready;
    host.artistInfos["Anberlin"] = { "ar-1", "An American band.", "https://nd/share/img/a?size=600", "https://www.last.fm/music/Anberlin" };
    JsEngine e(host);
    e.load("navidrome.GetArtistInfoAsync('Anberlin').then(i => console.log(i.Id, i.Biography, i.ImageUrl, i.LastFmUrl));"
           "navidrome.GetArtistInfoAsync('Nobody').then(() => console.log('no'), err => console.log('rejected', err.message));", "");
    CHECK_EQ(host.console_lines.size(), (size_t)2);
    CHECK_EQ(host.console_lines[0], std::string("ar-1 An American band. https://nd/share/img/a?size=600 https://www.last.fm/music/Anberlin"));
    CHECK_EQ(host.console_lines[1], std::string("rejected no artist named \"Nobody\" on the server"));
}

TEST("navidrome: GetAlbumsAsync and GetArtistsAsync resolve with plain objects") {
    t::FakeHost host;
    host.library = LibraryState::Ready;
    host.albums = { { "al1", "Hybrid Theory", "Linkin Park", "ar1", "c1", "2026-01-02T00:00:00Z", 2000, 12 },
                    { "al2", "Meteora", "Linkin Park", "ar1", "c2", "", 2003, 13 },
                    { "al3", "Push Pull", "Hoobastank", "ar2", "c3", "", 2018, 11 } };
    JsEngine e(host);
    e.load("navidrome.GetAlbumsAsync().then(a => console.log(a.length, a[0].Id, a[0].Name, a[0].Artist, a[0].Year, a[0].SongCount, a[0].Created));"
           "navidrome.GetArtistsAsync().then(a => console.log(a.map(x => x.Name + ':' + x.Id + ':' + x.AlbumCount + ':' + x.SongCount).join(',')));", "");
    CHECK(!e.failed());
    CHECK_EQ(host.console_lines.size(), (size_t)2);
    CHECK_EQ(host.console_lines[0], std::string("3 al1 Hybrid Theory Linkin Park 2000 12 2026-01-02T00:00:00Z"));
    CHECK_EQ(host.console_lines[1], std::string("Hoobastank:ar2:1:11,Linkin Park:ar1:2:25"));
}

TEST("navidrome: a failed request rejects with the error message") {
    t::FakeHost host;
    host.library = LibraryState::Ready;
    host.libraryError = "server unreachable";
    JsEngine e(host);
    e.load("navidrome.GetAlbumsAsync().then(() => console.log('no'), err => console.log('rejected', err.message));", "");
    CHECK_EQ(host.console_lines.back(), std::string("rejected server unreachable"));
    CHECK(!e.failed());
}

TEST("navidrome: results that arrive after the engine is gone are dropped") {
    t::FakeHost host;
    host.library = LibraryState::Ready;
    host.deferLibrary = true;
    {
        JsEngine e(host);
        e.load("navidrome.GetAlbumsAsync().then(a => console.log('late', a.length));", "");
        CHECK_EQ(host.waiting.size(), (size_t)1);
        host.waiting[0]({ { "x", "y", "z", "", "", "", 0, 1 } }, "");
        CHECK_EQ(host.console_lines.back(), std::string("late 1"));
        e.load("navidrome.GetAlbumsAsync().then(a => console.log('late2', a.length));", "");
    }
    host.waiting[1]({}, "");
    CHECK(host.console_lines.back() != std::string("late2 0"));
}

TEST("navidrome: play and add albums or artists") {
    t::FakeHost host;
    host.library = LibraryState::Ready;
    JsEngine e(host);
    e.load("navidrome.PlayAlbum('al1'); navidrome.AddAlbum('al2'); navidrome.PlayArtist('ar1', false, true); navidrome.AddArtist('ar2');", "");
    const std::vector<std::string> want = { "album al1 replace play", "album al2 add", "artist ar1 add play", "artist ar2 add" };
    CHECK(host.played == want);
    JsEngine bad(host);
    CHECK(!bad.load("navidrome.PlayAlbum();", ""));
}

TEST("handles: GetNowPlaying, Path, TitleFormat with a handle, Compare") {
    t::FakeHost host;
    host.playingTrack = t::track("/m/a.flac", { { "%title%", "A" } });
    JsEngine e(host);
    e.load("const h = fb.GetNowPlaying(); const g = fb.GetNowPlaying();"
           "console.log(h.Path, h.RawPath, fb.TitleFormat('%title%').EvalWithMetadb(h), h.Compare(g));", "");
    CHECK_EQ(host.console_lines.back(), std::string("/m/a.flac /m/a.flac A true"));
    host.playingTrack = nullptr;
    JsEngine none(host);
    none.load("console.log(fb.GetNowPlaying());", "");
    CHECK_EQ(host.console_lines.back(), std::string("null"));
}

TEST("handle lists: library items, Count, Item, Add, AddRange, Clone, Convert, EvalWithMetadbs") {
    t::FakeHost host;
    host.libraryTracks = { t::track("/1", { { "%artist%", "X" } }), t::track("/2", { { "%artist%", "Y" } }) };
    JsEngine e(host);
    CHECK(e.load("const all = fb.GetLibraryItems(); const sel = fb.CreateHandleList();"
                 "sel.Add(all.Item(1)); sel.AddRange(all); const c = sel.Clone(); c.Add(all.Item(0));"
                 "console.log(all.Count, sel.Count, c.Count, sel.Convert().map(h => h.Path).join(','),"
                 "  fb.TitleFormat('%artist%').EvalWithMetadbs(all).join('|'));", ""));
    CHECK_EQ(host.console_lines.back(), std::string("2 3 4 /2,/1,/2 X|Y"));
    JsEngine bad(host);
    CHECK(!bad.load("fb.GetLibraryItems().Item(5);", ""));
    CHECK(bad.error().find("out of range") != std::string::npos);
}

TEST("album art: GetAlbumArtAsync calls on_get_album_art_done with a GdiBitmap, DrawImage draws it") {
    t::FakeHost host;
    host.playingTrack = t::track("/m/a.flac");
    host.art["/m/a.flac"] = std::make_shared<t::FakeImage>(500, 400);
    JsEngine e(host);
    e.load("let img = null;"
           "function on_get_album_art_done(h, id, image, path) { img = image; console.log(h.Path, id, image.Width, image.Height, path); }"
           "utils.GetAlbumArtAsync(window.ID, fb.GetNowPlaying(), 0);"
           "function on_paint(gr) { if (img) gr.DrawImage(img, 4, 6, 44, 44, 0, 0, img.Width, img.Height); }", "");
    CHECK_EQ(host.console_lines.back(), std::string("/m/a.flac 0 500 400 /m/a.flac.jpg"));
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_op(c, "image"));
    CHECK(!e.failed());
}

TEST("album art: no art gives a null image, late results after the engine is gone are dropped") {
    t::FakeHost host;
    host.playingTrack = t::track("/none");
    {
        JsEngine e(host);
        e.load("function on_get_album_art_done(h, id, image) { console.log('art', image); }"
               "utils.GetAlbumArtAsync(window.ID, fb.GetNowPlaying(), 0);", "");
        CHECK_EQ(host.console_lines.back(), std::string("art null"));
    }
    host.deferArt = true;
    {
        JsEngine e(host);
        e.load("function on_get_album_art_done() { console.log('late'); }"
               "utils.GetAlbumArtAsync(window.ID, fb.GetNowPlaying(), 0);", "");
    }
    CHECK_EQ(host.artWaiting.size(), (size_t)1);
    host.artWaiting[0].second(nullptr, "");
    CHECK(host.console_lines.back() != std::string("late"));
}

TEST("album art: on_playback_new_track gets the track's handle") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("function on_playback_new_track(h) { console.log('new', h.Path); }", "");
    e.playback_new_track(t::track("/n.mp3"));
    CHECK_EQ(host.console_lines.back(), std::string("new /n.mp3"));
}

TEST("gdi.Image loads relative to the script folder, DrawImage rejects non-bitmaps") {
    t::FakeHost host;
    host.art["/skin/js/bg.png"] = std::make_shared<t::FakeImage>(10, 10);
    JsEngine e(host);
    e.load("const i = gdi.Image('bg.png'); console.log(i.Width, gdi.Image('missing.png'));"
           "function on_paint(gr) { gr.DrawImage(i, 0, 0, 10, 10, 0, 0, 10, 10); gr.DrawImage({}, 0, 0, 1, 1, 0, 0, 1, 1); }",
           "/skin/js/main.js");
    CHECK_EQ(host.console_lines.back(), std::string("10 null"));
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(e.failed());
    CHECK(e.error().find("GdiBitmap") != std::string::npos);
}

TEST("plman: find or create, clear, insert, activate and play") {
    t::FakeHost host;
    host.libraryTracks = { t::track("/1"), t::track("/2"), t::track("/3") };
    JsEngine e(host);
    CHECK(e.load("const p = plman.FindOrCreatePlaylist('Library selection', true);"
                 "plman.ClearPlaylist(p); plman.InsertPlaylistItems(p, 0, fb.GetLibraryItems());"
                 "plman.ActivePlaylist = p; plman.ExecutePlaylistDefaultAction(p, 0);"
                 "console.log(p, plman.PlaylistCount, plman.ActivePlaylist, plman.GetPlaylistName(p), plman.PlayingPlaylist);", ""));
    CHECK_EQ(host.console_lines.back(), std::string("1 2 1 Library selection -1"));
    const std::vector<std::string> want = { "clear 1", "insert 1 3", "activate 1", "play 1 0" };
    CHECK(host.actions == want);
    CHECK_EQ(host.playlistItems[1].size(), (size_t)3);
}

TEST("keys: on_key_down gets the virtual key and reports whether it was handled, IsKeyPressed asks the host") {
    t::FakeHost host;
    host.keys[0x11] = true;
    JsEngine e(host);
    e.load("function on_key_down(vk) { console.log('key', vk, utils.IsKeyPressed(0x11), utils.IsKeyPressed(0x10)); return vk === 13; }", "");
    CHECK(e.key_down(13));
    CHECK_EQ(host.console_lines.back(), std::string("key 13 true false"));
    CHECK(!e.key_down(0x28));
    CHECK(!e.key_down(0));
    JsEngine none(host);
    none.load("", "");
    CHECK(!none.key_down(13));
}

TEST("shell: Windows shutdown commands map to a power action") {
    CHECK(windows_power_command("shutdown.exe /s /t 00") == PowerAction::Shutdown);
    CHECK(windows_power_command("shutdown.exe /h") == PowerAction::Sleep);
    CHECK(windows_power_command("C:/Windows/System32/shutdown.exe /r") == PowerAction::Restart);
    CHECK(windows_power_command("\"shutdown\" -s") == PowerAction::Shutdown);
    CHECK(windows_power_command("shutdown.exe /a") == PowerAction::None);
    CHECK(windows_power_command("notepad.exe /s") == PowerAction::None);
    CHECK(windows_power_command("") == PowerAction::None);
}

TEST("menus: Windows menu text becomes a plain label") {
    CHECK_EQ(menu_label("Covers && Memory usage"), std::string("Covers & Memory usage"));
    CHECK_EQ(menu_label("&File"), std::string("File"));
    CHECK_EQ(menu_label("Play\tCtrl+P"), std::string("Play"));
    CHECK_EQ(menu_label("A&&&B"), std::string("A&B"));
}

TEST("keys: macOS key codes map to Windows virtual keys") {
    CHECK_EQ(mac_key_to_vk(36, '\r'), 0x0D);
    CHECK_EQ(mac_key_to_vk(76, 3), 0x0D);
    CHECK_EQ(mac_key_to_vk(126, 0xF700), 0x26);
    CHECK_EQ(mac_key_to_vk(125, 0xF701), 0x28);
    CHECK_EQ(mac_key_to_vk(116, 0), 0x21);
    CHECK_EQ(mac_key_to_vk(121, 0), 0x22);
    CHECK_EQ(mac_key_to_vk(115, 0), 0x24);
    CHECK_EQ(mac_key_to_vk(119, 0), 0x23);
    CHECK_EQ(mac_key_to_vk(0, 'a'), (int)'A');
    CHECK_EQ(mac_key_to_vk(18, '1'), (int)'1');
    CHECK_EQ(mac_key_to_vk(122, 0), 0x70);
    CHECK_EQ(mac_key_to_vk(50, '`'), 0);
}

TEST("keys: macOS modifiers for IsKeyPressed: ⌘ is VK_LWIN/RWIN, and still counts as VK_CONTROL") {
    using M = MacModifiers;
    const M none{}, shift{ true }, ctrl{ false, true }, opt{ false, false, true }, cmd{ false, false, false, true };
    CHECK(mac_modifier_pressed(0x10, shift));
    CHECK(!mac_modifier_pressed(0x10, none));
    CHECK(mac_modifier_pressed(0x11, ctrl));
    CHECK(mac_modifier_pressed(0x11, cmd));      // scripts written for Windows read ⌘C as Ctrl+C
    CHECK(mac_modifier_pressed(0x12, opt));
    CHECK(mac_modifier_pressed(0x5B, cmd));      // VK_LWIN: tells ⌘ from ⌃
    CHECK(mac_modifier_pressed(0x5C, cmd));      // VK_RWIN
    CHECK(!mac_modifier_pressed(0x5B, ctrl));
    CHECK(!mac_modifier_pressed(0x41, cmd));
}

TEST("Panels UI templates: navidrome_artists.js plays the selected artist on Enter, adds with Ctrl+Enter") {
    t::FakeHost host;
    load_template_js(host);
    host.library = LibraryState::Ready;
    host.albums = { { "a1", "A", "Alpha", "ar1", "", "", 0, 1 }, { "b1", "B", "Beta", "ar2", "", "", 0, 1 },
                    { "c1", "C", "Gamma", "ar3", "", "", 0, 1 } };
    JsEngine e(host);
    CHECK(e.load(host.files["js/navidrome_artists.js"], "js/navidrome_artists.js"));
    e.resize(280, 240);
    CHECK(e.key_down(0x0D));
    CHECK(host.played.empty());
    e.mouse(Mouse::LeftDown, 20, 36, 1);
    CHECK(e.key_down(0x28));
    CHECK(e.key_down(0x0D));
    CHECK(e.key_down(0x23));
    host.keys[0x11] = true;
    CHECK(e.key_down(0x0D));
    CHECK(!e.key_down('Q'));
    const std::vector<std::string> want = { "artist ar2 replace play", "artist ar3 add" };
    CHECK(host.played == want);
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_prefix(c, "fill_rect 0,78,280,24 #005496"));
    CHECK(!e.failed());
}

TEST("Panels UI templates: library_tree.js walks the tree with the arrows and plays on Enter") {
    t::FakeHost host;
    load_template_js(host);
    const std::string group = "$if2(%album artist%,Unknown artist)^^$if2(%album%,Unknown album)^^%date%";
    const std::string sort = "%album artist%|%date%|%album%|$num(%discnumber%,2)|$num(%tracknumber%,3)|%title%";
    host.libraryTracks = { t::track("/a1", { { group, "Alpha^^One^^2001" }, { sort, "Alpha|2001|One|01|001|x" } }),
                           t::track("/b1", { { group, "Beta^^Two^^2002" }, { sort, "Beta|2002|Two|01|001|y" } }),
                           t::track("/b2", { { group, "Beta^^Three^^2003" }, { sort, "Beta|2003|Three|01|001|z" } }) };
    JsEngine e(host);
    CHECK(e.load(host.files["js/library_tree.js"], "js/library_tree.js"));
    e.resize(280, 300);
    e.timer(host.timers.begin()->first);
    CHECK(e.key_down(0x28));
    CHECK(e.key_down(0x28));
    CHECK(e.key_down(0x27));
    CHECK(e.key_down(0x27));
    CHECK(e.key_down(0x28));
    CHECK(e.key_down(0x0D));
    CHECK_EQ(host.playlistItems[1].size(), (size_t)1);
    CHECK_EQ(host.track_path(host.playlistItems[1][0]), std::string("/b2"));
    CHECK(host.actions.back() == "play 1 0");
    CHECK(e.key_down(0x25));
    CHECK(e.key_down(0x25));
    t::RecordingCanvas closed;
    e.paint(closed);
    CHECK(!has_prefix(closed, "text \"Two"));
    host.keys[0x11] = true;
    CHECK(e.key_down(0x0D));
    CHECK_EQ(host.playlistItems[1].size(), (size_t)3);
    CHECK(!e.failed());
}

TEST("properties: GetProperty stores the default, SetProperty updates or deletes, saved once per callback") {
    t::FakeHost host;
    {
        JsEngine e(host);
        CHECK(e.load("const a = window.GetProperty('size', 12), b = window.GetProperty('name', 'x'), c = window.GetProperty('none');"
                     "console.log(a, b, c, window.GetProperty('toString'));", ""));
        CHECK_EQ(host.console_lines.back(), std::string("12 x null null"));
        CHECK_EQ(host.propertySaves, 1);
        CHECK_EQ(host.properties, std::string(R"({"size":12,"name":"x"})"));
        e.load("function on_size() { window.SetProperty('size', 14); window.SetProperty('name', null); window.SetProperty('obj', { a: [1] }); }", "");
        e.resize(10, 10);
        CHECK_EQ(host.properties, std::string(R"({"size":14,"obj":{"a":[1]}})"));
    }
    JsEngine again(host);
    again.load("console.log(window.GetProperty('size', 1), JSON.stringify(window.GetProperty('obj')), window.GetProperty('name'));", "");
    CHECK_EQ(host.console_lines.back(), std::string("14 {\"a\":[1]} null"));
    host.properties = "not json";
    JsEngine broken(host);
    broken.load("console.log(window.GetProperty('size', 3));", "");
    CHECK_EQ(host.console_lines.back(), std::string("3"));
}

TEST("notify: NotifyOthers hands the host a JSON copy, on_notify_data gets it back as a value") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("window.NotifyOthers('sel', { ids: [1, 2], name: 'a' }); window.NotifyOthers('ping');"
           "function on_notify_data(name, info) { console.log(name, JSON.stringify(info)); }", "");
    CHECK(host.notes.size() == 2);
    CHECK_EQ(host.notes[0].first, std::string("sel"));
    CHECK_EQ(host.notes[0].second, std::string(R"({"ids":[1,2],"name":"a"})"));
    CHECK_EQ(host.notes[1].second, std::string());
    e.notify_data("sel", host.notes[0].second);
    CHECK_EQ(host.console_lines.back(), std::string(R"(sel {"ids":[1,2],"name":"a"})"));
    e.notify_data("ping", "");
    CHECK_EQ(host.console_lines.back(), std::string("ping undefined"));
}

TEST("window: instance info, script info, sizes, colours, fonts, tooltip and profiler stubs") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("window.DefineScript('Eole', { author: 'me', version: '2' }); window.DlgCode = 4; window.MinWidth = 50;"
                 "const tt = window.CreateTooltip('Segoe UI', 12); tt.Text = 'x'; tt.Activate();"
                 "const p = fb.CreateProfiler('t'); p.Reset();"
                 "console.log(window.InstanceType, window.IsVisible, window.IsTransparent, window.ScriptInfo.Name, window.Name,"
                 " window.ScriptInfo.Author, window.ScriptInfo.Version, window.DlgCode, window.MinWidth,"
                 " (window.GetColourDUI(1) >>> 0).toString(16), window.GetFontDUI(3).Name, window.Tooltip.Text, p.Time >= 0,"
                 " window.JsMemoryStats.TotalMemoryLimit === window.MemoryLimit, window.TotalMemoryUsage > 0);", ""));
    CHECK_EQ(host.console_lines.back(), std::string("1 true false Eole Eole me 2 4 50 ff1e1e1e Segoe UI x true true true"));
    host.shown = false;
    JsEngine hidden(host);
    CHECK(hidden.load("console.log(window.IsVisible);", ""));
    CHECK_EQ(host.console_lines.back(), std::string("false"));
}

TEST("fb: paths, volume, flags, menu and context commands, queries, components") {
    t::FakeHost host;
    host.libraryTracks = { t::track("/a/1"), t::track("/b/2") };
    JsEngine e(host);
    CHECK(e.load("fb.Volume = -10; fb.VolumeDown(); fb.StopAfterCurrent = true; fb.Random();"
                 "const lib = fb.GetLibraryItems();"
                 "console.log(fb.ProfilePath, fb.Volume, fb.StopAfterCurrent, fb.CursorFollowPlayback,"
                 " fb.RunMainMenuCommand('View/Console'), fb.RunMainMenuCommand('Nope'),"
                 " fb.RunContextCommandWithMetadb('Properties', lib), fb.GetQueryItems(lib, '/b/').Count,"
                 " utils.CheckComponent('foo_playcount', true), utils.CheckComponent('foo_uie_eslyric'));"
                 "try { fb.GetQueryItems(lib, 'bad('); } catch (e) { console.log(e.name); }", ""));
    CHECK_EQ(host.console_lines[host.console_lines.size() - 2], std::string("/profile/ -12 true false true false true 1 true false"));
    CHECK_EQ(host.console_lines.back(), std::string("SyntaxError"));
    const std::vector<std::string> want = { "main View/Console", "main Nope", "context Properties 2" };
    CHECK(host.commands_run == want);
    CHECK(host.commands.back() == Playback::Random);
}

TEST("handles: GetFileInfo, Length, FileSize; lists insert, find, remove, combine, order and total") {
    t::FakeHost host;
    auto a = t::track("/a", { { "%title%", "beta" } }), b = t::track("/b", { { "%title%", "Alpha" } }), c = t::track("/c", { { "%title%", "gamma" } });
    a->info.meta = { { "ARTIST", { "X", "Y" } }, { "TITLE", { "beta" } } };
    a->info.info = { { "codec", "FLAC" } };
    a->info.length = 61.5;
    a->info.size = 1000;
    b->info.length = 10;
    host.libraryTracks = { a, b, c };
    JsEngine e(host);
    CHECK(e.load("const l = fb.GetLibraryItems(), h = l.Item(0), fi = h.GetFileInfo(), i = fi.MetaFind('artist');"
                 "console.log(fi.MetaCount, i, fi.MetaName(i), fi.MetaValueCount(i), fi.MetaValue(i, 1), fi.InfoValue(fi.InfoFind('CODEC')),"
                 " fi.MetaFind('none'), h.Length, h.FileSize);"
                 "const tf = fb.TitleFormat('%title%');"
                 "const m = l.Clone(); m.OrderByFormat(tf, 1); const ordered = tf.EvalWithMetadbs(m).join(',');"
                 "m.OrderByFormat(tf, -1); const back = tf.EvalWithMetadbs(m).join(',');"
                 "const two = fb.CreateHandleList(); two.Add(l.Item(2)); two.Insert(0, l.Item(1));"
                 "const x = l.Clone(); x.MakeIntersection(two); const y = l.Clone(); y.MakeDifference(two);"
                 "const u = two.Clone(); u.MakeUnion(l); const r = l.Clone(); r.Remove(l.Item(1)); r.RemoveById(0);"
                 "console.log(ordered, back, two.Find(l.Item(2)), x.Count, y.Count, u.Count, r.Count, r.Item(0).Path,"
                 " l.CalcTotalDuration(), l.CalcTotalSize());", ""));
    CHECK_EQ(host.console_lines[host.console_lines.size() - 2], std::string("2 0 ARTIST 2 Y FLAC -1 61.5 1000"));
    CHECK_EQ(host.console_lines.back(), std::string("Alpha,beta,gamma gamma,beta,Alpha 1 2 1 3 1 /c 71.5 1000"));
}

TEST("plman: items, selection, focus, order, playing location, sort, queue") {
    t::FakeHost host;
    auto a = t::track("/a", { { "%title%", "c" } }), b = t::track("/b", { { "%title%", "a" } }), c = t::track("/c", { { "%title%", "b" } });
    host.playlistItems[0] = { a, b, c };
    host.location = { 0, 2 };
    JsEngine e(host);
    CHECK(e.load("plman.SetPlaylistSelection(0, [0, 2], true); plman.SetPlaylistSelectionSingle(0, 0, false);"
                 "plman.SetPlaylistFocusItemByHandle(0, plman.GetPlaylistItems(0).Item(1)); plman.PlaybackOrder = 3;"
                 "const loc = plman.GetPlayingItemLocation();"
                 "console.log(plman.PlaylistItemCount(0), plman.GetPlaylistSelectedItems(0).Item(0).Path,"
                 " plman.IsPlaylistItemSelected(0, 0), plman.IsPlaylistItemSelected(0, 2), plman.GetPlaylistFocusItemIndex(0),"
                 " plman.PlaybackOrder, loc.IsValid, loc.PlaylistIndex, loc.PlaylistItemIndex);"
                 "plman.SortByFormatV2(0, '%title%', 1); plman.AddItemToPlaybackQueue(fb.CreateHandleList().Count === 0 ? plman.GetPlaylistItems(0).Item(0) : null);"
                 "console.log(fb.TitleFormat('%title%').EvalWithMetadbs(plman.GetPlaylistItems(0)).join(''), plman.GetPlaybackQueueHandles().Count);"
                 "plman.FlushPlaybackQueue(); console.log(plman.GetPlaybackQueueHandles().Count);", ""));
    const auto& out = host.console_lines;
    CHECK_EQ(out[out.size() - 3], std::string("3 /c false true 1 3 true 0 2"));
    CHECK_EQ(out[out.size() - 2], std::string("abc 1"));
    CHECK_EQ(out.back(), std::string("0"));
}

TEST("plman: playback queue contents, find, remove and on_playback_queue_changed") {
    t::FakeHost host;
    auto a = t::track("/a"), b = t::track("/b"), c = t::track("/c");
    host.playlistItems[0] = { a, b, c };
    JsEngine e(host);
    CHECK(e.load("function on_playback_queue_changed(origin) { console.log('queue', origin); }"
                 "plman.AddPlaylistItemToPlaybackQueue(0, 1); plman.AddPlaylistItemToPlaybackQueue(0, 2);"
                 "plman.AddItemToPlaybackQueue(plman.GetPlaylistItems(0).Item(0));"
                 "const q = plman.GetPlaybackQueueContents(), b = plman.GetPlaylistItems(0).Item(1);"
                 "console.log(q.length, q[0].Handle.Path, q[0].PlaylistIndex, q[0].PlaylistItemIndex, q[2].PlaylistIndex,"
                 " plman.FindPlaybackQueueItemIndex(b, 0, 1), plman.FindPlaybackQueueItemIndex(b, 0, 2));"
                 "plman.RemoveItemFromPlaybackQueue(0); plman.RemoveItemsFromPlaybackQueue([1, 7]);"
                 "console.log(plman.GetPlaybackQueueContents().map(x => x.Handle.Path).join(','));", ""));
    CHECK_EQ(host.console_lines[host.console_lines.size() - 2], std::string("3 /b 0 1 -1 0 -1"));
    CHECK_EQ(host.console_lines.back(), std::string("/c"));
    e.invoke("on_playback_queue_changed", { 1 });
    CHECK_EQ(host.console_lines.back(), std::string("queue 1"));
}

TEST("plman.AddLocations hands the paths to the host, Windows separators fixed off Windows") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("plman.AddLocations(1, ['C:\\\\m\\\\a.flac', 'http://x/s.mp3'], true); plman.AddLocations(0, ['/b.flac']);", ""));
#ifdef _WIN32
    CHECK_EQ(host.actions[host.actions.size() - 2], std::string("locations 1 C:\\m\\a.flac http://x/s.mp3 select"));
#else
    CHECK_EQ(host.actions[host.actions.size() - 2], std::string("locations 1 C:/m/a.flac http://x/s.mp3 select"));
#endif
    CHECK_EQ(host.actions.back(), std::string("locations 0 /b.flac"));
}

TEST("utils: ReadINI / WriteINI keep sections, case-insensitive keys and the file's line endings") {
    t::FakeHost host;
    host.files["/p/a.ini"] = "; c\r\n[Main]\r\nName = \"Eole\"\r\nsize=3\r\n\r\n[Other]\r\nx=1\r\n";
    JsEngine e(host);
    CHECK(e.load("console.log(utils.ReadINI('/p/a.ini', 'main', 'NAME'), utils.ReadINI('/p/a.ini', 'Main', 'nope', 'def'), utils.ReadINI('/p/none.ini', 'a', 'b'),"
                 " utils.WriteINI('/p/a.ini', 'Main', 'SIZE', '4'), utils.WriteINI('/p/a.ini', 'Main', 'new', 'n'),"
                 " utils.WriteINI('/p/a.ini', 'Third', 'k', 'v'), utils.WriteINI('/p/a.ini', '', 'k', 'v'));", ""));
    CHECK_EQ(host.console_lines.back(), std::string("Eole def  true true true false"));
    CHECK_EQ(host.files["/p/a.ini"], std::string("; c\r\n[Main]\r\nName = \"Eole\"\r\nsize=4\r\nnew=n\r\n\r\n[Other]\r\nx=1\r\n\r\n[Third]\r\nk=v\r\n"));
}

TEST("packages: a relative include that isn't next to the calling file is looked for in the package's scripts folder") {
    t::FakeHost host;
    host.files["/p/foo_spider_monkey_panel/packages/{ABC}/scripts/utils.js"] = "var where = 'scripts';";
    host.files["/p/foo_spider_monkey_panel/packages/{ABC}/local.js"] = "var near = 'root';";
    JsEngine e(host);
    CHECK(e.load("include('local.js'); include('utils.js'); console.log(near, where);", "/p/foo_spider_monkey_panel/packages/{ABC}/main.js"));
    if (e.failed()) printf("%s\n", e.error().c_str());
    CHECK_EQ(host.console_lines.back(), std::string("root scripts"));
    JsEngine plain(host);
    CHECK(!plain.load("include('utils.js');", "/s/main.js"));
}

TEST("include: a file is evaluated once unless always_evaluate is set, however its path is spelled") {
    t::FakeHost host;
    host.files["/s/lib/count.js"] = "var n = (typeof n === 'number' ? n : 0) + 1;";
    JsEngine e(host);
    CHECK(e.load("include('lib/count.js'); include('lib//count.js'); include('/s/lib/../lib/count.js'); const once = n;"
                 "include('lib/count.js', { always_evaluate: true }); console.log(once, n);", "/s/main.js"));
    CHECK_EQ(host.console_lines.back(), std::string("1 2"));
}

TEST("include: a relative path resolves against the file of the function that calls include, as in SMP") {
    t::FakeHost host;
    host.files["/s/lib/helpers.js"] = "function load_near(p) { include(p); }";
    host.files["/s/lib/other.js"] = "var from = 'lib';";
    host.files["/s/top.js"] = "var top = 'top';";
    JsEngine e(host);
    CHECK(e.load("include('lib/helpers.js'); load_near('other.js'); include('top.js'); console.log(from, top);", "/s/main.js"));
    if (e.failed()) printf("%s\n", e.error().c_str());
    CHECK_EQ(host.console_lines.back(), std::string("lib top"));
}

TEST("packages: GetPackageInfo / GetPackagePath find SMP 1.6 packages, ScriptInfo.PackageId names the running one") {
    namespace fs = std::filesystem;
    const fs::path profile = fs::temp_directory_path() / "jsp_pkg_test";
    fs::remove_all(profile);
    const fs::path pkg = profile / "foo_spider_monkey_panel" / "packages" / "{ABC}";
    fs::create_directories(pkg);
    std::ofstream(pkg / "package.json") << "{\"id\":\"{ABC}\",\"version\":\"1.2.0\"}";
    t::FakeHost host;
    host.profilePath = profile.generic_string() + "/";
    host.files[(pkg / "package.json").generic_string()] = "{\"id\":\"{ABC}\",\"version\":\"1.2.0\"}";
    JsEngine e(host);
    CHECK(e.load("const i = utils.GetPackageInfo('{ABC}'), p = fb.ProfilePath;"
                 "console.log(i.Version, i.Directories.Root.slice(p.length), i.Directories.Storage.slice(p.length), utils.GetPackageInfo('nope'),"
                 " utils.GetPackageInfo('../x'), utils.GetPackagePath('{ABC}') === i.Directories.Root, window.ScriptInfo.PackageId);"
                 "try { utils.GetPackagePath('nope'); } catch (e) { console.log('threw'); }", (pkg / "main.js").string()));
    CHECK_EQ(host.console_lines[host.console_lines.size() - 2],
             std::string("1.2.0 foo_spider_monkey_panel/packages/{ABC}/ foo_spider_monkey_panel/package_data/{ABC}/ null null true {ABC}"));
    CHECK_EQ(host.console_lines.back(), std::string("threw"));
    fs::remove_all(profile);
}

TEST("fetch: Response text/json, headers sent, HTTP errors resolve, network errors reject") {
    t::FakeHost host;
    host.web["https://api.example/j"] = { 200, "{\"a\":1}", {} };
    host.web["https://api.example/down"] = { 0, "", "host not found" };
    JsEngine e(host);
    CHECK(e.load("fetch('https://api.example/j', { headers: { 'X-Key': 'k' } }).then(r => r.json().then(j => console.log('json', r.ok, r.status, j.a, r.bodyUsed)));"
                 "fetch('https://api.example/nope', { method: 'post', body: 'b' }).then(r => console.log('miss', r.ok, r.status));"
                 "fetch('https://api.example/down').catch(e => console.log('down', e.name, e.message));"
                 "const h = new Headers({ A: '1' }); h.append('a', '2'); console.log('headers', h.get('A'), h.has('b'));", ""));
    CHECK_EQ(host.console_lines.back(), std::string("headers 1, 2 false"));
    host.finish_http();
    const auto& out = host.console_lines;
    CHECK(std::find(out.begin(), out.end(), "json true 200 1 true") != out.end());
    CHECK(std::find(out.begin(), out.end(), "miss false 404") != out.end());
    CHECK(std::find(out.begin(), out.end(), "down TypeError fetch failed: host not found") != out.end());
    CHECK_EQ(host.requests[0], std::string("GET https://api.example/j [x-key: k]"));
    CHECK_EQ(host.requests[1], std::string("POST https://api.example/nope body=b"));
    CHECK(!e.failed());
}

TEST("modules: a module script imports relative files, exports its callbacks, records the files for hot reload") {
    t::FakeHost host;
    host.files["/s/lib/a.js"] = "import { b } from '../b.js'; export const a = 'A' + b;";
    host.files["/s/b.js"] = "export const b = 'B';";
    JsEngine e(host);
    CHECK(e.load("import { a } from './lib/a.js';\nconsole.log('top', a);\nexport function on_size(w, h) { console.log('size', a, w, h); }\nfunction hidden() {}", "/s/main.js"));
    CHECK_EQ(host.console_lines.back(), std::string("top AB"));
    e.resize(3, 4);
    CHECK_EQ(host.console_lines.back(), std::string("size AB 3 4"));
    const auto& files = e.source_files();
    CHECK(std::find(files.begin(), files.end(), "/s/lib/a.js") != files.end() && std::find(files.begin(), files.end(), "/s/b.js") != files.end());
    t::FakeHost h2;
    JsEngine broken(h2);
    CHECK(!broken.load("import { x } from './missing.js';", "/s/main.js"));
    CHECK(broken.error().find("can't read /s/missing.js") != std::string::npos);
    t::FakeHost h3;
    h3.files["/s/b.js"] = "export const b = 'B';";
    JsEngine classic(h3);
    CHECK(classic.load("import('./b.js').then(m => console.log('dynamic', m.b));", "/s/main.js"));
    CHECK_EQ(h3.console_lines.back(), std::string("dynamic B"));
    CHECK(check_syntax("import { a } from './a.js'; export const b = a;", "m.js").empty());
    CHECK(!check_syntax("import { a } from './a.js'; export const = a;", "m.js").empty());
}

TEST("profile: every callback and timer is counted, slowest first, with the engine's memory") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("function on_size() { const t = Date.now(); while (Date.now() - t < 12) {} } function on_mouse_move() {}"
                 "window.SetTimeout(() => {}, 1);", ""));
    e.resize(1, 1);
    e.mouse(Mouse::Move, 0, 0, 0);
    e.mouse(Mouse::Move, 1, 0, 0);
    const auto pending = host.timers;
    for (const auto& [id, ms] : pending) e.timer(id);
    const auto p = e.profile();
    CHECK(p.size() == 3 && p[0].name == "on_size" && p[0].calls == 1 && p[0].maxMs >= 10);
    const auto move = std::find_if(p.begin(), p.end(), [](const CallTiming& c) { return c.name == "on_mouse_move"; });
    CHECK(move != p.end() && move->calls == 2);
    CHECK(std::find_if(p.begin(), p.end(), [](const CallTiming& c) { return c.name == "timers" && c.calls == 1; }) != p.end());
    CHECK(e.memory_used() > 0);
}

TEST("skin messages reach on_notify_data: JSON info parsed, anything else as a string") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("function on_notify_data(name, info) { console.log(name, typeof info, JSON.stringify(info)); }", ""));
    e.skin_message("view", "{\"page\":2}");
    CHECK_EQ(host.console_lines.back(), std::string("view object {\"page\":2}"));
    e.skin_message("mode", "compact");
    CHECK_EQ(host.console_lines.back(), std::string("mode string \"compact\""));
    e.skin_message("ping", "");
    CHECK_EQ(host.console_lines.back(), std::string("ping string \"\""));
}

TEST("navidrome: SearchAsync resolves with artists, albums and a track list, rejects on an error") {
    t::FakeHost host;
    jsp::SearchResult r;
    r.artists = { { "ar1", "Anberlin", "cov-ar1", 7, 0 } };
    r.albums = { { "al1", "Cities", "Anberlin", "ar1", "cov-al1", "2007-02-20", 2007, 13 } };
    r.tracks = { t::track("navidrome://s1"), t::track("navidrome://s2") };
    host.navidromeResults["anb"] = r;
    JsEngine e(host);
    CHECK(e.load("navidrome.SearchAsync('anb').then(r => console.log(r.Artists[0].Name, r.Artists[0].CoverArtId, r.Artists[0].AlbumCount,"
                 " r.Albums[0].Name, r.Albums[0].Year, r.Tracks.Count, r.Tracks.Item(1).Path));"
                 "navidrome.SearchAsync('x').then(() => console.log('no'), err => console.log('rejected', err.message));", ""));
    CHECK_EQ(host.console_lines[host.console_lines.size() - 2], std::string("Anberlin cov-ar1 7 Cities 2007 2 navidrome://s2"));
    CHECK_EQ(host.console_lines.back(), std::string("rejected search failed for \"x\""));
}

TEST("mouse: horizontal wheel and right double-click reach their own callbacks") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("function on_mouse_wheel(s) { console.log('v', s); } function on_mouse_wheel_h(s) { console.log('h', s); }"
                 "function on_mouse_rbtn_dblclk(x, y, m) { console.log('rdbl', x, y, m); }", ""));
    e.wheel(2, true);
    CHECK_EQ(host.console_lines.back(), std::string("h 2"));
    e.wheel(-1);
    CHECK_EQ(host.console_lines.back(), std::string("v -1"));
    e.mouse(Mouse::RightDouble, 3, 4, 2);
    CHECK_EQ(host.console_lines.back(), std::string("rdbl 3 4 2"));
}

TEST("utils: files, Glob, split paths, durations and sizes; backslashes work as separators off Windows") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "jsp_utils_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    std::ofstream(dir / "PSS_1.txt") << "abc";
    std::ofstream(dir / "pss_2.txt") << "";
    std::ofstream(dir / "other.txt") << "";
    t::FakeHost host;
    std::string d = dir.string() + "/";
#ifndef _WIN32
    std::string bs = d;
    std::replace(bs.begin(), bs.end(), '/', '\\');
#else
    std::string bs = d;
#endif
    const auto js = [](const std::string& in) { std::string s; for (char c : in) { if (c == '\\') s += "\\\\"; else s += c; } return s; };
    JsEngine e(host);
    CHECK(e.load("const d = '" + js(d) + "', bs = '" + js(bs) + "';"
                 "const g = utils.Glob(bs + 'PSS_*').map(p => p.slice(d.length));"
                 "console.log(g.join(','), utils.Glob(d + '*', 0).length, utils.FileTest(d + 'PSS_1.txt', 'e'), utils.FileTest(bs + 'PSS_1.txt', 's'),"
                 " utils.IsDirectory(d + 'sub'), utils.IsFile(d + 'sub'), utils.FileTest(d + 'nope', 'e'),"
                 " utils.SplitFilePath('C:\\\\x\\\\song.flac').join('|'), utils.FormatDuration(3725), utils.FormatDuration(65),"
                 " utils.FormatFileSize(1536), utils.WriteTextFile('/p/x.txt', 'hi'), utils.ReadTextFile('/p/x.txt'));", ""));
    std::string split = "C:/x/|song|.flac";
#ifdef _WIN32
    split = "C:\\x\\|song|.flac";
#endif
    CHECK_EQ(host.console_lines.back(), "PSS_1.txt,pss_2.txt 4 true 3 true false false " + split + " 1:02:05 1:05 1.50 KB true hi");
    fs::remove_all(dir);
}

TEST("invoke: generic callbacks get ints, strings, handles and lists, and report the return value") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("function on_playlist_items_removed(p, n) { console.log('removed', p, n); }"
           "function on_metadb_changed(list, fromhook) { console.log('changed', list.Count, fromhook); return true; }"
           "function on_item_focus_change(p, from, to) { console.log('focus', p, from, to); }", "");
    e.invoke("on_playlist_items_removed", { 1, 4 });
    CHECK_EQ(host.console_lines.back(), std::string("removed 1 4"));
    CHECK(e.invoke("on_metadb_changed", { Tracks{ t::track("/a") }, false }));
    CHECK_EQ(host.console_lines.back(), std::string("changed 1 false"));
    CHECK(!e.invoke("on_item_focus_change", { 0, -1, 2 }));
    CHECK_EQ(host.console_lines.back(), std::string("focus 0 -1 2"));
    CHECK(!e.invoke("on_nothing"));
}

TEST("menus: items, separators, submenus, checks and radio reach the host; TrackPopupMenu returns the pick") {
    t::FakeHost host;
    host.menuChoice = [](const Menu& m) { return m.size() > 3 ? m[3].children[1].id : 0; };
    JsEngine e(host);
    CHECK(e.load("const m = window.CreatePopupMenu(), sub = window.CreatePopupMenu();"
                 "m.AppendMenuItem(0, 1, 'One'); m.AppendMenuItem(0x1, 2, 'Grayed'); m.AppendMenuSeparator();"
                 "sub.AppendMenuItem(0, 10, 'A'); sub.AppendMenuItem(0, 11, 'B'); sub.AppendMenuItem(0, 12, 'C');"
                 "sub.AppendTo(m, 0, 'Sub'); sub.CheckMenuRadioItem(10, 12, 11); m.CheckMenuItem(1, true); m.EnableMenuItem(2, true);"
                 "sub.AppendMenuItem(0x8, 13, 'Late');"
                 "console.log('picked', m.TrackPopupMenu(5, 6));"
                 "try { m.AppendMenuItem(0x10, sub, 'x'); } catch (e) { console.log(e.name); }", ""));
    CHECK_EQ(host.console_lines[host.console_lines.size() - 2], std::string("picked 11"));
    CHECK_EQ(host.console_lines.back(), std::string("TypeError"));
    const Menu& m = host.shownMenu;
    CHECK_EQ(m.size(), (size_t)4);
    CHECK(m[0].checked && !m[0].disabled && m[0].text == "One" && m[0].id == 1);
    CHECK(!m[1].disabled);
    CHECK(m[2].separator);
    CHECK(m[3].submenu && m[3].text == "Sub" && m[3].children.size() == 4);
    CHECK(m[3].children[1].radio && m[3].children[1].checked && !m[3].children[0].checked && m[3].children[0].radio);
    CHECK(m[3].children[3].checked && !m[3].children[3].radio);
}

TEST("menus: a menu appended to itself doesn't recurse forever") {
    t::FakeHost host;
    host.menuChoice = [](const Menu& m) {
        int depth = 0;
        for (const Menu* cur = &m; !cur->empty() && cur->back().submenu; cur = &cur->back().children) ++depth;
        return depth;
    };
    JsEngine e(host);
    CHECK(e.load("const m = window.CreatePopupMenu(); m.AppendMenuItem(0, 1, 'x'); m.AppendTo(m, 0, 'self'); console.log(m.TrackPopupMenu(0, 0) > 0);", ""));
    CHECK_EQ(host.console_lines.back(), std::string("true"));
}

TEST("menus: context and main menu managers build into a menu and execute by id") {
    t::FakeHost host;
    host.libraryTracks = { t::track("/a"), t::track("/b") };
    host.menuChoice = [](const Menu& m) { return m.back().children.back().id; };
    JsEngine e(host);
    CHECK(e.load("const m = window.CreatePopupMenu(), cm = fb.CreateContextMenuManager(), mm = fb.CreateMainMenuManager();"
                 "cm.InitContext(fb.GetLibraryItems()); cm.BuildMenu(m, 1, -1);"
                 "mm.Init('Playback'); mm.BuildMenu(m, 100, 1);"
                 "const id = m.TrackPopupMenu(0, 0);"
                 "console.log(id, mm.ExecuteByID(id - 100), cm.ExecuteByID(1));"
                 "try { fb.CreateMainMenuManager().Init('Nope'); } catch (e) { console.log(e.name); }"
                 "try { fb.CreateContextMenuManager().BuildMenu(m, 1); } catch (e) { console.log(e.name); }", ""));
    const auto& out = host.console_lines;
    CHECK_EQ(out[out.size() - 3], std::string("100 true true"));
    CHECK_EQ(out[out.size() - 2], std::string("RangeError"));
    CHECK_EQ(out.back(), std::string("InternalError"));
    CHECK_EQ(host.shownMenu.size(), (size_t)2);
    CHECK_EQ(host.shownMenu[0].text, std::string("tracks:2"));
    CHECK_EQ(host.shownMenu[0].children.size(), (size_t)2);
    CHECK_EQ(host.shownMenu[1].children.size(), (size_t)1);
    CHECK_EQ(host.shownMenu[0].children[1].id, 2);
}

TEST("menus: time spent in a popup menu or input box doesn't count against the call budget") {
    t::FakeHost host;
    host.menuChoice = [](const Menu&) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
        while (std::chrono::steady_clock::now() < until) {}
        return 1;
    };
    JsEngine e(host, 200);
    e.load("function on_mouse_rbtn_up() { const m = window.CreatePopupMenu(); m.AppendMenuItem(0, 1, 'x');"
           " const id = m.TrackPopupMenu(0, 0); let n = 0; for (let i = 0; i < 200000; ++i) n += i; console.log('after', id); return true; }", "");
    CHECK(e.mouse(Mouse::RightUp, 0, 0, 0));
    CHECK(!e.failed());
    CHECK_EQ(host.console_lines.back(), std::string("after 1"));
    CHECK(!e.in_modal());
}

TEST("utils.InputBox returns the text, the default on cancel, or throws when asked to") {
    t::FakeHost host;
    host.inputValue = "typed";
    JsEngine e(host);
    CHECK(e.load("console.log(utils.InputBox(window.ID, 'Name?', 'Title', 'def', true));", ""));
    CHECK_EQ(host.console_lines.back(), std::string("typed"));
    CHECK_EQ(host.inputPrompt, std::string("Name?"));
    host.inputOk = false;
    CHECK(e.load("console.log(utils.InputBox(window.ID, 'Name?', 'Title', 'def'));"
                 "try { utils.InputBox(window.ID, 'a', 'b', 'c', true); } catch (e) { console.log('cancelled'); }", ""));
    CHECK_EQ(host.console_lines[host.console_lines.size() - 2], std::string("def"));
    CHECK_EQ(host.console_lines.back(), std::string("cancelled"));
}

TEST("input: cursor, middle button, key up, chars and focus reach the script") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("window.SetCursor(32649);"
           "function on_mouse_mbtn_down(x, y, m) { console.log('mdown', x, y, m); }"
           "function on_mouse_mbtn_up(x, y) { console.log('mup', x, y); return true; }"
           "function on_mouse_mbtn_dblclk(x, y) { console.log('mdbl', x, y); }"
           "function on_key_up(vk) { console.log('up', vk); return true; }"
           "function on_char(c) { console.log('char', String.fromCodePoint(c)); }"
           "function on_focus(f) { console.log('focus', f); }", "");
    CHECK(host.cursors == std::vector<int>{ 32649 });
    e.mouse(Mouse::MiddleDown, 3, 4, 0x10);
    CHECK_EQ(host.console_lines.back(), std::string("mdown 3 4 16"));
    CHECK(e.mouse(Mouse::MiddleUp, 3, 4, 0));
    e.mouse(Mouse::MiddleDouble, 1, 2, 0);
    CHECK_EQ(host.console_lines.back(), std::string("mdbl 1 2"));
    CHECK(e.invoke("on_key_up", { 65 }));
    e.invoke("on_char", { 0x1F600 });
    CHECK_EQ(host.console_lines.back(), std::string("char \xF0\x9F\x98\x80"));
    e.invoke("on_focus", { true });
    CHECK_EQ(host.console_lines.back(), std::string("focus true"));
}

TEST("keys: macOS characters map to the WM_CHAR codes scripts expect") {
    CHECK_EQ(mac_char_to_wm_char(U'a', false), 97u);
    CHECK_EQ(mac_char_to_wm_char(0x7f, false), 8u);
    CHECK_EQ(mac_char_to_wm_char(U'\r', false), 13u);
    CHECK_EQ(mac_char_to_wm_char(0x1b, false), 27u);
    CHECK_EQ(mac_char_to_wm_char(U'\t', false), 9u);
    CHECK_EQ(mac_char_to_wm_char(0xF700, false), 0u);
    CHECK_EQ(mac_char_to_wm_char(U'c', true), 0u);
    CHECK_EQ(mac_char_to_wm_char(0x1F600, false), 0x1F600u);
}

TEST("images: CreateImage, GetGraphics draws into the pixels, ReleaseGraphics ends that gr") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("const img = gdi.CreateImage(4, 3); let kept;"
                 "let g = img.GetGraphics(); g.FillSolidRect(0, 0, 4, 3, 0xff112233); g.FillSolidRect(0, 0, 2, 3, 0x80ffffff); img.ReleaseGraphics(g);"
                 "kept = g;"
                 "console.log(img.Width, img.Height, img.GetColourScheme(2).map(c => (c >>> 0).toString(16)).join(','));"
                 "try { kept.FillSolidRect(0, 0, 1, 1, 0); } catch (e) { console.log(e.name); }"
                 "function on_paint(gr) { gr.DrawImage(img, 0, 0, 4, 3, 0, 0, 4, 3); }", ""));
    const auto& out = host.console_lines;
    CHECK_EQ(out[out.size() - 2], std::string("4 3 ff182838,ff889898"));
    CHECK_EQ(out.back(), std::string("TypeError"));
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_op(c, "image"));
    CHECK_EQ(host.canvasOps[0], std::string("fill_rect 0,0,4,3 #112233"));
}

TEST("images: Clone, Resize, RotateFlip, ApplyMask, StackBlur, InvertColours, ApplyAlpha, SaveAs, raw bitmaps") {
    t::FakeHost host;
    host.art["/cover.png"] = std::make_shared<t::FakeImage>(6, 4, 0xFFFF0000u);
    JsEngine e(host);
    CHECK(e.load("const src = gdi.Image('/cover.png');"
                 "const part = src.Clone(1, 1, 3, 2), big = src.Resize(12, 8), near = src.Resize(3, 2, 5);"
                 "src.RotateFlip(1);"
                 "const mask = gdi.CreateImage(6, 4); const mg = mask.GetGraphics(); mg.FillSolidRect(0, 0, 3, 4, 0xffffffff); mask.ReleaseGraphics(mg);"
                 "const masked = gdi.Image('/cover.png'); const ok = masked.ApplyMask(mask), bad = masked.ApplyMask(part);"
                 "masked.StackBlur(2); const inv = big.InvertColours(), half = big.ApplyAlpha(128);"
                 "console.log(part.Width, part.Height, big.Width, near.Height, src.Width, src.Height, ok, bad,"
                 " (inv.GetColourScheme(1)[0] >>> 0).toString(16), JSON.parse(half.GetColourSchemeJSON(1))[0].freq,"
                 " big.SaveAs('C:\\\\out\\\\x.png'), typeof big.CreateRawBitmap().Width);"
                 "function on_paint(gr) { const raw = big.CreateRawBitmap(); gr.GdiDrawBitmap(raw, 0, 0, 12, 8, 0, 0, 12, 8); gr.GdiAlphaBlend(raw, 0, 0, 12, 8, 0, 0, 12, 8, 100); }", ""));
    CHECK_EQ(host.console_lines.back(), std::string("3 2 12 2 4 6 true false ff08f8f8 1 true number"));
#ifdef _WIN32
    CHECK_EQ(host.saved.back(), std::string("C:\\out\\x.png 12x8"));
#else
    CHECK_EQ(host.saved.back(), std::string("C:/out/x.png 12x8"));
#endif
    t::RecordingCanvas c;
    e.paint(c);
    CHECK_EQ(std::count(c.ops.begin(), c.ops.end(), std::string("image")), (long)2);
}

TEST("gr: outlines, polygons from flat or paired points, line wrapping and measuring") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("const f = gdi.Font('Segoe UI', 12);"
           "function on_paint(gr) {"
           " gr.DrawEllipse(0, 0, 20, 10, 1, 0xffffffff); gr.DrawRoundRect(0, 0, 40, 20, 4, 4, 3, 0xffffffff);"
           " gr.FillPolygon(0xff00ff00, 0, [0, 0, 10, 0, 5, 8]); gr.FillPolygon(0xff00ff00, 0, [[0, 0], [10, 0], [5, 8]]);"
           " gr.DrawPolygon(0xffff0000, 2, [0, 0, 10, 0, 5, 8]);"
           " const l = gr.EstimateLineWrap('aaa bbb ccc\\ndd', f, 50);"
           " const m = gr.MeasureString('aaa bbb ccc', f, 1, 2, 50, 100, 0);"
           " console.log(l.join('|'), m.X, m.Y, m.Width, m.Height, m.Lines, m.Chars); }", "");
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(!e.failed());
    CHECK(std::count(c.ops.begin(), c.ops.end(), std::string("polygon")) >= 16 + 36 + 2 + 3);
    CHECK(has_prefix(c, "round_rect"));
    CHECK_EQ(host.console_lines.back(), std::string("aaa bbb|49|ccc|21|dd|14 1 2 49 24 2 11"));
}

TEST("images: LoadImageAsync calls on_load_image_done later with its task id; LoadImageAsyncV2 and GetAlbumArtAsyncV2 resolve") {
    t::FakeHost host;
    host.art["/a.png"] = std::make_shared<t::FakeImage>(5, 5);
    host.art["/t"] = std::make_shared<t::FakeImage>(7, 7);
    host.libraryTracks = { t::track("/t") };
    JsEngine e(host);
    e.load("const id = gdi.LoadImageAsync(window.ID, '/a.png'); console.log('asked', id > 0);"
           "function on_load_image_done(tid, img, path) { console.log('done', tid === id, img.Width, path); }"
           "gdi.LoadImageAsyncV2(window.ID, '/missing.png').then(img => console.log('v2', img));"
           "const h = fb.GetLibraryItems().Item(0);"
           "console.log('sync', utils.GetAlbumArtV2(h, 0).Width);"
           "utils.GetAlbumArtAsyncV2(window.ID, h, 0).then(r => console.log('art', r.image.Width, r.path));", "");
    CHECK_EQ(host.console_lines[0], std::string("asked true"));
    CHECK_EQ(host.console_lines[1], std::string("sync 7"));
    CHECK_EQ(host.console_lines[2], std::string("art 7 /t.jpg"));
    host.finish_loads();
    CHECK_EQ(host.console_lines[3], std::string("done true 5 /a.png"));
    CHECK_EQ(host.console_lines.back(), std::string("v2 null"));
}

#ifndef _WIN32
TEST("images: LoadImageAsync hands the script back the path it asked for, Windows separators included") {
    t::FakeHost host;
    host.art["/art/a.jpg"] = std::make_shared<t::FakeImage>(3, 3);
    JsEngine e(host);
    e.load("gdi.LoadImageAsync(0, '\\\\art\\\\a.jpg');"
           "function on_load_image_done(tid, img, path) { console.log(img.Width, path); }", "");
    host.finish_loads();
    CHECK_EQ(host.console_lines.back(), std::string("3 \\art\\a.jpg"));
}
#endif

TEST("gr: translucent shapes and DrawString are drawn off-screen and composited with their alpha; GdiDrawText stays opaque") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("const f = gdi.Font('Segoe UI', 12);"
           "function on_paint(gr) { gr.FillRoundRect(10, 10, 20, 10, 5, 5, 0x80ff0000); gr.FillEllipse(0, 0, 4, 4, 0x00ffffff);"
           " gr.DrawString('a', f, 0x40ffffff, 0, 0, 50, 20, 0); gr.GdiDrawText('b', f, 0x40ffffff, 0, 0, 50, 20, 0); }", "");
    t::RecordingCanvas c;
    e.paint(c);
    CHECK_EQ(std::count(c.ops.begin(), c.ops.end(), std::string("image")), (long)2);
    CHECK(has_op(c, "text \"b\" 0,0,50,20 f0 #ffffff"));
    CHECK(std::find(host.canvasOps.begin(), host.canvasOps.end(), std::string("round_rect 0,0,20,10 10,10 #ff0000")) != host.canvasOps.end());
    CHECK(std::find(host.canvasOps.begin(), host.canvasOps.end(), std::string("text \"a\" 0,0,50,20 f128 #ffffff")) != host.canvasOps.end());
    CHECK(!has_prefix(c, "round_rect"));
}

namespace {

std::string js_path(const std::filesystem::path& p) {
    std::string s;
    for (char c : p.string()) s += c == '\\' ? std::string("\\\\") : std::string(1, c);
    return s;
}

}

TEST("ActiveX: Scripting.FileSystemObject creates, writes, reads, lists, moves and deletes on disk") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "jsp_fso_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    t::FakeHost host;
    JsEngine e(host);
    const std::string root = js_path(dir);
    CHECK(e.load("const fso = new ActiveXObject('Scripting.FileSystemObject'), root = '" + root + "';"
                 "const sub = fso.BuildPath(root, 'cache'); fso.CreateFolder(sub);"
                 "let ts = fso.CreateTextFile(fso.BuildPath(sub, 'a.txt'), true); ts.WriteLine('one'); ts.Write('two'); ts.Close();"
                 "ts = fso.OpenTextFile(fso.BuildPath(sub, 'a.txt'), 8, true); ts.Write('!'); ts.Close();"
                 "ts = fso.OpenTextFile(fso.BuildPath(sub, 'a.txt'), 1); const l1 = ts.ReadLine(), rest = ts.ReadAll(); ts.Close();"
                 "fso.CopyFile(fso.BuildPath(sub, 'a.txt'), fso.BuildPath(sub, 'PSS_1.txt'));"
                 "fso.CopyFile(fso.BuildPath(sub, 'a.txt'), fso.BuildPath(sub, 'PSS_2.txt'));"
                 "fso.CreateFolder(fso.BuildPath(sub, 'inner'));"
                 "const folder = fso.GetFolder(sub), names = []; for (const f of folder.Files) names.push(f.Name);"
                 "const file = fso.GetFile(fso.BuildPath(sub, 'a.txt'));"
                 "const fts = file.OpenAsTextStream(1, -1), viaFile = fts.ReadAll(); fts.Close(); file.Copy(fso.BuildPath(sub, 'c.txt'));"
                 "const copied = fso.FileExists(fso.BuildPath(sub, 'c.txt')); fso.GetFile(fso.BuildPath(sub, 'c.txt')).Delete();"
                 "console.log(viaFile.replace(/\\r?\\n/, '+'), copied, fso.FileExists(fso.BuildPath(sub, 'c.txt')));"
                 "console.log(l1, rest, names.join(','), folder.Files.Count, folder.SubFolders.Count, folder.SubFolders.Item(0).Name,"
                 " file.Size, file.DateLastModified instanceof Date, fso.FileExists(fso.BuildPath(sub, 'a.txt')), fso.FolderExists(sub));"
                 "fso.DeleteFile(fso.BuildPath(sub, 'PSS_*'));"
                 "fso.MoveFile(fso.BuildPath(sub, 'a.txt'), fso.BuildPath(sub, 'b.txt'));"
                 "let err = ''; try { fso.GetFile(fso.BuildPath(sub, 'a.txt')); } catch (x) { err = x.message + ' ' + x.number; }"
                 "try { fso.CreateFolder(sub); } catch (x) { err += ' | ' + x.message; }"
                 "console.log(fso.GetFolder(sub).Files.Count, fso.FileExists(fso.BuildPath(sub, 'b.txt')), err);"
                 "fso.DeleteFolder(sub); console.log(fso.FolderExists(sub),"
                 " fso.GetBaseName('C:\\\\x\\\\song.flac'), fso.GetExtensionName('C:\\\\x\\\\song.flac'), fso.GetParentFolderName('C:\\\\x\\\\song.flac'), fso.GetFileName('C:\\\\x\\\\'));", ""));
    const auto& out = host.console_lines;
    CHECK_EQ(out[out.size() - 4], std::string("one+two! true false"));
    CHECK_EQ(out[out.size() - 3], std::string("one two! PSS_1.txt,PSS_2.txt,a.txt 3 1 inner 9 true true true"));
    CHECK_EQ(out[out.size() - 2], std::string("1 true File not found -2146828235 | File already exists"));
    CHECK_EQ(out.back(), std::string("false song flac C:\\x x"));
    fs::remove_all(dir);
}

TEST("ActiveX: XMLHTTP runs async through the host and calls onreadystatechange; WinHttp works synchronously") {
    t::FakeHost host;
    host.web["https://api.example/x"] = { 200, "{\"a\":1}", {} };
    JsEngine e(host);
    CHECK(e.load("const x = new ActiveXObject('Microsoft.XMLHTTP');"
                 "x.open('GET', 'https://api.example/x', true); x.setRequestHeader('User-Agent', 'jsp');"
                 "x.onreadystatechange = () => { if (x.readyState === 4) console.log('async', x.status, JSON.parse(x.responseText).a); };"
                 "x.send(); console.log('sent', x.readyState);"
                 "const w = new ActiveXObject('WinHttp.WinHttpRequest.5.1'); w.Open('POST', 'https://api.example/missing', false);"
                 "w.SetTimeouts(1, 2, 3, 4); w.SetRequestHeader('Content-Type', 'text/plain'); w.Send('hello'); w.WaitForResponse();"
                 "console.log('sync', w.Status, w.ResponseText);"
                 "const y = new ActiveXObject('Msxml2.XMLHTTP'); y.open('GET', 'https://api.example/x'); y.onreadystatechange = () => console.log('aborted?'); y.send(); y.abort();", ""));
    CHECK_EQ(host.console_lines[0], std::string("sent 2"));
    CHECK_EQ(host.console_lines[1], std::string("sync 404 not found"));
    host.finish_http();
    CHECK_EQ(host.console_lines.back(), std::string("async 200 1"));
    CHECK_EQ(host.console_lines.size(), (size_t)3);
    CHECK_EQ(host.requests[0], std::string("POST https://api.example/missing [Content-Type: text/plain] body=hello"));
    CHECK_EQ(host.requests[1], std::string("GET https://api.example/x [User-Agent: jsp]"));
}

TEST("ActiveX: htmlfile parses HTML for scrapers: tags, classes, attributes, innerHTML and innerText") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("const doc = new ActiveXObject('htmlfile'); doc.open(); const div = doc.createElement('div');"
                 "div.innerHTML = '<!DOCTYPE html><ul><li class=\"tag\"><a href=\"/tag/rock\">Rock &amp; Roll</a><li class=tag>Pop</ul>"
                 "<div class=\"wiki-content\"><p>One<br>two</p><img src=\"https://x/a.jpg\"><script>if (a < b) {}</script></div>';"
                 "const li = div.getElementsByTagName('li');"
                 "console.log(li.length, li[0].className, li[0].innerText, li[1].innerText, li[0].getElementsByTagName('a')[0].href);"
                 "const w = div.getElementsByTagName('div')[0];"
                 "console.log(w.tagName, JSON.stringify(w.innerText.trim()), w.getElementsByTagName('img')[0].src, w.innerHTML.slice(0, 9), li[0].parentNode.tagName);", ""));
    CHECK_EQ(host.console_lines[0], std::string("2 tag Rock & Roll Pop about:/tag/rock"));
    CHECK_EQ(host.console_lines[1], std::string("DIV \"One\\ntwo\" https://x/a.jpg <p>One<br UL"));
}

TEST("ActiveX: htmlfile innerHTML serialises character references like a browser, keeping &, <, > and quotes escaped") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("const div = new ActiveXObject('htmlfile').createElement('div');"
                 "div.innerHTML = '<p title=\"a&#x22;b\">Creed&#x27;s &#39;hit&#39; &#x26; &lt;b&gt; &#9733; &#xD800; &#x110000;</p>';"
                 "const p = div.getElementsByTagName('p')[0];"
                 "console.log(p.innerHTML); console.log(p.innerText); console.log(div.innerHTML === p.outerHTML ? 'same' : p.outerHTML);", ""));
    const auto& out = host.console_lines;
    CHECK_EQ(e.error(), std::string());
    if (out.size() < 3) return;
    CHECK_EQ(out[out.size() - 3], std::string("Creed's 'hit' &#x26; &lt;b&gt; ★ &#xD800; &#x110000;"));
    CHECK_EQ(out[out.size() - 2], std::string("Creed's 'hit' & <b> ★ &#xD800; &#x110000;"));
    CHECK_EQ(out.back(), std::string("same"));
}

TEST("ActiveX: WScript.Shell.Run of a cscript download VBS fetches the URL into the file") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "jsp_vbs_test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const std::string vbs = (dir / "img.vbs").generic_string(), out = (dir / "art" / "a.jpg").generic_string();
    std::ofstream(vbs) << "Set x = CreateObject(\"MSXML2.XMLHTTP\")\ns.SaveToFile file\n";
    t::FakeHost host;
    host.web["https://img.example/a.jpg"] = { 200, std::string("\xff\xd8\x00jpg", 6), {} };
    JsEngine e(host);
    CHECK(e.load("new ActiveXObject('WScript.Shell').Run('cscript //nologo \"" + vbs + "\" \"https://img.example/a.jpg\" \"" + out + "\"', 0);", ""));
    CHECK(host.runs.empty());
    host.finish_http();
    std::string got;
    {
        std::ifstream in(out, std::ios::binary);
        got.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    CHECK(got == std::string("\xff\xd8\x00jpg", 6));
    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST("ActiveX: WScript.Shell, htmlfile clipboard, UIHacks, ESLyric, Georgia-ReBORN's wizards and unknown objects") {
    t::FakeHost host;
    host.clip = "copied";
    JsEngine e(host);
    CHECK(e.load("const sh = new ActiveXObject('WScript.Shell');"
                 "sh.Run('\"https://last.fm/x\"'); sh.Run('shutdown.exe /h', 0);"
                 "const v = sh.RegRead('HKLM\\\\SOFTWARE\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\\\\CurrentMajorVersionNumber');"
                 "let reg = ''; try { sh.RegRead('HKCU\\\\Nope'); } catch (x) { reg = 'threw'; }"
                 "const answer = sh.Popup('Delete?', 0, 'Biography', 4);"
                 "const doc = new ActiveXObject('htmlfile'), was = doc.parentWindow.clipboardData.getData('Text');"
                 "doc.parentWindow.clipboardData.setData('Text', 'new text');"
                 "const ui = new ActiveXObject('UIHacks'); ui.DisableSizing = true; ui.SetPseudoCaption(0, 0, 10, 10);"
                 "const esl = new ActiveXObject('ESLyric'), panel = esl.GetAll(); panel.SetTextColor(1); panel.SetTextFont('Segoe UI', 12, 0);"
                 "const aw = new ActiveXObject('AudioWizard'), uw = new ActiveXObject('UIWizard'); aw.StartPeakmeterMonitoring(50, 50); uw.FrameStyle = 3;"
                 "let unknown = ''; try { new ActiveXObject('Some.Thing'); } catch (x) { unknown = x.message; }"
                 "let vbs = ''; try { const s = new ActiveXObject('ScriptControl'); s.Language = 'VBScript'; s.Eval('MsgBox(1)'); } catch (x) { vbs = 'no vbs'; }"
                 "console.log(v, reg, answer, was, ui.FrameStyle, ui.DisableSizing, ui.MaxSize.Enabled, typeof panel,"
                 " JSON.parse(aw.GetWaveformDataInfo()).metricsPerChannel, aw.GetWaveformTrackCount(), aw.PeakmeterAdjustedLeftRMS, unknown, vbs);", ""));
    CHECK_EQ(host.console_lines.back(), std::string("10 threw 6 copied 0 true false function 5 0 -100 Automation server can't create object (Some.Thing) no vbs"));
    CHECK(host.runs == (std::vector<std::string>{ "\"https://last.fm/x\"", "shutdown.exe /h" }));
    CHECK_EQ(host.boxes[0], std::string("Biography: Delete? (4)"));
    CHECK_EQ(host.clip, std::string("new text"));
    CHECK(std::find(host.console_lines.begin(), host.console_lines.end(), std::string("WScript.Shell.Run: can't run shutdown.exe /h")) != host.console_lines.end());
}

TEST("globals: setTimeout / setInterval / clear* with extra arguments, new FbMetadbHandleList from a handle, list or array") {
    t::FakeHost host;
    host.libraryTracks = { t::track("/a"), t::track("/b") };
    JsEngine e(host);
    CHECK(e.load("let n = 0; const iv = setInterval((step) => { n += step; if (n >= 4) clearInterval(iv); }, 5, 2);"
                 "const t1 = setTimeout((a, b) => console.log('timeout', a + b), 0, 1, 2); const t2 = setTimeout(() => console.log('never'), 10); clearTimeout(t2);"
                 "const lib = fb.GetLibraryItems();"
                 "console.log(new FbMetadbHandleList().Count, new FbMetadbHandleList(lib.Item(0)).Count, new FbMetadbHandleList(lib).Count,"
                 " new FbMetadbHandleList([lib.Item(0), lib.Item(1), lib.Item(0)]).Count, typeof setTimeout(() => {}, 1));"
                 "function report() { console.log('n', n); }", ""));
    CHECK_EQ(host.console_lines.back(), std::string("0 1 2 3 number"));
    for (int round = 0; round < 3; ++round) {
        const auto pending = host.timers;
        for (const auto& [id, ms] : pending) e.timer(id);
    }
    e.invoke("report");
    CHECK(std::find(host.console_lines.begin(), host.console_lines.end(), std::string("timeout 3")) != host.console_lines.end());
    CHECK(std::find(host.console_lines.begin(), host.console_lines.end(), std::string("never")) == host.console_lines.end());
    CHECK_EQ(host.console_lines.back(), std::string("n 4"));
}

TEST("ActiveX: a host can refuse file writes outside an allowed folder") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "jsp_sandbox_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "inside");
    struct Host : t::FakeHost {
        std::string root;
        bool may_write(const std::string& p) override { return p.rfind(root, 0) == 0; }
    } host;
    host.root = (dir / "inside").string();
    JsEngine e(host);
    const std::string in = js_path(dir / "inside"), out = js_path(dir);
    CHECK(e.load("const fso = new ActiveXObject('Scripting.FileSystemObject');"
                 "fso.CreateTextFile('" + in + "/ok.txt').Write('x');"
                 "const s = fso.CreateTextFile('" + out + "/nope.txt'); s.Write('y');"
                 "console.log(fso.FileExists('" + in + "/ok.txt'), fso.FileExists('" + out + "/nope.txt'), utils.WriteTextFile('" + out + "/n2.txt', 'z'));", ""));
    CHECK_EQ(host.console_lines.back(), std::string("true false false"));
    CHECK(std::any_of(host.console_lines.begin(), host.console_lines.end(), [](const std::string& l) { return l.rfind("blocked a write", 0) == 0; }));
    fs::remove_all(dir);
}

TEST("SMP extras: include is relative to the including file, utils.Version, CreateThemeManager, ShowHtmlDialog") {
    t::FakeHost host;
    host.files["/theme/main.js"] = "include('lib\\\\a.js');";
    host.files["/theme/lib/a.js"] = "include('b.js'); var fromA = fromB + 1;";
    host.files["/theme/lib/b.js"] = "var fromB = 41;";
    host.boxAnswer = 7;
    JsEngine e(host);
    CHECK(e.load("include('/theme/main.js');"
                 "const tm = window.CreateThemeManager('TREEVIEW'); tm.SetPartAndStateId(2, 1);"
                 "const got = [];"
                 "utils.ShowHtmlDialog(window.ID, '<div id=btn_cancel></div>', { data: ['Confirm', 'Delete <b>it</b>?', 'Yes', 'No', (s, v) => got.push(s + ':' + v)] });"
                 "utils.ShowHtmlDialog(window.ID, '<div id=btn_ok></div>', { data: ['Note', 'Done', 'OK', '', (s, v) => got.push(s + ':' + v)] });"
                 "utils.ShowHtmlDialog(window.ID, '<div id=radio_container></div>', { data: ['Pick', 'x', 'Cancel', (s, v) => got.push(s + ':' + v)] });"
                 "console.log(fromA, utils.Version, typeof tm.DrawThemeBackground, got.join(' '));"
                 "function on_paint(gr) { tm.DrawThemeBackground(gr, 0, 0, 12, 12); }", "/theme/panel.js"));
    CHECK_EQ(host.console_lines.back(), std::string("42 1.6.1 function ok:false ok:true cancel:false"));
    CHECK_EQ(host.boxes[0], std::string("Confirm: Delete it? (4)"));
    CHECK_EQ(host.boxes[1], std::string("Note: Done (0)"));
    CHECK_EQ(host.boxes.size(), (size_t)2);
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_op(c, "polygon"));
}

TEST("SMP extras: new FbTitleFormat and a small Intl (DateTimeFormat, NumberFormat, numeric Collator)") {
    t::FakeHost host;
    host.fields["%title%"] = "Song";
    JsEngine e(host);
    CHECK(e.load("const d = new Date(2001, 2, 12);"
                 "console.log(new FbTitleFormat('%title%').Eval(), typeof new FbProfiler('p').Time,"
                 " new Intl.DateTimeFormat('en-GB', {year: 'numeric', month: 'long', day: 'numeric'}).format(d),"
                 " new Intl.DateTimeFormat('en-GB', {year: 'numeric', month: 'long'}).format(d),"
                 " new Intl.DateTimeFormat('en-GB', {year: 'numeric'}).format(d),"
                 " new Intl.NumberFormat().format(1234567.5),"
                 " ['Track 10', 'track 2', 'Track 1'].sort(new Intl.Collator(undefined, {numeric: true}).compare).join('|'));", ""));
    CHECK_EQ(host.console_lines.back(), std::string("Song number 12 March 2001 March 2001 2001 1,234,567.5 Track 1|track 2|Track 10"));
}

TEST("selection: AcquireUiSelectionHolder sets the UI selection or tracking; GetSelection and GetSelections read it") {
    t::FakeHost host;
    host.libraryTracks = { t::track("/a"), t::track("/b") };
    JsEngine e(host);
    CHECK(e.load("const h = fb.AcquireUiSelectionHolder(); const none = fb.GetSelection();"
                 "h.SetSelection(fb.GetLibraryItems()); h.SetPlaylistSelectionTracking(); h.SetPlaylistTracking(); h.Dispose();"
                 "console.log(none, fb.GetSelection().Path, fb.GetSelections().Count);", ""));
    CHECK_EQ(host.console_lines.back(), std::string("null /a 2"));
    CHECK(host.selectionLog == (std::vector<std::string>{ "0:2", "1:0", "2:0", "3:0" }));
}

TEST("handle lists can be read with [i] like in Spider Monkey Panel") {
    t::FakeHost host;
    host.libraryTracks = { t::track("/a"), t::track("/b") };
    JsEngine e(host);
    CHECK(e.load("const l = fb.GetLibraryItems(); let paths = [];"
                 "for (let i = 0; i < l.Count; i++) paths.push(l[i].Path);"
                 "console.log(paths.join(','), l[2], l['1'].Path, typeof l.Item, 0 in l, 5 in l);", ""));
    CHECK_EQ(host.console_lines.back(), std::string("/a,/b undefined /b function true false"));
}

TEST("gr: FillGradRect with alpha or a focus is composited as an image, opaque square gradients stay native") {
    t::FakeHost host;
    JsEngine e(host);
    e.load("function on_paint(gr) { gr.FillGradRect(0, 0, 10, 10, 270, 0x00000000, 0x99000000, 1);"
           " gr.FillGradRect(0, 0, 10, 10, 0, 0xff000000, 0xffffffff, 0.5); gr.FillGradRect(0, 0, 10, 10, 90, 0xff000000, 0xffffffff); }", "");
    t::RecordingCanvas c;
    e.paint(c);
    CHECK_EQ(std::count(c.ops.begin(), c.ops.end(), std::string("image")), (long)2);
    CHECK(has_op(c, "gradient 0,0,10,10 #000000>#ffffff"));
}

TEST("tags: UpdateFileInfoFromJSON takes one object per item or one for all; values, arrays and removals") {
    t::FakeHost host;
    host.libraryTracks = { t::track("/a"), t::track("/b") };
    JsEngine e(host);
    CHECK(e.load("const l = fb.GetLibraryItems();"
                 "l.UpdateFileInfoFromJSON(JSON.stringify([{ rating: 4 }, { rating: '', genre: ['Rock', 'Metal'] }]));"
                 "l.UpdateFileInfoFromJSON(JSON.stringify({ mood: 'happy' }));"
                 "try { l.UpdateFileInfoFromJSON('[{}]'); } catch (x) { console.log(x.name); }", ""));
    const std::vector<std::string> want = { "/a rating=4", "/b rating= genre=Rock|Metal", "/a mood=happy", "/b mood=happy" };
    CHECK(host.tagWrites == want);
    CHECK_EQ(host.console_lines.back(), std::string("SyntaxError"));
}

TEST("titleformat: %rating% falls back to foo_navidrome's NAVIDROME_RATING tag") {
    CHECK_EQ(with_fallback_rating("$if2(%rating%,0)"), std::string("$if2($if2(%rating%,$meta(NAVIDROME_RATING)),0)"));
    CHECK_EQ(with_fallback_rating("%RATING% %title%"), std::string("$if2(%rating%,$meta(NAVIDROME_RATING)) %title%"));
    CHECK_EQ(with_fallback_rating("%ratings% %rating"), std::string("%ratings% %rating"));
}

TEST("Panels UI bridge: panelsui reads and writes skin variables and runs actions") {
    t::FakeHost host;
    host.puiVars["eole.view"] = "0";
    JsEngine e(host);
    CHECK(e.load("panelsui.SetVariable('eole.view', 2); console.log(panelsui.IsAvailable, panelsui.GetVariable('eole.view'),"
                 " panelsui.GetVariable('none', 'def'), panelsui.RunAction('MINIMODE:TOGGLE'));", ""));
    CHECK_EQ(host.console_lines.back(), std::string("true 2 def true"));
    CHECK(host.puiActions == std::vector<std::string>{ "MINIMODE:TOGGLE" });
}

TEST("Panels UI bridge: panelsui publishes values the skin reads with $get") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("panelsui.SetValue('hover', 3); console.log(panelsui.GetValue('hover'), panelsui.GetValue('none', 'def'));", ""));
    CHECK_EQ(host.console_lines.back(), std::string("3 def"));
    CHECK_EQ(host.puiValues["hover"], std::string("3"));
    CHECK(host.puiVars.empty());
}

TEST("Panels UI bridge: panelsui shows, hides, moves and measures skin panels") {
    t::FakeHost host;
    host.puiName = "Cover";
    host.puiRects["Cover"] = pui::gfx::Rect{ 10, 20, 300, 200 };
    host.puiTransparent = true;
    JsEngine e(host);
    CHECK(e.load("const r = panelsui.GetPanelRect(), o = panelsui.GetPanelRect('Other');"
                 "panelsui.ShowPanel('Lyrics', false); panelsui.ShowPanel('Lyrics'); panelsui.TogglePanel('Lyrics');"
                 "panelsui.MovePanel('Lyrics', 1, 2, 30, 40); panelsui.ResetPanel(); "
                 "console.log(panelsui.PanelName, r.X, r.Y, r.Width, r.Height, o, window.IsTransparent, panelsui.Eval('%pui_zoom%'));", ""));
    CHECK_EQ(host.console_lines.back(), std::string("Cover 10 20 300 200 null true 125"));
    const std::vector<std::string> want = { "PANEL:HIDE:Lyrics", "PANEL:SHOW:Lyrics", "PANEL:TOGGLE:Lyrics",
                                            "PANEL:MOVE:Lyrics:1,2,30,40", "PANEL:RESET:Cover" };
    CHECK(host.puiActions == want);
}

TEST("UIHacks drives the main window: state, fullscreen, size limits and the pseudo caption") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("const ui = new ActiveXObject('UIHacks');"
                 "ui.MainWindowState = 2; ui.FullScreen = true; ui.MaxSize.Width = 400; ui.MaxSize.Height = 300; ui.MaxSize.Enabled = true;"
                 "ui.MinSize.Width = 200; ui.MinSize.Enabled = true; ui.MaxSize.Enabled = false; ui.SetPseudoCaption(40, 8, 900, 28);"
                 "console.log(ui.MainWindowState, ui.FullScreen, ui.MaxSize.Width, ui.MinSize.Enabled);", ""));
    CHECK_EQ(host.console_lines.back(), std::string("2 true 400 true"));
    const std::vector<std::string> want = { "state 2", "full 1", "limits 0,0,0,0", "limits 0,0,0,0", "limits 0,0,400,300",
                                            "limits 0,0,400,300", "limits 200,0,400,300", "limits 200,0,0,0",
                                            "caption 40,8,900,28" };
    CHECK(host.windowCalls == want);
}

TEST("UIHacks.FrameStyle hides Panels UI's title bar, shared by every UIHacks object") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("const a = new ActiveXObject('UIHacks'); a.FrameStyle = 3;"
                 "console.log(new ActiveXObject('UIHacks').FrameStyle); a.FrameStyle = 0;", ""));
    CHECK_EQ(host.console_lines.back(), std::string("3"));
    const std::vector<std::string> want = { "WINDOWSTYLE:hidetitlebar", "WINDOWSTYLE:showtitlebar" };
    CHECK(host.puiActions == want);
}

TEST("notify: handles and handle lists inside NotifyOthers' info arrive as real objects") {
    t::FakeHost host;
    host.libraryTracks = { t::track("/a"), t::track("/b") };
    JsEngine sender(host);
    CHECK(sender.load("const l = fb.GetLibraryItems();"
                      "window.NotifyOthers('rating_updated', l.Item(1));"
                      "window.NotifyOthers('mixed', { list: l, nested: [l.Item(0), 3], text: 'x' });", ""));
    CHECK_EQ(host.notes.size(), (size_t)2);
    JsEngine receiver(host);
    receiver.load("function on_notify_data(name, info) {"
                  " if (name === 'rating_updated') console.log(name, info.Path, info.Compare(fb.GetLibraryItems().Item(1)));"
                  " else console.log(name, info.list.Count, info.nested[0].Path, info.nested[1], info.text); }", "");
    receiver.notify_data(host.notes[0].first, host.notes[0].second, host.noteItems[0]);
    CHECK_EQ(host.console_lines.back(), std::string("rating_updated /b true"));
    receiver.notify_data(host.notes[1].first, host.notes[1].second, host.noteItems[1]);
    CHECK_EQ(host.console_lines.back(), std::string("mixed 2 /a 3 x"));
}

TEST("engine: output devices, DSP presets, ReplayGain and always on top") {
    t::FakeHost host;
    host.outputs = { { "Primary", "{A}", "{1}", true }, { "USB DAC", "{A}", "{2}", false } };
    host.dsp = { "Flat", "Loud" };
    host.dspSelected = 0;
    JsEngine e(host);
    CHECK(e.load("const d = JSON.parse(fb.GetOutputDevices());"
                 "fb.SetOutputDevice(d[1].output_id, d[1].device_id);"
                 "const p = JSON.parse(fb.GetDSPPresets()); fb.SetDSPPreset(1);"
                 "fb.ReplaygainMode = 2; fb.AlwaysOnTop = true; fb.ShowLibrarySearchUI('artist IS x');"
                 "console.log(d.length, d[0].name, d[0].active, JSON.parse(fb.GetOutputDevices())[1].active,"
                 " p[0].name, p[0].active, JSON.parse(fb.GetDSPPresets())[1].active, fb.ReplaygainMode, fb.AlwaysOnTop);"
                 "try { fb.ReplaygainMode = 7; } catch (e) { console.log(e.name); }"
                 "try { fb.SetDSPPreset(5); } catch (e) { console.log(e.name); }", ""));
    CHECK(!e.failed());
    CHECK_EQ(host.console_lines[0], std::string("2 Primary true true Flat true true 2 true"));
    CHECK_EQ(host.console_lines[1], std::string("RangeError"));
    CHECK_EQ(host.console_lines[2], std::string("RangeError"));
    CHECK(host.searches == std::vector<std::string>{ "artist IS x" });
}

TEST("engine: playlist locked actions") {
    t::FakeHost host;
    host.playlists = { "A", "B" };
    host.foreignLocks[1] = "foo_other";
    JsEngine e(host);
    CHECK(e.load("plman.SetPlaylistLockedActions(0, ['AddItems', 'RemovePlaylist']);"
                 "console.log(plman.GetPlaylistLockedActions(0).join(','), plman.GetPlaylistLockName(0),"
                 " plman.GetPlaylistLockedActions(1).length, plman.GetPlaylistLockName(1));"
                 "plman.SetPlaylistLockedActions(0, []); console.log(plman.GetPlaylistLockedActions(0).length);"
                 "for (const f of [() => plman.SetPlaylistLockedActions(0, ['Nope']), () => plman.SetPlaylistLockedActions(1, ['AddItems']),"
                 "                 () => plman.GetPlaylistLockedActions(9)])"
                 "  try { f(); } catch (e) { console.log(e.name); }", ""));
    CHECK(!e.failed());
    const std::vector<std::string> want = { "AddItems,RemovePlaylist JS Panel 7 foo_other", "0", "RangeError", "InternalError", "RangeError" };
    CHECK(host.console_lines == want);
}

TEST("engine: state callbacks reach the script") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("function on_always_on_top_changed(s) { console.log('top', s); }"
                 "function on_replaygain_mode_changed(m) { console.log('rg', m); }"
                 "function on_main_menu(i) { console.log('menu', i); }"
                 "function on_selection_changed() { console.log('sel'); }"
                 "function on_output_device_changed() { console.log('out'); }"
                 "function on_dsp_preset_changed() { console.log('dsp'); }", ""));
    e.invoke("on_always_on_top_changed", { true });
    e.invoke("on_replaygain_mode_changed", { 1 });
    e.invoke("on_main_menu", { 3 });
    e.invoke("on_selection_changed");
    e.invoke("on_output_device_changed");
    e.invoke("on_dsp_preset_changed");
    const std::vector<std::string> want = { "top true", "rg 1", "menu 3", "sel", "out", "dsp" };
    CHECK(host.console_lines == want);
}

TEST("api util: DetectCharset code pages") {
    CHECK_EQ(detect_codepage("plain"), 65001);
    CHECK_EQ(detect_codepage("caf\xc3\xa9"), 65001);
    CHECK_EQ(detect_codepage("caf\xe9"), 1252);
    CHECK_EQ(detect_codepage("\xc3"), 1252);
    CHECK_EQ(detect_codepage(std::string("\xff\xfe" "a\0", 4)), 1200);
    CHECK_EQ(detect_codepage("\xef\xbb\xbfx"), 65001);
}

TEST("api util: text files with a byte order mark are read as UTF-8, UTF-16 included") {
    CHECK_EQ(decode_text("\xef\xbb\xbf{}"), std::string("{}"));
    CHECK_EQ(decode_text(std::string("\xff\xfe{\0\xe9\0\x3d\xd8\x00\xde}\0", 12)), std::string("{\xc3\xa9\xf0\x9f\x98\x80}"));
    CHECK_EQ(decode_text(std::string("\xfe\xff\0a\x4e\x2d", 6)), std::string("a\xe4\xb8\xad"));
    CHECK_EQ(decode_text("plain"), std::string("plain"));
}

TEST("engine: utils.MapString and DetectCharset") {
    t::FakeHost host;
    const std::string dir = (std::filesystem::temp_directory_path() / "jsp_charset").generic_string();
    std::filesystem::create_directories(dir);
    { std::ofstream(dir + "/a.txt", std::ios::binary) << "caf\xe9"; }
    JsEngine e(host);
    CHECK(e.load("console.log(utils.MapString('AbC', 0, 0x100), utils.MapString('abc', 0, 0x200),"
                 " utils.MapString('\u30ab\u30ca', 0, 0x100000) === '\u304b\u306a', utils.MapString('A1', 0, 0x800000) === '\uff21\uff11',"
                 " utils.DetectCharset('" + dir + "/a.txt'), utils.DetectCharset('" + dir + "/none.txt'));", ""));
    CHECK(!e.failed());
    CHECK_EQ(host.console_lines.back(), std::string("abc ABC true true 1252 0"));
    std::filesystem::remove_all(dir);
}

TEST("engine: drag callbacks get a DropTargetAction and their changes come back") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("function on_drag_enter(a, x, y, mask) { console.log('enter', a.Effect, a.Playlist, a.Base, a.ToSelect, x, y, mask); }"
                 "function on_drag_over(a) { a.Effect = 0; }"
                 "function on_drag_leave() { console.log('leave'); }"
                 "function on_drag_drop(a, x, y) { a.Effect = 1; a.Playlist = 2; a.Base = y; a.ToSelect = false; a.Text = 'here'; }", ""));
    DropAction a;
    a.effect = 3;
    e.drag(Drag::Enter, 5, 6, 1, a);
    CHECK_EQ(a.effect, 3);
    e.drag(Drag::Over, 5, 6, 1, a);
    CHECK_EQ(a.effect, 0);
    e.drag(Drag::Leave, 0, 0, 0, a);
    DropAction d;
    d.effect = 3;
    e.drag(Drag::Drop, 1, 7, 0, d);
    CHECK(!e.failed());
    CHECK_EQ(d.effect, 1);
    CHECK_EQ(d.playlist, 2);
    CHECK_EQ(d.base, 7);
    CHECK(!d.toSelect);
    CHECK_EQ(d.text, std::string("here"));
    const std::vector<std::string> want = { "enter 3 -1 0 true 5 6 1", "leave" };
    CHECK(host.console_lines == want);
}

TEST("engine: DoDragDrop and the handle clipboard") {
    t::FakeHost host;
    host.libraryTracks = { t::track("/a/1"), t::track("/b/2") };
    host.dragResult = 2;
    JsEngine e(host);
    CHECK(e.load("const lib = fb.GetLibraryItems();"
                 "console.log(fb.DoDragDrop(window.ID, lib, 3, {}), fb.DoDragDrop(window.ID, fb.CreateHandleList(), 1),"
                 " fb.CheckClipboardContents(), fb.CopyHandleListToClipboard(lib), fb.CheckClipboardContents(),"
                 " fb.GetClipboardContents(window.ID).Count);", ""));
    CHECK(!e.failed());
    CHECK_EQ(host.console_lines.back(), std::string("2 0 false true true 2"));
    CHECK(host.drags == (std::vector<std::pair<size_t, int>>{ { 2, 3 } }));
}

TEST("engine: the tooltip shows its text while active") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("const tt = window.CreateTooltip('Segoe UI', 12, 0);"
                 "tt.Text = 'quiet'; tt.Activate(); tt.SetMaxWidth(200); tt.Text = 'Play';"
                 "tt.Deactivate(); tt.Text = 'later';"
                 "console.log(tt.Text, window.CreateTooltip() === tt);", ""));
    CHECK(!e.failed());
    const std::vector<std::string> want = { "quiet on 0", "quiet on 200", "Play on 200", "Play off 200" };
    CHECK(host.tips == want);
    CHECK_EQ(host.console_lines.back(), std::string("later true"));
}

TEST("engine: DUI and CUI colours and fonts come from the host, with a dark fallback") {
    t::FakeHost host;
    {
        JsEngine e(host);
        CHECK(e.load("console.log((window.GetColourDUI(1) >>> 0).toString(16), window.GetFontDUI(5).Name, window.GetFontCUI(0).Size);", ""));
        CHECK_EQ(host.console_lines.back(), std::string("ff1e1e1e Consolas 12"));
    }
    host.themed = true;
    JsEngine e(host);
    CHECK(e.load("const d = [0, 1, 2, 3].map(i => window.GetColourDUI(i) & 0xff).join(',');"
                 "const c = [0, 1, 2, 3, 4, 5, 6].map(i => window.GetColourCUI(i) & 0xff).join(',');"
                 "const f = window.GetFontDUI(3), cf = window.GetFontCUI(0), cl = window.GetFontCUI(1);"
                 "console.log(d, c, f.Name, f.Size, f.Style, cf.Name, cl.Name);", ""));
    CHECK(!e.failed());
    CHECK_EQ(host.console_lines.back(), std::string("0,1,2,3 0,4,0,1,3,5,2 Face3 13 1 Face2 Face0"));
}

TEST("engine: utils.GetSysColour and utils.ColourPicker") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("console.log((utils.GetSysColour(5) >>> 0).toString(16), utils.GetSysColour(99), (utils.ColourPicker(window.ID, 0xff112233) >>> 0).toString(16));", ""));
    host.picked = 0xff445566u;
    CHECK(e.load("console.log((utils.ColourPicker(window.ID, 0xff112233) >>> 0).toString(16));", ""));
    CHECK(!e.failed());
    const std::vector<std::string> want = { "ffabcdef 0 ff112233", "ff445566" };
    CHECK(host.console_lines == want);
}

TEST("engine: ShowProperties hands the host the properties as indented JSON") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("window.SetProperty('a', 1); window.SetProperty('b', 'x'); window.ShowProperties();", ""));
    CHECK(!e.failed());
    CHECK_EQ(host.editedProperties, std::string("{\n  \"a\": 1,\n  \"b\": \"x\"\n}"));
    CHECK_EQ(check_properties("{\"a\": 1}"), std::string());
    CHECK(!check_properties("{a: 1}").empty());
    CHECK(!check_properties("[1]").empty());
}

TEST("api util: layout script paths resolve ~ and the profile folder") {
    CHECK_EQ(resolve_script_path("~/s/a.js", "/Users/me", "/p"), std::string("/Users/me/s/a.js"));
    CHECK_EQ(resolve_script_path("themes/x.js", "/Users/me", "/p/"), std::string("/p/themes/x.js"));
    CHECK_EQ(resolve_script_path("/abs/x.js", "/Users/me", "/p"), std::string("/abs/x.js"));
    CHECK_EQ(resolve_script_path("C:\\x.js", "/h", "/p"), std::string("C:\\x.js"));
    CHECK_EQ(resolve_script_path("", "/h", "/p"), std::string());
}

TEST("samples: every built-in sample loads and paints") {
    for (const Sample& sample : samples()) {
        t::FakeHost host;
        JsEngine e(host);
        CHECK(e.load(sample.source, sample.file));
        e.resize(400, 120);
        t::RecordingCanvas c;
        e.paint(c);
        if (e.failed()) printf("sample %s: %s\n", sample.name, e.error().c_str());
        CHECK(!e.failed());
    }
}

const Sample& sample_named(const std::string& file) {
    for (const Sample& s : samples())
        if (file == s.file) return s;
    return samples().front();
}

TEST("samples: the playlist selects, plays, removes and takes drops") {
    t::FakeHost host;
    host.playlistItems[0] = { t::track("/a/1"), t::track("/a/2"), t::track("/a/3") };
    JsEngine e(host);
    CHECK(e.load(sample_named("playlist.js").source, "playlist.js"));
    e.resize(400, 200);
    t::RecordingCanvas c;
    e.paint(c);
    e.mouse(Mouse::LeftDown, 20, 30, 1);
    e.mouse(Mouse::LeftUp, 20, 30, 0);
    CHECK(host.selection[0] == (std::vector<bool>{ false, true, false }));
    CHECK_EQ(host.focus[0], 1);
    e.mouse(Mouse::LeftDouble, 20, 55, 1);
    CHECK_EQ(host.actions.back(), std::string("play 0 2"));
    DropAction a;
    a.effect = 1;
    e.drag(Drag::Drop, 10, 26, 1, a);
    CHECK_EQ(a.playlist, 0);
    CHECK_EQ(a.base, 1);
    CHECK_EQ(a.effect, 1);
    CHECK(!e.failed());
}

TEST("samples: the seekbar seeks and sets the volume") {
    t::FakeHost host;
    host.playing = true;
    host.length = 200;
    JsEngine e(host);
    CHECK(e.load(sample_named("seekbar.js").source, "seekbar.js"));
    e.resize(400, 30);
    t::RecordingCanvas c;
    e.paint(c);
    const int seekW = 400 - 120 - 120;
    e.mouse(Mouse::LeftDown, 52 + seekW / 2, 15, 1);
    e.mouse(Mouse::LeftUp, 52 + seekW / 2, 15, 0);
    CHECK(host.seeks.size() == 1 && host.seeks[0] > 99 && host.seeks[0] < 101);
    e.mouse(Mouse::LeftDown, 400 - 12, 15, 1);
    CHECK(host.vol > -1);
    CHECK(!e.failed());
}

TEST("engine: utils.CheckFont asks the canvas whether the face exists") {
    t::FakeHost host;
    host.missingFonts = { "Nope Sans" };
    JsEngine e(host);
    CHECK(e.load("console.log(utils.CheckFont('Segoe UI'), utils.CheckFont('Nope Sans'), utils.CheckFont(''));", ""));
    CHECK_EQ(host.console_lines.back(), std::string("true false false"));
}

TEST("jsp3: the preprocessor header lists the imports and marks JScript Panel 3 scripts") {
    const std::string src = "// ==PREPROCESSOR==\n// @name \"x\"\n// @import \"lodash\"\n// @import \"%fb2k_component_path%samples\\js\\panel.js\"\n// ==/PREPROCESSOR==\nvar a = 1;";
    const ScriptHeader h = parse_header(src);
    CHECK(h.present);
    CHECK(h.imports == (std::vector<std::string>{ "lodash", "%fb2k_component_path%samples\\js\\panel.js" }));
    CHECK(is_jsp3_script(src, h));
    CHECK(!is_jsp3_script("function on_paint(gr) { gr.FillSolidRect(0, 0, 1, 1, 0); }", parse_header("")));
    CHECK(is_jsp3_script("function on_paint(gr) { gr.WriteText('a', '', 0, 0, 0, 9, 9); }", parse_header("")));
    CHECK_EQ(resolve_import("%fb2k_component_path%helpers.txt", "/s/", "/jsp3/", "/p/", "/fb/"), std::string("/jsp3/helpers.txt"));
    CHECK_EQ(resolve_import("lib/x.js", "/s/", "/jsp3/", "/p/", "/fb/"), std::string("/s/lib/x.js"));
    CHECK_EQ(resolve_import("%fb2k_profile_path%y.js", "/s/", "/jsp3/", "/p/", "/fb/"), std::string("/p/y.js"));
}

TEST("jsp3: imports load first and the JScript Panel 3 drawing calls map onto the canvas") {
    t::FakeHost host;
    host.files["/jsp3/helpers.txt"] = "function RGB(r, g, b) { return 0xff000000 | (r << 16) | (g << 8) | b; }";
    host.files["/s/lib.js"] = "var shared = 'from lib';";
    JsEngine e(host);
    CHECK(e.load("// ==PREPROCESSOR==\n// @import \"%fb2k_component_path%helpers.txt\"\n// @import \"lib.js\"\n// ==/PREPROCESSOR==\n"
                 "var font = JSON.stringify({ Name: 'Segoe UI', Size: 20, Weight: 700 });\n"
                 "function on_paint(gr) {\n"
                 "  gr.Clear(RGB(1, 2, 3));\n"
                 "  gr.FillRectangle(1, 2, 3, 4, RGB(255, 0, 0));\n"
                 "  gr.FillEllipse(20, 20, 5, 4, RGB(0, 255, 0));\n"
                 "  gr.WriteText('$rgb(255,0,0)Hi ' + shared, font, RGB(0, 0, 255), 0, 0, 100, 30, 2, 2);\n"
                 "}\n"
                 "console.log(window.GetFontDUI(0).indexOf('\"Name\"') >= 0, typeof utils.ListFiles('/nope').toArray(), fb.ComponentPath,"
                 " utils.ReplaceIllegalChars('a:b'), utils.DateStringToTimestamp(utils.TimestampToDateString(1000000000)) === 1000000000);",
                 "/s/main.js"));
    if (e.failed()) printf("%s\n", e.error().c_str());
    CHECK(!e.failed());
    e.resize(100, 40);
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_op(c, "fill_rect 0,0,32767,32767 #010203"));
    CHECK(has_op(c, "fill_rect 1,2,3,4 #ff0000"));
    CHECK(has_op(c, "round_rect 15,16,10,8 10,8 #00ff00"));
    CHECK(has_op(c, "font Segoe UI 20 bold"));
    CHECK(has_prefix(c, "text \"Hi from lib\""));
    CHECK_EQ(host.console_lines.back(), std::string("true object /jsp3/ a∶b true"));
}

TEST("jsp3: a missing component import names the folder to copy JScript Panel 3's files into") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(!e.load("// ==PREPROCESSOR==\n// @import \"%fb2k_component_path%helpers.txt\"\n// ==/PREPROCESSOR==\n", "/s/main.js"));
    CHECK(e.error().find("/jsp3/") != std::string::npos);
}

TEST("wsh: @feature lines and WSH Panel Mod-only names mark a script, JScript Panel 3 and SMP scripts stay as they are") {
    const ScriptHeader h = parse_header("// ==PREPROCESSOR==\n// @feature \"v1.4\"\n// @feature \"dragdrop\"\n// ==/PREPROCESSOR==\n");
    CHECK_EQ(h.features.size(), (size_t)2);
    CHECK_EQ(h.features[1], std::string("dragdrop"));
    CHECK(is_wsh_script("", h));
    CHECK(is_wsh_script("var c = window.GetColorCUI(0);", parse_header("")));
    CHECK(is_wsh_script("var a = utils.Glob('*').toArray();", parse_header("")));
    CHECK(!is_wsh_script("var c = window.GetColourCUI(0);", parse_header("")));
    CHECK_EQ(resolve_import("%fb2k_path%x.js", "/s/", "/c/", "/p/", "/fb/"), std::string("/fb/x.js"));
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("var a = [1].toArray(); function on_paint(gr) { gr.WriteText('a', '', 0, 0, 0, 9, 9); }", "/s/jsp3.js"));
    CHECK_EQ(e.dialect(), std::string("JScript Panel 3"));
    JsEngine smp(host);
    CHECK(smp.load("console.log(plman.FindPlaylist('Default'), plman.FindPlaylist('Nope'), gdi.Font('Segoe UI', 12).Height > 0,"
                   " fb.IsMainMenuCommandChecked('Playback/Stop after current'), fb.IsMainMenuCommandChecked('Playback/Stop'));", "/s/smp.js"));
    CHECK_EQ(host.console_lines.back(), std::string("0 -1 true true false"));
    CHECK(smp.dialect().empty());
    CHECK(smp.missing_api().empty());
}

TEST("wsh: JScript globals, colour and playlist aliases, VBArrays and imports") {
    t::FakeHost host;
    host.files["/profile/lib/common.js"] = "var shared = 'lib';";
    host.playlists = { "Default", "Two" };
    JsEngine e(host);
    CHECK(e.load("// ==PREPROCESSOR==\n// @import \"%fb2k_profile_path%lib/common.js\"\n// @feature \"v1.4\"\n// ==/PREPROCESSOR==\n"
                 "CollectGarbage();\n"
                 "var vb = new VBArray([3, 4]);\n"
                 "var en = new Enumerator([5, 6]), seen = [];\n"
                 "for (; !en.atEnd(); en.moveNext()) seen.push(en.item());\n"
                 "var f = gdi.Font('Segoe UI', 12, 1);\n"
                 "console.log(shared, typeof window.GetColorCUI(0), typeof utils.GetSysColor(5), fb.PlaylistCount, fb.GetPlaylistName(1),"
                 " vb.toArray().join(), vb.ubound(), seen.join(), [7].toArray().length, utils.PathWildcardMatch('*.MP3', 'a.mp3'),"
                 " utils.PathWildcardMatch('a?.txt', 'ab.txt'), utils.PathWildcardMatch('*.txt', 'a.mp3'), f.Height > 0, ScriptEngine());\n",
                 "/s/wsh.js"));
    if (e.failed()) printf("%s\n", e.error().c_str());
    CHECK(!e.failed());
    CHECK_EQ(e.dialect(), std::string("WSH Panel Mod"));
    CHECK_EQ(host.console_lines.back(), std::string("lib number number 2 Two 3,4 1 5,6 1 true true false true JScript"));
    CHECK(std::find(e.source_files().begin(), e.source_files().end(), "/profile/lib/common.js") != e.source_files().end());
}

TEST("wsh: names are found whatever their case, GdiDrawText answers the box and the characters drawn") {
    t::FakeHost host;
    CHECK(is_wsh_script("img.BoxBlur(2); fb.PlayBackOrder = 1;", parse_header("")));
    JsEngine e(host);
    CHECK(e.load("fb.PlayBackOrder = 3;\n"
                 "var f = gdi.font('Segoe UI', 12, 0), r = [];\n"
                 "function on_paint(gr) { r = gr.gdidrawtext('ab', f, 0xffffffff, 0, 0, 50, 40, 0x400 | 0x4 | 0x20).toArray(); }\n"
                 "console.log(plman.PlaybackOrder, fb.playbackorder, typeof fb.NoSuch);",
                 "/s/wsh.js"));
    if (e.failed()) printf("%s\n", e.error().c_str());
    CHECK(!e.failed());
    CHECK_EQ(host.order, 3);
    CHECK_EQ(host.console_lines.back(), std::string("3 3 undefined"));
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(e.load("console.log(r.length, r[0], r[2], r[3] - r[1] > 0 && r[3] - r[1] < 40, r[4]);", "/s/check.js"));
    CHECK_EQ(host.console_lines.back(), std::string("5 0 50 true 2"));
    CHECK(e.missing_api() == std::vector<std::string>{ "fb.NoSuch" });
    JsEngine smp(host);
    CHECK(smp.load("var v = fb.playbackorder;", "/s/smp.js"));
    CHECK(smp.missing_api().empty());
}

TEST("wsh: timer objects call on_timer with their id until killed") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("var t = window.CreateTimerInterval(50), n = 0;\n"
                 "function on_timer(id) { if (id === t.ID) ++n; console.log('tick', n); if (n === 2) window.KillTimer(t); }",
                 "/s/wsh.js"));
    CHECK_EQ(host.timers.size(), (size_t)1);
    const int id = host.timers.begin()->first;
    e.timer(id);
    e.timer(id);
    CHECK_EQ(host.console_lines.back(), std::string("tick 2"));
    CHECK(host.timers.empty());
}

TEST("wsh: v1.0 scripts get the watched handle in on_metadb_changed and the selection in on_selection_changed") {
    t::FakeHost host;
    const jsp::TrackPtr a = t::track("/a"), b = t::track("/b");
    host.uiSelection = { b };
    JsEngine e(host);
    CHECK(e.load("var watched = null;\n"
                 "function on_metadb_changed(h, fromhook) { console.log('changed', h.Path, fromhook); }\n"
                 "function on_selection_changed(h) { console.log('selected', h ? h.Path : 'none'); }\n"
                 "function on_mouse_lbtn_up() { window.WatchMetadb(fb.GetSelection()); }",
                 "/s/wsh.js"));
    e.invoke("on_metadb_changed", { Tracks{ b }, false });
    CHECK(host.console_lines.empty());
    e.invoke("on_mouse_lbtn_up", { 1, 1, 0 });
    e.invoke("on_metadb_changed", { Tracks{ a }, false });
    CHECK(host.console_lines.empty());
    e.invoke("on_metadb_changed", { Tracks{ a, b }, true });
    CHECK_EQ(host.console_lines.back(), std::string("changed /b true"));
    e.invoke("on_selection_changed");
    CHECK_EQ(host.console_lines.back(), std::string("selected /b"));
}

TEST("wsh: style text render draws the outline ring and the text, MeasureString has the lowercase fields") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("var r = gdi.CreateStyleTextRender(), f = gdi.Font('Segoe UI', 12, 0);\n"
                 "r.OutLineText(0xffffffff, 0xff000000, 1);\n"
                 "function on_paint(gr) { r.RenderStringRect(gr, 'Hi', f, 10, 10, 100, 20, 0);\n"
                 "  var m = gr.MeasureString('Hi', f, 0, 0, 100, 20); console.log(m.lines, m.chars, m.x); }\n"
                 "CollectGarbage();",
                 "/s/wsh.js"));
    e.resize(120, 40);
    t::RecordingCanvas c;
    e.paint(c);
    CHECK_EQ(std::count_if(c.ops.begin(), c.ops.end(), [](const std::string& o) { return o.rfind("text \"Hi\"", 0) == 0; }), 9);
    CHECK(std::any_of(c.ops.begin(), c.ops.end(), [](const std::string& o) { return o.rfind("text \"Hi\"", 0) == 0 && o.find("#ffffff") != std::string::npos; }));
    CHECK_EQ(host.console_lines.back(), std::string("1 2 0"));
}

TEST("missing api: a script's reads of names JS Panel lacks are listed once, the preludes' are not") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("var a = fb.NoSuchThing, b = fb.NoSuchThing, c = typeof utils.Nope, d = fb.lowercase, e2 = fb.TitleFormat('%title%').Missing;\n"
                 "var l = fb.CreateHandleList(); var n = l.Count; var g = l[0];\n"
                 "function on_paint(gr) { if (gr.NotThere) {} }",
                 "/s/smp.js"));
    t::RecordingCanvas c;
    e.paint(c);
    const std::vector<std::string> expected = { "fb.NoSuchThing", "utils.Nope", "FbTitleFormat.Missing", "GdiGraphics.NotThere" };
    CHECK(e.missing_api() == expected);
    JsEngine wsh(host);
    CHECK(wsh.load("// ==PREPROCESSOR==\n// @feature \"v1.4\"\n// ==/PREPROCESSOR==\nvar x = 1;", "/s/wsh.js"));
    CHECK(wsh.missing_api().empty());
}

TEST("jsp3: Spider Monkey Panel scripts keep FillEllipse's box arguments") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("function on_paint(gr) { gr.FillEllipse(20, 20, 5, 4, 0xff00ff00); }", "/s/smp.js"));
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(has_op(c, "round_rect 20,20,5,4 5,4 #00ff00"));
}

TEST("jsp3: playlist, title format, hash and request helpers") {
    t::FakeHost host;
    host.playlistItems[0] = { t::track("/a/x"), t::track("/b/y"), t::track("/a/z") };
    JsEngine e(host);
    CHECK(e.load("// ==PREPROCESSOR==\n// ==/PREPROCESSOR==\nwindow.IsDark;\n"
                 "plman.SelectQueryItems(0, '/a/');\n"
                 "plman.InvertSelection(0);\n"
                 "console.log(plman.GetPlaylistSelectedIndexes(0).toArray().join(','), plman.FindPlaylist('Default'), plman.GetPlaylistItemCount(0),"
                 " utils.HashString('abc'), utils.HashString('abc') === utils.HashString('abc'), typeof fb.TitleFormat('%title%').EvalPlaylistItem,"
                 " typeof utils.HTTPRequestAsync(window.ID, 0, 'http://x'), fb.GetAudioChunk());", "/s/m.js"));
    if (e.failed()) printf("%s\n", e.error().c_str());
    CHECK(!e.failed());
    CHECK_EQ(host.console_lines.back(), std::string("1 0 3 e71fa2190541574b true function number null"));
}

TEST("jsp3: lodash 3 names, CreateImage, Dispose and GetOtherInfo") {
    t::FakeHost host;
    host.bundledLodash = "var _ = { mixin(o) { Object.assign(_, o); }, orderBy: () => 'ordered', mapValues: (o, f) => Object.fromEntries(Object.entries(o).map(([k, v]) => [k, f(v)])),"
                         " groupBy: (c, k) => c.reduce((m, x) => ((m[x[k]] = m[x[k]] || []).push(x), m), {}) };";
    host.playlistItems[0] = { t::track("/music/a.mp3") };
    JsEngine e(host);
    CHECK(e.load("// ==PREPROCESSOR==\n// @import \"lodash\"\n// ==/PREPROCESSOR==\n"
                 "const items = plman.GetPlaylistItems(0); const info = JSON.parse(items.GetOtherInfo()); items.Dispose();"
                 "console.log(_.sortByOrder(), Object.keys(_.nest([{ a: 1, b: 2 }, { a: 1, b: 3 }], ['a', 'b'])['1']).join(','),"
                 " utils.CreateImage(3, 2).Width, info.Location['File name'], info.Location['Folder name']);", "/s/m.js"));
    if (e.failed()) printf("%s\n", e.error().c_str());
    CHECK(!e.failed());
    CHECK_EQ(host.console_lines.back(), std::string("ordered 2,3 3 a.mp3 /music"));
    CHECK(e.missing_api().empty());
}

TEST("engine: the preprocessor header names the script") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("// ==PREPROCESSOR==\n// @name \"Header name\"\n// @author \"me\"\n// ==/PREPROCESSOR==\nconsole.log(window.Name, window.ScriptInfo.Author);", "/s/x.js"));
    CHECK_EQ(host.console_lines.back(), std::string("Header name me"));
}

TEST("jsp3: PushLayer clips until PopLayer, and an open layer is closed after on_paint") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("function on_paint(gr) { gr.PushLayer(1, 2, 30, 40); gr.FillRectangle(0, 0, 99, 99, 0xff000000); gr.PopLayer();"
                 " gr.PushLayer(5, 5, 5, 5); gr.WriteText('x', '', 0xff000000, 0, 0, 9, 9); }", "/s/x.js"));
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(!e.failed());
    const auto at = [&](const std::string& op) { return std::find(c.ops.begin(), c.ops.end(), op) - c.ops.begin(); };
    CHECK(at("clip 1,2,30,40") < at("fill_rect 0,0,99,99 #000000"));
    CHECK(at("fill_rect 0,0,99,99 #000000") < at("unclip"));
    CHECK_EQ(std::count(c.ops.begin(), c.ops.end(), std::string("unclip")), 2);
}

TEST("jsp3: FillRectangle draws a linear brush as gradient bands between its stops") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("function on_paint(gr) {"
                 " gr.FillRectangle(0, 0, 10, 100, JSON.stringify({ Start: [0, 0], End: [0, 100], Stops: [[0, 0xff000000], [0.5, 0xffff0000], [1, 0xffffffff]] }));"
                 " gr.FillEllipse(50, 50, 5, 5, JSON.stringify({ Centre: [0, 0], Stops: [[0, 0xff000000], [1, 0xffffffff]] })); }", "/s/x.js"));
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(!e.failed());
    CHECK(has_op(c, "gradient 0,0,10,50 #000000>#ff0000"));
    CHECK(has_op(c, "gradient 0,50,10,50 #ff0000>#ffffff"));
    CHECK(has_op(c, "round_rect 45,45,10,10 10,10 #808080"));
}

TEST("jsp3: $rgb codes and colour ranges colour their runs of single-line text") {
    t::FakeHost host;
    JsEngine e(host);
    CHECK(e.load("function on_paint(gr) {"
                 " gr.WriteText('A$rgb(255,0,0)B$rgb()C', '', 0xff00ff00, 0, 0, 300, 20);"
                 " gr.WriteText('\\x03FF0000\\x03D', '', 0xff00ff00, 0, 30, 300, 20);"
                 " gr.WriteText('EFG', '', JSON.stringify([{ Start: 1, Length: 1, Colour: 0xff0000ff }]), 0, 60, 300, 20); }", "/s/x.js"));
    t::RecordingCanvas c;
    e.paint(c);
    CHECK(!e.failed());
    CHECK(has_prefix(c, "text \"A\" 0,0,"));
    CHECK(std::any_of(c.ops.begin(), c.ops.end(), [](const std::string& o) { return o.rfind("text \"B\"", 0) == 0 && o.find("#ff0000") != std::string::npos; }));
    CHECK(std::any_of(c.ops.begin(), c.ops.end(), [](const std::string& o) { return o.rfind("text \"C\"", 0) == 0 && o.find("#00ff00") != std::string::npos; }));
    CHECK(std::any_of(c.ops.begin(), c.ops.end(), [](const std::string& o) { return o.rfind("text \"D\"", 0) == 0 && o.find("#0000ff") != std::string::npos; }));
    CHECK(std::any_of(c.ops.begin(), c.ops.end(), [](const std::string& o) { return o.rfind("text \"F\"", 0) == 0 && o.find("#0000ff") != std::string::npos; }));
}

TEST("engine: source_files lists the script and everything it includes, for the hot reload") {
    t::FakeHost host;
    host.files["/s/lib/a.js"] = "include('b.js'); var a = 1;";
    host.files["/s/lib/b.js"] = "var b = 2;";
    JsEngine e(host);
    CHECK(e.load("include('lib/a.js'); console.log(a + b);", "/s/main.js"));
    CHECK(!e.failed());
    const std::vector<std::string> want = { "/s/main.js", "/s/lib/a.js", "/s/lib/b.js" };
    CHECK(e.source_files() == want);
}

TEST("editor: error_line reads the syntax check's line and a runtime stack's") {
    CHECK_EQ(error_line(check_syntax("var a = 1;\nvar b = ;\n", "x.js"), ""), 2);
    t::FakeHost host;
    JsEngine e(host);
    CHECK(!e.load("var ok = 1;\n\nfunction boom() { undefinedThing(); }\nboom();\n", "/s/main.js"));
    CHECK_EQ(error_line(e.error(), "/s/main.js"), 3);
    CHECK_EQ(error_line("nothing here", "/s/main.js"), 0);
    const std::string text = "one\ntwo\nthree";
    CHECK_EQ(line_start(text, 1), (std::size_t)0);
    CHECK_EQ(line_start(text, 3), (std::size_t)8);
    CHECK_EQ(line_start(text, 9), text.size());
}

TEST("themes: Eole's files are patched in memory, once, and only Eole's") {
    const std::string common = "switch (x) {\n        case \"fy+h\":\t\t\n        case \"http\":\n        y();\n}";
    std::vector<std::string> applied;
    const std::string once = patch_theme_source("C:\\fb\\themes\\eole\\js\\JStheme_common.js", common, &applied);
    CHECK(once.find("case \"navi\":") != std::string::npos);
    CHECK(applied == std::vector<std::string>{ "js/JStheme_common.js" });
    CHECK_EQ(patch_theme_source("/p/themes/eole/js/JStheme_common.js", once), once);
    CHECK_EQ(patch_theme_source("/p/themes/other/js/JStheme_common.js", common), common);
    const std::string filter = patch_theme_source("/p/themes/eole/js/WSHfilter.js", "var brw = {};");
    CHECK(filter.rfind("var brw = {};", 0) == 0 && filter.find("navidrome.GetArtistInfoAsync") != std::string::npos);
    t::FakeHost host;
    host.files["/p/themes/eole/js/JStheme_common.js"] = "var kind = 'x';\nswitch (kind) {\n        case \"fy+h\":\t\t\n        case \"http\":\n        kind = 'web';\n}";
    JsEngine e(host);
    CHECK(e.load("include('/p/themes/eole/js/JStheme_common.js'); console.log(kind);", "/p/panel.js"));
    CHECK(!e.failed());
}
