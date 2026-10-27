#include "core/fs_util.h"
#include "core/js_engine.h"
#include "platform/mac/mac_canvas.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct StillTrack : jsp::Track {
    std::string path;
    std::map<std::string, std::string> fields;
};

struct StillHost : jsp::JsHost {
    std::map<std::string, std::string> fields;
    bool playing = false, paused = false;
    double time = 0, length = 0;
    pui::gfx::ImagePtr art;
    jsp::TrackPtr playingTrack = std::make_shared<StillTrack>();
    jsp::Tracks library;
    std::vector<jsp::LibraryAlbum> albums;
    std::map<int, int> timers;
    void repaint() override {}
    void repaint_rect(const pui::gfx::Rect&) override {}
    void set_timer(int id, int ms) override { timers[id] = ms; }
    void kill_timer(int id) override { timers.erase(id); }
    jsp::TrackPtr now_playing() override { return playing ? playingTrack : nullptr; }
    jsp::Tracks library_items() override { return library; }
    std::string track_path(const jsp::TrackPtr& t) override {
        auto* s = dynamic_cast<StillTrack*>(t.get());
        return s ? s->path : std::string();
    }
    std::vector<std::string> format_tracks(const std::string& expr, const jsp::Tracks& tracks) override {
        std::vector<std::string> out;
        for (const auto& t : tracks) {
            auto* s = dynamic_cast<StillTrack*>(t.get());
            auto it = s ? s->fields.find(expr) : decltype(s->fields.end()){};
            out.push_back(s && it != s->fields.end() ? it->second : std::string());
        }
        return out;
    }
    void album_art(const jsp::TrackPtr&, int, jsp::ArtDone done) override { done(art, std::string()); }
    pui::gfx::ImagePtr load_image(const std::string& path) override { return pui::gfx::decode_image_file(path); }
    jsp::LibraryState navidrome_state() override { return albums.empty() ? jsp::LibraryState::Missing : jsp::LibraryState::Ready; }
    void navidrome_albums(jsp::AlbumsDone done) override { done(albums, std::string()); }
    std::string titleformat(const std::string& expr) override {
        auto it = fields.find(expr);
        return it == fields.end() ? std::string() : it->second;
    }
    bool is_playing() override { return playing; }
    bool is_paused() override { return paused; }
    double playback_time() override { return time; }
    double playback_length() override { return length; }
    void playback(jsp::Playback) override {}
    void console(const std::string& text) override { std::fprintf(stderr, "%s\n", text.c_str()); }
    bool read_text_file(const std::string& path, std::string& out) override {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(jsp::fs_path(path), ec)) return false;
        out = jsp::read_file(path);
        return true;
    }
};

int usage() {
    std::fprintf(stderr,
                 "usage: render_script SCRIPT.js OUT.png [--size WxH] [--scale S] [--playing] [--paused]\n"
                 "                     [--time SECONDS] [--length SECONDS] [--field 'EXPR=VALUE']... [--art IMAGE]\n"
                 "                     [--library FILE.tsv] [--navidrome FILE.tsv] [--click X,Y]...\n"
                 "Runs SCRIPT.js in the JS Panel engine (resize, pending timers, clicks, then on_paint) on a\n"
                 "CoreGraphics canvas and writes the frame as a PNG. --field answers fb.TitleFormat(EXPR).Eval();\n"
                 "--art is every album art request's image; --library is the Media Library, a TSV whose first\n"
                 "line holds titleformat expressions and each further line one track's values; --navidrome is\n"
                 "the server's albums, TSV lines id, name, artist, artist id, year, song count.\n");
    return 2;
}

}

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string script = argv[1], out = argv[2];
    int w = 600, h = 56;
    double scale = 1;
    StillHost host;
    std::vector<std::pair<int, int>> clicks;
    auto read_tsv = [](const char* path) {
        std::vector<std::vector<std::string>> rows;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            std::vector<std::string> cells;
            std::stringstream ss(line);
            std::string cell;
            while (std::getline(ss, cell, '\t')) cells.push_back(cell);
            if (!cells.empty()) rows.push_back(cells);
        }
        return rows;
    };
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        if (a == "--size") {
            const char* v = next();
            if (!v || std::sscanf(v, "%dx%d", &w, &h) != 2) return usage();
        } else if (a == "--scale") {
            const char* v = next();
            if (!v) return usage();
            scale = std::atof(v);
        } else if (a == "--playing") host.playing = true;
        else if (a == "--paused") host.paused = true;
        else if (a == "--time") {
            const char* v = next();
            if (!v) return usage();
            host.time = std::atof(v);
        } else if (a == "--length") {
            const char* v = next();
            if (!v) return usage();
            host.length = std::atof(v);
        } else if (a == "--art") {
            const char* v = next();
            if (!v || !(host.art = pui::gfx::decode_image_file(v))) return usage();
        } else if (a == "--library") {
            const char* v = next();
            if (!v) return usage();
            const auto rows = read_tsv(v);
            for (size_t r = 1; r < rows.size(); ++r) {
                auto t = std::make_shared<StillTrack>();
                t->path = "track" + std::to_string(r);
                for (size_t c = 0; c < rows[0].size() && c < rows[r].size(); ++c) t->fields[rows[0][c]] = rows[r][c];
                host.library.push_back(t);
            }
        } else if (a == "--navidrome") {
            const char* v = next();
            if (!v) return usage();
            for (const auto& r : read_tsv(v))
                if (r.size() >= 6) host.albums.push_back({ r[0], r[1], r[2], r[3], "", "", std::atoi(r[4].c_str()), std::atoi(r[5].c_str()) });
        } else if (a == "--click") {
            const char* v = next();
            int x = 0, y = 0;
            if (!v || std::sscanf(v, "%d,%d", &x, &y) != 2) return usage();
            clicks.emplace_back(x, y);
        } else if (a == "--field") {
            const char* v = next();
            const char* eq = v ? std::strchr(v, '=') : nullptr;
            if (!eq) return usage();
            host.fields[std::string(v, eq)] = eq + 1;
        } else return usage();
    }
    std::string source;
    if (!host.read_text_file(script, source)) {
        std::fprintf(stderr, "can't read %s\n", script.c_str());
        return 1;
    }
    jsp::JsEngine engine(host);
    engine.load(source, script);
    engine.resize(w, h);
    for (int round = 0; round < 3 && !host.timers.empty(); ++round) {
        const auto pending = host.timers;
        for (const auto& [id, ms] : pending) engine.timer(id);
    }
    for (const auto& [x, y] : clicks) {
        engine.mouse(jsp::Mouse::LeftDown, x, y, 1);
        engine.mouse(jsp::Mouse::LeftUp, x, y, 0);
    }
    pui::gfx::CGCanvas canvas(w, h, scale);
    engine.paint(canvas);
    if (engine.failed()) {
        std::fprintf(stderr, "script error: %s\n", engine.error().c_str());
        return 1;
    }
    CGImageRef img = canvas.copy_image();
    pui::gfx::ImagePtr image = pui::gfx::image_from_cgimage(img, scale);
    if (!image || !pui::gfx::encode_png_file(*image, out, (int)(w * scale))) {
        std::fprintf(stderr, "can't write %s\n", out.c_str());
        return 1;
    }
    std::printf("%s\n", out.c_str());
    return 0;
}
