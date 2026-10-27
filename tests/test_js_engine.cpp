#include "test.h"
#include "fake_host.h"
#include "recording_canvas.h"
#include "core/js_api_util.h"
#include "core/sample_script.h"
#include <algorithm>
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
    CHECK(has_op(c, "line"));
    CHECK(has_op(c, "polygon"));
    CHECK(has_op(c, "round_rect 0,0,10,8 5,4 #00ff00"));
    CHECK(has_op(c, "round_rect 0,0,10,10 2,3 #0000ff"));
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
