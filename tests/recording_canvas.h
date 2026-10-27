#pragma once
#include "gfx/canvas.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace t {

inline std::string col(pui::gfx::Color c) {
    char b[8]; std::snprintf(b, sizeof b, "#%02x%02x%02x", c.r, c.g, c.b); return b;
}
inline std::string rect(const pui::gfx::Rect& r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + "," + std::to_string(r.w) + "," + std::to_string(r.h);
}

class RecordingCanvas : public pui::gfx::Canvas {
public:
    std::vector<std::string> ops;
    int charWidth = 7;
    bool fontAvailable = true;

    int width() const override { return 800; }
    int height() const override { return 600; }
    int dpi() const override { return 96; }
    void fill_rect(const pui::gfx::Rect& r, pui::gfx::Color c) override { ops.push_back("fill_rect " + rect(r) + " " + col(c)); }
    void fill_rect_alpha(const pui::gfx::Rect& r, pui::gfx::Color c, int a) override {
        ops.push_back("fill_rect_alpha " + rect(r) + " " + col(c) + " a" + std::to_string(a));
    }
    void frame_rect(const pui::gfx::Rect& r, pui::gfx::Color c) override { ops.push_back("frame_rect " + rect(r) + " " + col(c)); }
    void fill_round_rect(const pui::gfx::Rect& r, int rx, int ry, pui::gfx::Color c) override {
        ops.push_back("round_rect " + rect(r) + " " + std::to_string(rx) + "," + std::to_string(ry) + " " + col(c));
    }
    void gradient_v(const pui::gfx::Rect& r, pui::gfx::Color a, pui::gfx::Color b) override {
        ops.push_back("gradient " + rect(r) + " " + col(a) + ">" + col(b));
    }
    void line(int, int, int, int, pui::gfx::Color) override { ops.push_back("line"); }
    void fill_polygon(const pui::gfx::PointF*, int, pui::gfx::Color) override { ops.push_back("polygon"); }
    bool set_font(const pui::gfx::FontSpec& s) override {
        ops.push_back("font " + s.face + " " + std::to_string((int)s.size) + (s.bold ? " bold" : "") +
                      (s.italic ? " italic" : "") + (s.underline ? " underline" : ""));
        return fontAvailable;
    }
    void draw_text(std::string_view text, const pui::gfx::Rect& r, unsigned flags, pui::gfx::Color c) override {
        ops.push_back("text \"" + std::string(text) + "\" " + rect(r) + " f" + std::to_string(flags) + " " + col(c));
    }
    int text_width(std::string_view text) override { return (int)text.size() * charWidth; }
    int text_height(std::string_view, int, unsigned) override { return 12; }
    bool text_coverage(std::string_view, const pui::gfx::Rect& r, unsigned, const pui::gfx::Rect& area,
                       std::vector<uint8_t>& mask) override {
        mask.assign((size_t)area.w * area.h, 0);
        for (int y = 0; y < area.h; ++y) for (int x = 0; x < area.w; ++x)
            if (r.contains(area.x + x, area.y + y)) mask[(size_t)y * area.w + x] = 255;
        return true;
    }
    void blend_argb(const pui::gfx::Rect& r, const uint32_t*, int) override { ops.push_back("glow " + rect(r)); }
    void draw_image(const pui::gfx::Image&, const pui::gfx::RectF&, const pui::gfx::RectF&, int, bool,
                    pui::gfx::Interp) override { ops.push_back("image"); }
    pui::gfx::ImagePtr snapshot(const pui::gfx::Rect&) override { return nullptr; }
};


}
