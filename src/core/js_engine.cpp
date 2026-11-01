#include "js_engine.h"
#include "js_api_util.h"
#include "fs_util.h"
#include "image_ops.h"
#include "quickjs.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iterator>
#include <type_traits>
#include <map>
#include <set>
#include <vector>

namespace jsp {

using pui::gfx::Canvas;
using pui::gfx::Color;
using pui::gfx::FontSpec;
using pui::gfx::Rect;

namespace {

long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

JSClassID g_grClass = 0, g_fontClass = 0, g_tfClass = 0, g_handleClass = 0, g_listClass = 0, g_bitmapClass = 0, g_infoClass = 0,
          g_menuClass = 0, g_menuManagerClass = 0;
int g_nextWindowId = 1;
struct Bitmap {
    pui::gfx::ImagePtr image;
    std::shared_ptr<pui::gfx::Pixels> px;
};
using BitmapPtr = std::shared_ptr<Bitmap>;
BitmapPtr* bitmap_of(JSValueConst v);

struct Graphics {
    Canvas* canvas = nullptr;
    std::unique_ptr<Canvas> own;
    BitmapPtr target;
};
const pui::gfx::ImagePtr& drawable(JSContext* ctx, Bitmap& b);

constexpr std::size_t kSyntaxCheckMemory = 64u * 1024 * 1024;
constexpr Color kBackground(30, 30, 30);
constexpr Color kErrorBackground(96, 24, 24);
constexpr Color kErrorText(255, 230, 230);

}

std::string script_dir_of(const std::string& path) {
    const auto cut = path.find_last_of("/\\");
    return cut == std::string::npos ? std::string() : path.substr(0, cut + 1);
}

struct JsEngine::Impl {
    struct Timer {
        JSValue fn;
        bool repeat;
    };

    JsHost& host;
    int budgetMs;
    JSRuntime* rt = nullptr;
    JSContext* ctx = nullptr;
    std::string error;
    std::string dir;
    int w = 0, h = 0;
    int nextTimer = 1;
    std::map<int, Timer> timers;
    long long deadline = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    struct Pending {
        JSValue resolve, reject;
        bool artists;
    };
    std::map<int, Pending> pending;
    int nextPending = 1;
    int windowId = g_nextWindowId++;
    JSValue props = JS_UNDEFINED;
    bool propsDirty = false;
    std::string scriptName, scriptAuthor, scriptVersion;
    int dlgCode = 0, minW = 0, minH = 0, maxW = 0, maxH = 0;

    Impl(JsHost& hst, int budget);
    ~Impl();

    void install_api();
    bool eval(const std::string& source, const std::string& filename);
    bool call(const char* name, int argc, JSValue* argv, bool* truthy = nullptr);
    void report_exception();
    void fail(const std::string& msg);
    void free_timers();
    int add_timer(JSValueConst fn, int ms, bool repeat);
    void clear_timer(int id);
    void drain_jobs();
    JSValue request_albums(bool artists);
    void settle(int id, const std::vector<LibraryAlbum>& albums, const std::string& error);
    void free_pending();
    void art_done(const TrackPtr& track, int artId, pui::gfx::ImagePtr image, const std::string& path);
    JSValueConst properties();
    void flush_properties();

    std::map<int, std::pair<JSValue, JSValue>> promises;
    int nextTask = 1;
    JSValue new_promise(int& id);
    void settle_promise(int id, JSValue value, bool ok = true);
    void image_loaded(int task, pui::gfx::ImagePtr image, const std::string& path);

    std::vector<std::string> includeDirs;
    int modalDepth = 0;
    struct Budget {
        Impl& e;
        long long saved;
        explicit Budget(Impl& impl) : e(impl), saved(impl.deadline) { e.deadline = now_ms() + e.budgetMs; }
        ~Budget() { e.deadline = saved; }
    };
    template <typename F>
    auto modal(F&& f) {
        ++modalDepth;
        const long long t0 = now_ms();
        auto r = f();
        --modalDepth;
        if (deadline) deadline += now_ms() - t0;
        return r;
    }
};

namespace {

JsEngine::Impl* I(JSContext* ctx) { return static_cast<JsEngine::Impl*>(JS_GetContextOpaque(ctx)); }

std::string to_string(JSContext* ctx, JSValueConst v) {
    size_t len = 0;
    const char* s = JS_ToCStringLen2(ctx, &len, v, false);
    if (!s) return {};
    std::string out(s, len);
    JS_FreeCString(ctx, s);
    return out;
}

JSValue new_string(JSContext* ctx, const std::string& s) { return JS_NewStringLen(ctx, s.data(), s.size()); }

void set_str(JSContext* ctx, JSValueConst obj, const char* k, const std::string& v) { JS_SetPropertyStr(ctx, obj, k, new_string(ctx, v)); }

void set_int(JSContext* ctx, JSValueConst obj, const char* k, int v) { JS_SetPropertyStr(ctx, obj, k, JS_NewInt32(ctx, v)); }

std::string arg_str(JSContext* ctx, int argc, JSValueConst* argv, int i) {
    return i < argc ? to_string(ctx, argv[i]) : std::string();
}

double arg_num(JSContext* ctx, int argc, JSValueConst* argv, int i, double def = 0) {
    double d = def;
    if (i < argc && JS_ToFloat64(ctx, &d, argv[i]) != 0) return def;
    return std::isfinite(d) ? d : def;
}

int arg_int(JSContext* ctx, int argc, JSValueConst* argv, int i, int def = 0) {
    return (int)std::lround(arg_num(ctx, argc, argv, i, def));
}

uint32_t arg_u32(JSContext* ctx, int argc, JSValueConst* argv, int i, uint32_t def = 0) {
    uint32_t v = def;
    if (i < argc && JS_ToUint32(ctx, &v, argv[i]) != 0) return def;
    return v;
}

bool arg_bool(JSContext* ctx, int argc, JSValueConst* argv, int i, bool def = false) {
    return i < argc && !JS_IsUndefined(argv[i]) ? JS_ToBool(ctx, argv[i]) > 0 : def;
}

std::vector<int> arg_ints(JSContext* ctx, int argc, JSValueConst* argv, int i) {
    std::vector<int> out;
    int64_t n = 0;
    if (i >= argc || !JS_IsArray(argv[i]) || JS_GetLength(ctx, argv[i], &n) != 0) return out;
    for (int64_t k = 0; k < n; ++k) {
        JSValue v = JS_GetPropertyUint32(ctx, argv[i], (uint32_t)k);
        int32_t x = 0;
        if (JS_ToInt32(ctx, &x, v) == 0) out.push_back(x);
        JS_FreeValue(ctx, v);
    }
    return out;
}

std::string fix_path(std::string p) {
#ifndef _WIN32
    std::replace(p.begin(), p.end(), '\\', '/');
#endif
    return p;
}

bool writable(JSContext* ctx, int argc, JSValueConst* argv, int i) {
    const std::string path = fix_path(arg_str(ctx, argc, argv, i));
    if (I(ctx)->host.may_write(path)) return true;
    I(ctx)->host.console("blocked a write outside the allowed folder: " + path);
    return false;
}

std::string script_relative(JsEngine::Impl* e, const std::string& in) {
    std::string path = fix_path(in);
    const bool absolute = !path.empty() && (path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos);
    return absolute ? path : (e->includeDirs.empty() ? e->dir : e->includeDirs.back()) + path;
}

Rect arg_rect(JSContext* ctx, int argc, JSValueConst* argv, int i) {
    return Rect{ arg_int(ctx, argc, argv, i), arg_int(ctx, argc, argv, i + 1), arg_int(ctx, argc, argv, i + 2),
                 arg_int(ctx, argc, argv, i + 3) };
}

Graphics* graphics_of(JSValueConst v) { return static_cast<Graphics*>(JS_GetOpaque(v, g_grClass)); }

Canvas* canvas_or_throw(JSContext* ctx, JSValueConst self) {
    Graphics* g = graphics_of(self);
    Canvas* c = g ? g->canvas : nullptr;
    if (!c) JS_ThrowTypeError(ctx, "gr can only be used inside on_paint or between GetGraphics and ReleaseGraphics");
    return c;
}

FontSpec* font_arg(JSContext* ctx, int argc, JSValueConst* argv, int i) {
    auto* f = i < argc ? static_cast<FontSpec*>(JS_GetOpaque(argv[i], g_fontClass)) : nullptr;
    if (!f) JS_ThrowTypeError(ctx, "argument %d is not a gdi.Font", i + 1);
    return f;
}

void fill(Canvas& c, const Rect& r, uint32_t argb) {
    const int a = argb_alpha(argb);
    if (a == 0 || r.empty()) return;
    if (a == 255) c.fill_rect(r, argb_color(argb));
    else c.fill_rect_alpha(r, argb_color(argb), a);
}

Color mix(Color a, Color b, double t) {
    auto m = [t](int x, int y) { return (int)std::lround(x + (y - x) * t); };
    return Color(m(a.r, b.r), m(a.g, b.g), m(a.b, b.b));
}

JSValue gr_FillSolidRect(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    fill(*c, arg_rect(ctx, argc, argv, 0), arg_u32(ctx, argc, argv, 4));
    return JS_UNDEFINED;
}

Rect intersect(const Rect& a, const Rect& b) {
    const int l = std::max(a.x, b.x), t = std::max(a.y, b.y), r = std::min(a.right(), b.right()), bt = std::min(a.bottom(), b.bottom());
    return Rect{ l, t, std::max(0, r - l), std::max(0, bt - t) };
}

template <typename F>
void with_alpha(JSContext* ctx, Canvas& c, uint32_t argb, Rect box, F&& draw) {
    const int a = argb_alpha(argb);
    if (a == 0) return;
    if (a == 255) {
        draw(c, 0.f, 0.f);
        return;
    }
    box = intersect(box, Rect{ 0, 0, c.width(), c.height() });
    if (box.empty()) return;
    JsHost& host = I(ctx)->host;
    const double s = c.device_scale() > 0 ? c.device_scale() : 1.0;
    pui::gfx::Pixels px = img::blank((int)std::lround(box.w * s), (int)std::lround(box.h * s));
    {
        std::unique_ptr<Canvas> off = host.pixels_canvas(px, s);
        if (!off) {
            draw(c, 0.f, 0.f);
            return;
        }
        draw(*off, (float)-box.x, (float)-box.y);
    }
    if (auto image = host.image_from_pixels(px))
        c.draw_image(*image, pui::gfx::RectF{ (float)box.x, (float)box.y, (float)box.w, (float)box.h },
                     pui::gfx::RectF{ 0, 0, (float)px.w, (float)px.h }, a, false, pui::gfx::Interp::Bilinear);
}

Rect bounds(const std::vector<pui::gfx::PointF>& pts, double pad) {
    if (pts.empty()) return {};
    float l = pts[0].x, t = pts[0].y, r = l, b = t;
    for (const auto& p : pts) {
        l = std::min(l, p.x);
        t = std::min(t, p.y);
        r = std::max(r, p.x);
        b = std::max(b, p.y);
    }
    const int x = (int)std::floor(l - pad), y = (int)std::floor(t - pad);
    return Rect{ x, y, (int)std::ceil(r + pad) - x + 1, (int)std::ceil(b + pad) - y + 1 };
}

std::vector<pui::gfx::PointF> shifted(std::vector<pui::gfx::PointF> pts, float dx, float dy) {
    for (auto& p : pts) {
        p.x += dx;
        p.y += dy;
    }
    return pts;
}

std::vector<pui::gfx::PointF> arg_points(JSContext* ctx, int argc, JSValueConst* argv, int i) {
    std::vector<pui::gfx::PointF> pts;
    std::vector<double> flat;
    int64_t n = 0;
    if (i >= argc || !JS_IsArray(argv[i]) || JS_GetLength(ctx, argv[i], &n) != 0) return pts;
    for (int64_t k = 0; k < n; ++k) {
        JSValue v = JS_GetPropertyUint32(ctx, argv[i], (uint32_t)k);
        if (JS_IsArray(v)) {
            JSValue x = JS_GetPropertyUint32(ctx, v, 0), y = JS_GetPropertyUint32(ctx, v, 1);
            double dx = 0, dy = 0;
            JS_ToFloat64(ctx, &dx, x);
            JS_ToFloat64(ctx, &dy, y);
            flat.push_back(dx);
            flat.push_back(dy);
            JS_FreeValue(ctx, x);
            JS_FreeValue(ctx, y);
        } else {
            double d = 0;
            JS_ToFloat64(ctx, &d, v);
            flat.push_back(d);
        }
        JS_FreeValue(ctx, v);
    }
    for (size_t k = 0; k + 1 < flat.size(); k += 2) pts.push_back({ (float)flat[k], (float)flat[k + 1] });
    return pts;
}

void stroke(Canvas& c, const std::vector<pui::gfx::PointF>& pts, bool closed, double lw, Color col) {
    const size_t n = pts.size();
    if (n < 2) return;
    const size_t segs = closed ? n : n - 1;
    for (size_t i = 0; i < segs; ++i) {
        const pui::gfx::PointF a = pts[i], b = pts[(i + 1) % n];
        const double len = std::hypot(b.x - a.x, b.y - a.y);
        if (len == 0) continue;
        const double w = std::max(1.0, lw);
        const float nx = (float)(-(b.y - a.y) / len * w / 2), ny = (float)((b.x - a.x) / len * w / 2);
        const pui::gfx::PointF quad[4] = { { a.x + nx, a.y + ny }, { b.x + nx, b.y + ny }, { b.x - nx, b.y - ny }, { a.x - nx, a.y - ny } };
        c.fill_polygon(quad, 4, col);
    }
    if (lw > 2) {
        const int d = (int)std::lround(lw);
        for (size_t i = closed ? 0 : 1; i < (closed ? n : n - 1); ++i)
            c.fill_round_rect(Rect{ (int)std::lround(pts[i].x - lw / 2), (int)std::lround(pts[i].y - lw / 2), d, d }, d, d, col);
    }
}

std::vector<pui::gfx::PointF> ellipse_points(double x, double y, double w, double h) {
    std::vector<pui::gfx::PointF> pts;
    const int n = std::clamp((int)((w + h) / 2), 16, 128);
    for (int i = 0; i < n; ++i) {
        const double t = 2 * 3.14159265358979 * i / n;
        pts.push_back({ (float)(x + w / 2 + w / 2 * std::cos(t)), (float)(y + h / 2 + h / 2 * std::sin(t)) });
    }
    return pts;
}

std::vector<pui::gfx::PointF> round_rect_points(double x, double y, double w, double h, double rx, double ry) {
    rx = std::clamp(rx, 0.0, w / 2);
    ry = std::clamp(ry, 0.0, h / 2);
    std::vector<pui::gfx::PointF> pts;
    const double cx[4] = { x + w - rx, x + w - rx, x + rx, x + rx }, cy[4] = { y + ry, y + h - ry, y + h - ry, y + ry };
    for (int q = 0; q < 4; ++q)
        for (int i = 0; i <= 8; ++i) {
            const double t = 3.14159265358979 / 2 * (q - 1 + i / 8.0);
            pts.push_back({ (float)(cx[q] + rx * std::cos(t)), (float)(cy[q] + ry * std::sin(t)) });
        }
    return pts;
}

template <typename Points>
JSValue stroke_shape(JSContext* ctx, JSValueConst self, Points&& pts, bool closed, double lw, uint32_t argb) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    with_alpha(ctx, *c, argb, bounds(pts, lw / 2 + 1), [&](Canvas& cv, float dx, float dy) {
        stroke(cv, shifted(pts, dx, dy), closed, lw, argb_color(argb));
    });
    return JS_UNDEFINED;
}

JSValue gr_DrawRect(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    const Rect r0 = arg_rect(ctx, argc, argv, 0);
    const int lw = std::max(1, arg_int(ctx, argc, argv, 4, 1));
    const uint32_t argb = arg_u32(ctx, argc, argv, 5);
    with_alpha(ctx, *c, argb, Rect{ r0.x, r0.y, r0.w + 1, r0.h + 1 }, [&](Canvas& cv, float dx, float dy) {
        Rect r{ r0.x + (int)dx, r0.y + (int)dy, r0.w + 1, r0.h + 1 };
        for (int i = 0; i < lw && !r.empty(); ++i) {
            cv.frame_rect(r, argb_color(argb));
            r = Rect{ r.x + 1, r.y + 1, r.w - 2, r.h - 2 };
        }
    });
    return JS_UNDEFINED;
}

JSValue gr_DrawLine(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    const std::vector<pui::gfx::PointF> pts = { { (float)arg_num(ctx, argc, argv, 0), (float)arg_num(ctx, argc, argv, 1) },
                                                { (float)arg_num(ctx, argc, argv, 2), (float)arg_num(ctx, argc, argv, 3) } };
    return stroke_shape(ctx, self, pts, false, arg_num(ctx, argc, argv, 4, 1), arg_u32(ctx, argc, argv, 5));
}

JSValue fill_round(JSContext* ctx, JSValueConst self, const Rect& r, int dw, int dh, uint32_t argb) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    if (r.empty()) return JS_UNDEFINED;
    with_alpha(ctx, *c, argb, r, [&](Canvas& cv, float dx, float dy) {
        cv.fill_round_rect(Rect{ r.x + (int)dx, r.y + (int)dy, r.w, r.h }, dw, dh, argb_color(argb));
    });
    return JS_UNDEFINED;
}

JSValue gr_FillRoundRect(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    const Rect r = arg_rect(ctx, argc, argv, 0);
    return fill_round(ctx, self, r, std::min(r.w, 2 * arg_int(ctx, argc, argv, 4)), std::min(r.h, 2 * arg_int(ctx, argc, argv, 5)),
                      arg_u32(ctx, argc, argv, 6));
}

JSValue gr_FillEllipse(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    const Rect r = arg_rect(ctx, argc, argv, 0);
    return fill_round(ctx, self, r, r.w, r.h, arg_u32(ctx, argc, argv, 4));
}

JSValue gr_DrawEllipse(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    return stroke_shape(ctx, self, ellipse_points(arg_num(ctx, argc, argv, 0), arg_num(ctx, argc, argv, 1), arg_num(ctx, argc, argv, 2), arg_num(ctx, argc, argv, 3)),
                        true, arg_num(ctx, argc, argv, 4, 1), arg_u32(ctx, argc, argv, 5));
}

JSValue gr_DrawRoundRect(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    return stroke_shape(ctx, self,
                        round_rect_points(arg_num(ctx, argc, argv, 0), arg_num(ctx, argc, argv, 1), arg_num(ctx, argc, argv, 2),
                                          arg_num(ctx, argc, argv, 3), arg_num(ctx, argc, argv, 4), arg_num(ctx, argc, argv, 5)),
                        true, arg_num(ctx, argc, argv, 6, 1), arg_u32(ctx, argc, argv, 7));
}

JSValue gr_DrawPolygon(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    return stroke_shape(ctx, self, arg_points(ctx, argc, argv, 2), true, arg_num(ctx, argc, argv, 1, 1), arg_u32(ctx, argc, argv, 0));
}

JSValue gr_FillPolygon(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    const auto pts = arg_points(ctx, argc, argv, 2);
    const uint32_t argb = arg_u32(ctx, argc, argv, 0);
    if (pts.size() >= 3)
        with_alpha(ctx, *c, argb, bounds(pts, 1), [&](Canvas& cv, float dx, float dy) {
            const auto moved = shifted(pts, dx, dy);
            cv.fill_polygon(moved.data(), (int)moved.size(), argb_color(argb));
        });
    return JS_UNDEFINED;
}

JSValue gr_FillGradRect(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    const Rect r = arg_rect(ctx, argc, argv, 0);
    if (r.empty()) return JS_UNDEFINED;
    const double angle = std::fmod(std::fmod(arg_num(ctx, argc, argv, 4), 360) + 360, 360);
    const uint32_t from = arg_u32(ctx, argc, argv, 5), to = arg_u32(ctx, argc, argv, 6);
    const double focus = arg_num(ctx, argc, argv, 7, 1.0);
    const bool square = std::fmod(angle, 90.0) == 0;
    if (argb_alpha(from) < 255 || argb_alpha(to) < 255 || focus < 1 || !square) {
        if (argb_alpha(from) == 0 && argb_alpha(to) == 0) return JS_UNDEFINED;
        const Rect box = intersect(r, Rect{ 0, 0, c->width(), c->height() });
        if (box.empty()) return JS_UNDEFINED;
        pui::gfx::Pixels px = img::gradient(r.w, r.h, angle, from, to, focus);
        pui::gfx::Pixels part = img::crop(px, box.x - r.x, box.y - r.y, box.w, box.h);
        if (auto image = I(ctx)->host.image_from_pixels(part))
            c->draw_image(*image, pui::gfx::RectF{ (float)box.x, (float)box.y, (float)box.w, (float)box.h },
                          pui::gfx::RectF{ 0, 0, (float)box.w, (float)box.h }, 255, false, pui::gfx::Interp::Bilinear);
        return JS_UNDEFINED;
    }
    Color a = argb_color(from), b = argb_color(to);
    if (angle >= 45 && angle < 135) c->gradient_v(r, a, b);
    else if (angle >= 225 && angle < 315) c->gradient_v(r, b, a);
    else {
        if (angle >= 135 && angle < 225) std::swap(a, b);
        for (int x = 0; x < r.w; ++x)
            c->fill_rect(Rect{ r.x + x, r.y, 1, r.h }, mix(a, b, r.w > 1 ? (double)x / (r.w - 1) : 0));
    }
    return JS_UNDEFINED;
}

JSValue draw_text(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv, unsigned flags, bool alpha,
                  uint32_t dt = 0) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    FontSpec* f = font_arg(ctx, argc, argv, 1);
    if (!f) return JS_EXCEPTION;
    const std::string text = arg_str(ctx, argc, argv, 0);
    Rect r = arg_rect(ctx, argc, argv, 3);
    if (dt & (DT_CALCRECT_ | DT_BOTTOM_)) {
        c->set_font(*f);
        const Rect adjusted = dt_text_rect(r, dt, c->text_height(text, r.w, flags & ~pui::gfx::kVCenter));
        if (adjusted.y != r.y || adjusted.h != r.h) flags &= ~pui::gfx::kVCenter;
        r = adjusted;
    }
    const uint32_t argb = alpha ? arg_u32(ctx, argc, argv, 2) : (arg_u32(ctx, argc, argv, 2) | 0xFF000000u);
    with_alpha(ctx, *c, argb, r, [&](Canvas& cv, float dx, float dy) {
        cv.set_font(*f);
        cv.draw_text(text, Rect{ r.x + (int)dx, r.y + (int)dy, r.w, r.h }, flags, argb_color(argb));
    });
    return JS_UNDEFINED;
}

JSValue gr_DrawString(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    return draw_text(ctx, self, argc, argv, string_format_to_text_flags(arg_u32(ctx, argc, argv, 7)), true);
}

JSValue gr_GdiDrawText(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    const uint32_t dt = arg_u32(ctx, argc, argv, 7);
    return draw_text(ctx, self, argc, argv, dt_to_text_flags(dt), false, dt);
}

JSValue gr_CalcTextWidth(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    FontSpec* f = font_arg(ctx, argc, argv, 1);
    if (!f) return JS_EXCEPTION;
    c->set_font(*f);
    return JS_NewInt32(ctx, c->text_width(arg_str(ctx, argc, argv, 0)));
}

JSValue gr_CalcTextHeight(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    FontSpec* f = font_arg(ctx, argc, argv, 1);
    if (!f) return JS_EXCEPTION;
    c->set_font(*f);
    return JS_NewInt32(ctx, c->text_height(arg_str(ctx, argc, argv, 0), 1 << 20, pui::gfx::kSingleLine));
}

JSValue draw_bitmap(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv, int alphaArg) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    auto* b = argc > 0 ? bitmap_of(argv[0]) : nullptr;
    if (!b) return JS_ThrowTypeError(ctx, "argument 1 is not a GdiBitmap");
    if (!*b) return JS_UNDEFINED;
    const pui::gfx::ImagePtr& ip = drawable(ctx, **b);
    if (!ip) return JS_UNDEFINED;
    const pui::gfx::Image& img = *ip;
    auto f = [&](int i, double def) { return (float)arg_num(ctx, argc, argv, i, def); };
    const pui::gfx::RectF dst{ f(1, 0), f(2, 0), f(3, (float)img.width()), f(4, (float)img.height()) };
    const pui::gfx::RectF src{ f(5, 0), f(6, 0), f(7, (float)img.width()), f(8, (float)img.height()) };
    const int alpha = std::clamp(arg_int(ctx, argc, argv, alphaArg, 255), 0, 255);
    if (dst.w > 0 && dst.h > 0 && src.w > 0 && src.h > 0 && alpha > 0) c->draw_image(img, dst, src, alpha, false, pui::gfx::Interp::High);
    return JS_UNDEFINED;
}

JSValue gr_DrawImage(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) { return draw_bitmap(ctx, self, argc, argv, 10); }
JSValue gr_GdiDrawBitmap(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) { return draw_bitmap(ctx, self, argc, argv, 99); }
JSValue gr_GdiAlphaBlend(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) { return draw_bitmap(ctx, self, argc, argv, 9); }

JSValue gr_noop(JSContext*, JSValueConst, int, JSValueConst*) { return JS_UNDEFINED; }

std::vector<std::pair<std::string, int>> wrap_lines(Canvas& c, const std::string& text, int maxWidth) {
    std::vector<std::pair<std::string, int>> out;
    size_t start = 0;
    while (start <= text.size()) {
        const size_t nl = text.find('\n', start);
        std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        if (!para.empty() && para.back() == '\r') para.pop_back();
        std::string line;
        size_t i = 0;
        while (i <= para.size()) {
            size_t sp = para.find(' ', i);
            if (sp == std::string::npos) sp = para.size();
            const std::string word = para.substr(i, sp - i);
            const std::string cand = line.empty() ? word : line + " " + word;
            if (!line.empty() && c.text_width(cand) > maxWidth) {
                out.emplace_back(line, c.text_width(line));
                line = word;
            } else {
                line = cand;
            }
            i = sp + 1;
        }
        out.emplace_back(line, c.text_width(line));
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return out;
}

JSValue gr_EstimateLineWrap(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    FontSpec* f = font_arg(ctx, argc, argv, 1);
    if (!f) return JS_EXCEPTION;
    c->set_font(*f);
    const auto lines = wrap_lines(*c, arg_str(ctx, argc, argv, 0), std::max(1, arg_int(ctx, argc, argv, 2, 1)));
    JSValue arr = JS_NewArray(ctx);
    uint32_t k = 0;
    for (const auto& [text, width] : lines) {
        JS_SetPropertyUint32(ctx, arr, k++, new_string(ctx, text));
        JS_SetPropertyUint32(ctx, arr, k++, JS_NewInt32(ctx, width));
    }
    return arr;
}

JSValue gr_MeasureString(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx, self);
    if (!c) return JS_EXCEPTION;
    FontSpec* f = font_arg(ctx, argc, argv, 1);
    if (!f) return JS_EXCEPTION;
    c->set_font(*f);
    const std::string text = arg_str(ctx, argc, argv, 0);
    const double x = arg_num(ctx, argc, argv, 2), y = arg_num(ctx, argc, argv, 3);
    const int w = std::max(1, arg_int(ctx, argc, argv, 4, 1 << 20));
    const auto lines = wrap_lines(*c, text, w);
    const int lineH = c->text_height("Ag", 1 << 20, pui::gfx::kSingleLine);
    int width = 0;
    for (const auto& l : lines) width = std::max(width, std::min(l.second, w));
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "X", JS_NewFloat64(ctx, x));
    JS_SetPropertyStr(ctx, o, "Y", JS_NewFloat64(ctx, y));
    set_int(ctx, o, "Width", width);
    set_int(ctx, o, "Height", lineH * (int)lines.size());
    set_int(ctx, o, "Lines", (int)lines.size());
    set_int(ctx, o, "Chars", (int)std::count_if(text.begin(), text.end(), [](char ch) { return ((unsigned char)ch & 0xC0) != 0x80; }));
    return o;
}

const JSCFunctionListEntry kGrFuncs[] = {
    JS_CFUNC_DEF("FillSolidRect", 5, gr_FillSolidRect),
    JS_CFUNC_DEF("DrawRect", 6, gr_DrawRect),
    JS_CFUNC_DEF("DrawLine", 6, gr_DrawLine),
    JS_CFUNC_DEF("FillRoundRect", 7, gr_FillRoundRect),
    JS_CFUNC_DEF("FillEllipse", 5, gr_FillEllipse),
    JS_CFUNC_DEF("FillGradRect", 7, gr_FillGradRect),
    JS_CFUNC_DEF("DrawString", 7, gr_DrawString),
    JS_CFUNC_DEF("GdiDrawText", 7, gr_GdiDrawText),
    JS_CFUNC_DEF("CalcTextWidth", 2, gr_CalcTextWidth),
    JS_CFUNC_DEF("CalcTextHeight", 2, gr_CalcTextHeight),
    JS_CFUNC_DEF("DrawImage", 9, gr_DrawImage),
    JS_CFUNC_DEF("DrawEllipse", 6, gr_DrawEllipse),
    JS_CFUNC_DEF("DrawRoundRect", 8, gr_DrawRoundRect),
    JS_CFUNC_DEF("DrawPolygon", 3, gr_DrawPolygon),
    JS_CFUNC_DEF("FillPolygon", 3, gr_FillPolygon),
    JS_CFUNC_DEF("EstimateLineWrap", 3, gr_EstimateLineWrap),
    JS_CFUNC_DEF("MeasureString", 7, gr_MeasureString),
    JS_CFUNC_DEF("GdiDrawBitmap", 9, gr_GdiDrawBitmap),
    JS_CFUNC_DEF("GdiAlphaBlend", 10, gr_GdiAlphaBlend),
    JS_CFUNC_DEF("SetSmoothingMode", 1, gr_noop),
    JS_CFUNC_DEF("SetTextRenderingHint", 1, gr_noop),
    JS_CFUNC_DEF("SetInterpolationMode", 1, gr_noop),
};

void font_finalizer(JSRuntime*, JSValueConst val) { delete static_cast<FontSpec*>(JS_GetOpaque(val, g_fontClass)); }

JSValue font_Name(JSContext* ctx, JSValueConst self) {
    auto* f = static_cast<FontSpec*>(JS_GetOpaque(self, g_fontClass));
    return f ? JS_NewStringLen(ctx, f->face.data(), f->face.size()) : JS_UNDEFINED;
}

JSValue font_Size(JSContext* ctx, JSValueConst self) {
    auto* f = static_cast<FontSpec*>(JS_GetOpaque(self, g_fontClass));
    return f ? JS_NewFloat64(ctx, f->size) : JS_UNDEFINED;
}

JSValue font_Style(JSContext* ctx, JSValueConst self) {
    auto* f = static_cast<FontSpec*>(JS_GetOpaque(self, g_fontClass));
    if (!f) return JS_UNDEFINED;
    return JS_NewInt32(ctx, (f->bold ? kFontBold : 0) | (f->italic ? kFontItalic : 0) |
                                (f->underline ? kFontUnderline : 0));
}

const JSCFunctionListEntry kFontFuncs[] = {
    JS_CGETSET_DEF("Name", font_Name, nullptr),
    JS_CGETSET_DEF("Size", font_Size, nullptr),
    JS_CGETSET_DEF("Style", font_Style, nullptr),
};

JSValue gdi_Font(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue obj = JS_NewObjectClass(ctx, (int)g_fontClass);
    if (JS_IsException(obj)) return obj;
    JS_SetOpaque(obj, new FontSpec(font_spec(arg_str(ctx, argc, argv, 0), (float)arg_num(ctx, argc, argv, 1, 12),
                                             arg_int(ctx, argc, argv, 2))));
    return obj;
}

void tf_finalizer(JSRuntime*, JSValueConst val) { delete static_cast<std::string*>(JS_GetOpaque(val, g_tfClass)); }

JSValue tf_Eval(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    auto* expr = static_cast<std::string*>(JS_GetOpaque(self, g_tfClass));
    if (!expr) return JS_ThrowTypeError(ctx, "not a TitleFormat object");
    const std::string out = I(ctx)->host.titleformat(*expr);
    return JS_NewStringLen(ctx, out.data(), out.size());
}

TrackPtr* handle_of(JSValueConst v) { return static_cast<TrackPtr*>(JS_GetOpaque(v, g_handleClass)); }
Tracks* list_of(JSValueConst v) { return static_cast<Tracks*>(JS_GetOpaque(v, g_listClass)); }
BitmapPtr* bitmap_of(JSValueConst v) { return static_cast<BitmapPtr*>(JS_GetOpaque(v, g_bitmapClass)); }

JSValue new_handle(JSContext* ctx, const TrackPtr& t) {
    if (!t) return JS_NULL;
    JSValue obj = JS_NewObjectClass(ctx, (int)g_handleClass);
    if (!JS_IsException(obj)) JS_SetOpaque(obj, new TrackPtr(t));
    return obj;
}

JSValue new_list(JSContext* ctx, Tracks tracks) {
    JSValue obj = JS_NewObjectClass(ctx, (int)g_listClass);
    if (!JS_IsException(obj)) JS_SetOpaque(obj, new Tracks(std::move(tracks)));
    return obj;
}

JSValue new_bitmap(JSContext* ctx, BitmapPtr b) {
    JSValue obj = JS_NewObjectClass(ctx, (int)g_bitmapClass);
    if (!JS_IsException(obj)) JS_SetOpaque(obj, new BitmapPtr(std::move(b)));
    return obj;
}

JSValue new_bitmap(JSContext* ctx, const pui::gfx::ImagePtr& img) {
    if (!img) return JS_NULL;
    auto b = std::make_shared<Bitmap>();
    b->image = img;
    return new_bitmap(ctx, std::move(b));
}

JSValue new_bitmap(JSContext* ctx, pui::gfx::Pixels px) {
    auto b = std::make_shared<Bitmap>();
    b->px = std::make_shared<pui::gfx::Pixels>(std::move(px));
    return new_bitmap(ctx, std::move(b));
}

Bitmap* bitmap_arg(JSContext* ctx, int argc, JSValueConst* argv, int i) {
    BitmapPtr* b = i < argc ? bitmap_of(argv[i]) : nullptr;
    if (!b || !*b) JS_ThrowTypeError(ctx, "argument %d is not a GdiBitmap", i + 1);
    return b && *b ? b->get() : nullptr;
}

pui::gfx::Pixels* pixels_of(JSContext* ctx, Bitmap& b) {
    if (!b.px) {
        auto px = std::make_shared<pui::gfx::Pixels>();
        if (!b.image || !I(ctx)->host.image_pixels(*b.image, *px)) {
            JS_ThrowInternalError(ctx, "can't read the bitmap's pixels");
            return nullptr;
        }
        b.px = std::move(px);
    }
    return b.px.get();
}

const pui::gfx::ImagePtr& drawable(JSContext* ctx, Bitmap& b) {
    if (!b.image && b.px) b.image = I(ctx)->host.image_from_pixels(*b.px);
    return b.image;
}

int bitmap_width(const Bitmap& b) { return b.px ? b.px->w : b.image ? b.image->width() : 0; }
int bitmap_height(const Bitmap& b) { return b.px ? b.px->h : b.image ? b.image->height() : 0; }

TrackPtr arg_track(JSContext* ctx, int argc, JSValueConst* argv, int i) {
    TrackPtr* t = i < argc ? handle_of(argv[i]) : nullptr;
    if (!t) JS_ThrowTypeError(ctx, "argument %d is not an FbMetadbHandle", i + 1);
    return t ? *t : nullptr;
}

Tracks* arg_tracks(JSContext* ctx, int argc, JSValueConst* argv, int i) {
    Tracks* l = i < argc ? list_of(argv[i]) : nullptr;
    if (!l) JS_ThrowTypeError(ctx, "argument %d is not an FbMetadbHandleList", i + 1);
    return l;
}

void handle_finalizer(JSRuntime*, JSValueConst val) { delete handle_of(val); }
void list_finalizer(JSRuntime*, JSValueConst val) { delete list_of(val); }

int list_index_property(JSContext* ctx, JSPropertyDescriptor* desc, JSValueConst obj, JSAtom prop) {
    Tracks* l = list_of(obj);
    const char* name = l ? JS_AtomToCString(ctx, prop) : nullptr;
    if (!name) return 0;
    long long i = -1;
    if (name[0] >= '0' && name[0] <= '9' && (name[0] != '0' || name[1] == 0) && std::strlen(name) < 10) {
        i = 0;
        for (const char* c = name; *c; ++c) i = (*c >= '0' && *c <= '9' && i >= 0) ? i * 10 + (*c - '0') : -1;
    }
    JS_FreeCString(ctx, name);
    if (i < 0 || i >= (long long)l->size()) return 0;
    if (desc) {
        desc->flags = JS_PROP_ENUMERABLE;
        desc->value = new_handle(ctx, (*l)[(size_t)i]);
        desc->getter = JS_UNDEFINED;
        desc->setter = JS_UNDEFINED;
    }
    return 1;
}

JSClassExoticMethods kListExotic = { list_index_property, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
void bitmap_finalizer(JSRuntime*, JSValueConst val) { delete bitmap_of(val); }
void graphics_finalizer(JSRuntime*, JSValueConst val) { delete graphics_of(val); }

TrackInfo* info_of(JSValueConst v) { return static_cast<TrackInfo*>(JS_GetOpaque(v, g_infoClass)); }
void info_finalizer(JSRuntime*, JSValueConst val) { delete info_of(val); }

std::string lower_ascii(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

template <typename List>
int find_name(const List& list, const std::string& name) {
    const std::string want = lower_ascii(name);
    for (size_t i = 0; i < list.size(); ++i)
        if (lower_ascii(list[i].first) == want) return (int)i;
    return -1;
}

JSValue info_MetaCount(JSContext* ctx, JSValueConst self) {
    TrackInfo* f = info_of(self);
    return JS_NewInt32(ctx, f ? (int)f->meta.size() : 0);
}

JSValue info_InfoCount(JSContext* ctx, JSValueConst self) {
    TrackInfo* f = info_of(self);
    return JS_NewInt32(ctx, f ? (int)f->info.size() : 0);
}

JSValue info_MetaFind(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    TrackInfo* f = info_of(self);
    return JS_NewInt32(ctx, f ? find_name(f->meta, arg_str(ctx, argc, argv, 0)) : -1);
}

JSValue info_InfoFind(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    TrackInfo* f = info_of(self);
    return JS_NewInt32(ctx, f ? find_name(f->info, arg_str(ctx, argc, argv, 0)) : -1);
}

JSValue info_MetaName(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    TrackInfo* f = info_of(self);
    const int i = arg_int(ctx, argc, argv, 0, -1);
    if (!f || i < 0 || i >= (int)f->meta.size()) return JS_ThrowRangeError(ctx, "index %d is out of range", i);
    return new_string(ctx, f->meta[(size_t)i].first);
}

JSValue info_MetaValueCount(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    TrackInfo* f = info_of(self);
    const int i = arg_int(ctx, argc, argv, 0, -1);
    if (!f || i < 0 || i >= (int)f->meta.size()) return JS_ThrowRangeError(ctx, "index %d is out of range", i);
    return JS_NewInt32(ctx, (int)f->meta[(size_t)i].second.size());
}

JSValue info_MetaValue(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    TrackInfo* f = info_of(self);
    const int i = arg_int(ctx, argc, argv, 0, -1), v = arg_int(ctx, argc, argv, 1, 0);
    if (!f || i < 0 || i >= (int)f->meta.size() || v < 0 || v >= (int)f->meta[(size_t)i].second.size())
        return JS_ThrowRangeError(ctx, "index %d, %d is out of range", i, v);
    return new_string(ctx, f->meta[(size_t)i].second[(size_t)v]);
}

template <bool Value>
JSValue info_InfoItem(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    TrackInfo* f = info_of(self);
    const int i = arg_int(ctx, argc, argv, 0, -1);
    if (!f || i < 0 || i >= (int)f->info.size()) return JS_ThrowRangeError(ctx, "index %d is out of range", i);
    return new_string(ctx, Value ? f->info[(size_t)i].second : f->info[(size_t)i].first);
}

const JSCFunctionListEntry kInfoFuncs[] = {
    JS_CGETSET_DEF("MetaCount", info_MetaCount, nullptr),
    JS_CGETSET_DEF("InfoCount", info_InfoCount, nullptr),
    JS_CFUNC_DEF("MetaFind", 1, info_MetaFind),
    JS_CFUNC_DEF("InfoFind", 1, info_InfoFind),
    JS_CFUNC_DEF("MetaName", 1, info_MetaName),
    JS_CFUNC_DEF("MetaValueCount", 1, info_MetaValueCount),
    JS_CFUNC_DEF("MetaValue", 2, info_MetaValue),
    JS_CFUNC_DEF("InfoName", 1, info_InfoItem<false>),
    JS_CFUNC_DEF("InfoValue", 1, info_InfoItem<true>),
};

JSValue handle_GetFileInfo(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    TrackPtr* t = handle_of(self);
    if (!t) return JS_NULL;
    JSValue obj = JS_NewObjectClass(ctx, (int)g_infoClass);
    if (!JS_IsException(obj)) JS_SetOpaque(obj, new TrackInfo(I(ctx)->host.track_info(*t)));
    return obj;
}

JSValue handle_Length(JSContext* ctx, JSValueConst self) {
    TrackPtr* t = handle_of(self);
    return JS_NewFloat64(ctx, t ? I(ctx)->host.track_info(*t).length : 0);
}

JSValue handle_FileSize(JSContext* ctx, JSValueConst self) {
    TrackPtr* t = handle_of(self);
    return JS_NewFloat64(ctx, t ? (double)I(ctx)->host.track_info(*t).size : -1);
}

JSValue noop(JSContext*, JSValueConst, int, JSValueConst*) { return JS_UNDEFINED; }

JSValue list_Insert(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    TrackPtr t = arg_track(ctx, argc, argv, 1);
    if (!l || !t) return JS_EXCEPTION;
    const size_t at = (size_t)std::clamp(arg_int(ctx, argc, argv, 0), 0, (int)l->size());
    l->insert(l->begin() + (std::ptrdiff_t)at, t);
    return JS_NewInt32(ctx, (int)at);
}

JSValue list_InsertRange(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    Tracks* other = arg_tracks(ctx, argc, argv, 1);
    if (!l || !other) return JS_EXCEPTION;
    const Tracks copy = *other;
    const size_t at = (size_t)std::clamp(arg_int(ctx, argc, argv, 0), 0, (int)l->size());
    l->insert(l->begin() + (std::ptrdiff_t)at, copy.begin(), copy.end());
    return JS_UNDEFINED;
}

int index_in(JSContext* ctx, const Tracks& l, const TrackPtr& t) {
    for (size_t i = 0; i < l.size(); ++i)
        if (l[i] == t || I(ctx)->host.same_track(l[i], t)) return (int)i;
    return -1;
}

JSValue list_Find(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    TrackPtr t = arg_track(ctx, argc, argv, 0);
    if (!l || !t) return JS_EXCEPTION;
    return JS_NewInt32(ctx, index_in(ctx, *l, t));
}

JSValue list_Remove(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    TrackPtr t = arg_track(ctx, argc, argv, 0);
    if (!l || !t) return JS_EXCEPTION;
    const int i = index_in(ctx, *l, t);
    if (i >= 0) l->erase(l->begin() + i);
    return JS_UNDEFINED;
}

JSValue list_RemoveById(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    const int i = arg_int(ctx, argc, argv, 0, -1);
    if (!l || i < 0 || i >= (int)l->size()) return JS_ThrowRangeError(ctx, "index %d is out of range", i);
    l->erase(l->begin() + i);
    return JS_UNDEFINED;
}

JSValue list_RemoveRange(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    if (!l) return JS_UNDEFINED;
    const int from = std::clamp(arg_int(ctx, argc, argv, 0), 0, (int)l->size());
    const int n = std::clamp(arg_int(ctx, argc, argv, 1), 0, (int)l->size() - from);
    l->erase(l->begin() + from, l->begin() + from + n);
    return JS_UNDEFINED;
}

JSValue list_RemoveAll(JSContext*, JSValueConst self, int, JSValueConst*) {
    if (Tracks* l = list_of(self)) l->clear();
    return JS_UNDEFINED;
}

template <int Op>
JSValue list_Combine(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    Tracks* other = arg_tracks(ctx, argc, argv, 0);
    if (!l || !other) return JS_EXCEPTION;
    const Tracks b = *other;
    Tracks out;
    for (const auto& t : *l)
        if ((index_in(ctx, b, t) >= 0) == (Op == 0) || Op == 1) out.push_back(t);
    if (Op == 1)
        for (const auto& t : b)
            if (index_in(ctx, out, t) < 0) out.push_back(t);
    *l = std::move(out);
    return JS_UNDEFINED;
}

std::vector<int> sorted_order(const std::vector<std::string>& keys, int direction) {
    std::vector<int> idx(keys.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = (int)i;
    std::vector<std::string> low(keys.size());
    for (size_t i = 0; i < keys.size(); ++i) low[i] = lower_ascii(keys[i]);
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) { return direction < 0 ? low[(size_t)b] < low[(size_t)a] : low[(size_t)a] < low[(size_t)b]; });
    return idx;
}

void order_by_keys(Tracks& l, const std::vector<std::string>& keys, int direction) {
    Tracks out;
    out.reserve(l.size());
    for (int i : sorted_order(keys, direction)) out.push_back(l[(size_t)i]);
    l = std::move(out);
}

JSValue list_OrderByFormat(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    auto* expr = argc > 0 ? static_cast<std::string*>(JS_GetOpaque(argv[0], g_tfClass)) : nullptr;
    if (!l) return JS_UNDEFINED;
    if (!expr) return JS_ThrowTypeError(ctx, "argument 1 is not an FbTitleFormat");
    order_by_keys(*l, I(ctx)->host.format_tracks(*expr, *l), arg_int(ctx, argc, argv, 1, 1));
    return JS_UNDEFINED;
}

template <bool Relative>
JSValue list_OrderByPath(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    Tracks* l = list_of(self);
    if (!l) return JS_UNDEFINED;
    std::vector<std::string> keys;
    for (const auto& t : *l) keys.push_back(Relative ? I(ctx)->host.library_relative_path(t) : I(ctx)->host.path_sort_key(t));
    order_by_keys(*l, keys, 1);
    return JS_UNDEFINED;
}

JSValue list_GetLibraryRelativePaths(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    Tracks* l = list_of(self);
    JSValue arr = JS_NewArray(ctx);
    if (l)
        for (uint32_t i = 0; i < l->size(); ++i) JS_SetPropertyUint32(ctx, arr, i, new_string(ctx, I(ctx)->host.library_relative_path((*l)[i])));
    return arr;
}

bool json_tags(JSContext* ctx, JSValueConst obj, JsHost::TagSet& out) {
    JSPropertyEnum* props = nullptr;
    uint32_t n = 0;
    if (!JS_IsObject(obj) || JS_IsArray(obj) || JS_GetOwnPropertyNames(ctx, &props, &n, obj, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0) return false;
    for (uint32_t i = 0; i < n; ++i) {
        const char* key = JS_AtomToCString(ctx, props[i].atom);
        JSValue v = JS_GetProperty(ctx, obj, props[i].atom);
        std::vector<std::string> values;
        if (JS_IsArray(v)) {
            int64_t len = 0;
            JS_GetLength(ctx, v, &len);
            for (int64_t k = 0; k < len; ++k) {
                JSValue e = JS_GetPropertyUint32(ctx, v, (uint32_t)k);
                const std::string s = to_string(ctx, e);
                if (!s.empty()) values.push_back(s);
                JS_FreeValue(ctx, e);
            }
        } else if (!JS_IsUndefined(v) && !JS_IsNull(v)) {
            const std::string s = to_string(ctx, v);
            if (!s.empty()) values.push_back(s);
        }
        if (key) out.emplace_back(key, std::move(values));
        JS_FreeCString(ctx, key);
        JS_FreeValue(ctx, v);
    }
    JS_FreePropertyEnum(ctx, props, n);
    return true;
}

JSValue list_UpdateFileInfoFromJSON(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    if (!l) return JS_EXCEPTION;
    const std::string text = arg_str(ctx, argc, argv, 0);
    JSValue json = JS_ParseJSON(ctx, text.data(), text.size(), "<UpdateFileInfoFromJSON>");
    if (JS_IsException(json)) return JS_EXCEPTION;
    std::vector<JsHost::TagSet> sets;
    bool ok = true;
    if (JS_IsArray(json)) {
        int64_t len = 0;
        JS_GetLength(ctx, json, &len);
        if (len != (int64_t)l->size()) ok = false;
        for (int64_t i = 0; ok && i < len; ++i) {
            JSValue e = JS_GetPropertyUint32(ctx, json, (uint32_t)i);
            sets.emplace_back();
            ok = json_tags(ctx, e, sets.back());
            JS_FreeValue(ctx, e);
        }
    } else {
        JsHost::TagSet one;
        ok = json_tags(ctx, json, one);
        sets.assign(l->size(), one);
    }
    JS_FreeValue(ctx, json);
    if (!ok) return JS_ThrowSyntaxError(ctx, "UpdateFileInfoFromJSON: expected an object, or an array with one object per item");
    I(ctx)->host.update_tags(*l, sets);
    return JS_UNDEFINED;
}

template <bool Size>
JSValue list_CalcTotal(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    Tracks* l = list_of(self);
    double total = 0;
    if (l)
        for (const auto& t : *l) {
            const TrackInfo info = I(ctx)->host.track_info(t);
            total += Size ? (double)std::max(0LL, info.size) : info.length;
        }
    return JS_NewFloat64(ctx, total);
}

JSValue handle_Path(JSContext* ctx, JSValueConst self) {
    TrackPtr* t = handle_of(self);
    if (!t) return JS_UNDEFINED;
    const std::string p = I(ctx)->host.track_path(*t);
    return JS_NewStringLen(ctx, p.data(), p.size());
}

JSValue handle_Compare(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    TrackPtr* a = handle_of(self);
    TrackPtr* b = argc > 0 ? handle_of(argv[0]) : nullptr;
    return JS_NewBool(ctx, a && b && I(ctx)->host.same_track(*a, *b));
}

const JSCFunctionListEntry kHandleFuncs[] = {
    JS_CGETSET_DEF("Path", handle_Path, nullptr),
    JS_CGETSET_DEF("RawPath", handle_Path, nullptr),
    JS_CFUNC_DEF("Compare", 1, handle_Compare),
    JS_CFUNC_DEF("GetFileInfo", 0, handle_GetFileInfo),
    JS_CGETSET_DEF("Length", handle_Length, nullptr),
    JS_CGETSET_DEF("FileSize", handle_FileSize, nullptr),
    JS_CFUNC_DEF("RefreshStats", 0, noop),
};

JSValue list_Count(JSContext* ctx, JSValueConst self) {
    Tracks* l = list_of(self);
    return JS_NewInt32(ctx, l ? (int)l->size() : 0);
}

JSValue list_Item(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    const int i = arg_int(ctx, argc, argv, 0, -1);
    if (!l || i < 0 || i >= (int)l->size()) return JS_ThrowRangeError(ctx, "index %d is out of range", i);
    return new_handle(ctx, (*l)[(size_t)i]);
}

JSValue list_Add(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    TrackPtr t = arg_track(ctx, argc, argv, 0);
    if (!l || !t) return JS_EXCEPTION;
    l->push_back(t);
    return JS_NewInt32(ctx, (int)l->size() - 1);
}

JSValue list_AddRange(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks* l = list_of(self);
    Tracks* other = arg_tracks(ctx, argc, argv, 0);
    if (!l || !other) return JS_EXCEPTION;
    const Tracks copy = *other;
    l->insert(l->end(), copy.begin(), copy.end());
    return JS_UNDEFINED;
}

JSValue list_Clone(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    Tracks* l = list_of(self);
    return new_list(ctx, l ? *l : Tracks());
}

JSValue list_Convert(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    Tracks* l = list_of(self);
    JSValue arr = JS_NewArray(ctx);
    if (l)
        for (uint32_t i = 0; i < l->size(); ++i) JS_SetPropertyUint32(ctx, arr, i, new_handle(ctx, (*l)[i]));
    return arr;
}

const JSCFunctionListEntry kListFuncs[] = {
    JS_CGETSET_DEF("Count", list_Count, nullptr),
    JS_CFUNC_DEF("Item", 1, list_Item),
    JS_CFUNC_DEF("Add", 1, list_Add),
    JS_CFUNC_DEF("AddRange", 1, list_AddRange),
    JS_CFUNC_DEF("Clone", 0, list_Clone),
    JS_CFUNC_DEF("Convert", 0, list_Convert),
    JS_CFUNC_DEF("Insert", 2, list_Insert),
    JS_CFUNC_DEF("InsertRange", 2, list_InsertRange),
    JS_CFUNC_DEF("Find", 1, list_Find),
    JS_CFUNC_DEF("BSearch", 1, list_Find),
    JS_CFUNC_DEF("Remove", 1, list_Remove),
    JS_CFUNC_DEF("RemoveById", 1, list_RemoveById),
    JS_CFUNC_DEF("RemoveRange", 2, list_RemoveRange),
    JS_CFUNC_DEF("RemoveAll", 0, list_RemoveAll),
    JS_CFUNC_DEF("MakeIntersection", 1, list_Combine<0>),
    JS_CFUNC_DEF("MakeUnion", 1, list_Combine<1>),
    JS_CFUNC_DEF("MakeDifference", 1, list_Combine<2>),
    JS_CFUNC_DEF("OrderByFormat", 2, list_OrderByFormat),
    JS_CFUNC_DEF("OrderByPath", 0, list_OrderByPath<false>),
    JS_CFUNC_DEF("OrderByRelativePath", 0, list_OrderByPath<true>),
    JS_CFUNC_DEF("Sort", 0, list_OrderByPath<false>),
    JS_CFUNC_DEF("GetLibraryRelativePaths", 0, list_GetLibraryRelativePaths),
    JS_CFUNC_DEF("CalcTotalDuration", 0, list_CalcTotal<false>),
    JS_CFUNC_DEF("CalcTotalSize", 0, list_CalcTotal<true>),
    JS_CFUNC_DEF("RefreshStats", 0, noop),
    JS_CFUNC_DEF("UpdateFileInfoFromJSON", 1, list_UpdateFileInfoFromJSON),
};

JSValue bitmap_Width(JSContext* ctx, JSValueConst self) {
    auto* b = bitmap_of(self);
    return JS_NewInt32(ctx, b && *b ? bitmap_width(**b) : 0);
}

JSValue bitmap_Height(JSContext* ctx, JSValueConst self) {
    auto* b = bitmap_of(self);
    return JS_NewInt32(ctx, b && *b ? bitmap_height(**b) : 0);
}

Bitmap* self_bitmap(JSContext* ctx, JSValueConst self) {
    BitmapPtr* b = bitmap_of(self);
    if (!b || !*b) JS_ThrowTypeError(ctx, "the GdiBitmap was disposed");
    return b && *b ? b->get() : nullptr;
}

JSValue bitmap_GetGraphics(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    BitmapPtr* bp = bitmap_of(self);
    if (!bp || !*bp) return JS_ThrowTypeError(ctx, "the GdiBitmap was disposed");
    pui::gfx::Pixels* px = pixels_of(ctx, **bp);
    if (!px) return JS_EXCEPTION;
    auto g = std::make_unique<Graphics>();
    g->own = I(ctx)->host.pixels_canvas(*px, 1.0);
    if (!g->own) return JS_ThrowInternalError(ctx, "GetGraphics: can't draw on this bitmap");
    g->canvas = g->own.get();
    g->target = *bp;
    (*bp)->image.reset();
    JSValue obj = JS_NewObjectClass(ctx, (int)g_grClass);
    if (!JS_IsException(obj)) JS_SetOpaque(obj, g.release());
    return obj;
}

void release_graphics(Graphics* g) {
    if (!g || !g->own) return;
    g->canvas = nullptr;
    g->own.reset();
    if (g->target) g->target->image.reset();
    g->target.reset();
}

JSValue bitmap_ReleaseGraphics(JSContext*, JSValueConst, int argc, JSValueConst* argv) {
    if (argc > 0) release_graphics(graphics_of(argv[0]));
    return JS_UNDEFINED;
}

template <typename F>
JSValue edit_pixels(JSContext* ctx, JSValueConst self, F&& f) {
    Bitmap* b = self_bitmap(ctx, self);
    if (!b) return JS_EXCEPTION;
    pui::gfx::Pixels* px = pixels_of(ctx, *b);
    if (!px) return JS_EXCEPTION;
    JSValue r = f(*px);
    b->image.reset();
    return r;
}

template <typename F>
JSValue derive_pixels(JSContext* ctx, JSValueConst self, F&& f) {
    Bitmap* b = self_bitmap(ctx, self);
    if (!b) return JS_EXCEPTION;
    pui::gfx::Pixels* px = pixels_of(ctx, *b);
    if (!px) return JS_EXCEPTION;
    return new_bitmap(ctx, f(*px));
}

JSValue bitmap_Clone(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    return derive_pixels(ctx, self, [&](const pui::gfx::Pixels& px) {
        return img::crop(px, arg_int(ctx, argc, argv, 0), arg_int(ctx, argc, argv, 1), arg_int(ctx, argc, argv, 2, px.w), arg_int(ctx, argc, argv, 3, px.h));
    });
}

JSValue bitmap_Resize(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    const int w = arg_int(ctx, argc, argv, 0), h = arg_int(ctx, argc, argv, 1);
    if (w <= 0 || h <= 0) return JS_ThrowRangeError(ctx, "Resize: invalid size %dx%d", w, h);
    const bool nearest = arg_int(ctx, argc, argv, 2) == 5;
    return derive_pixels(ctx, self, [&](const pui::gfx::Pixels& px) { return img::resize(px, w, h, nearest); });
}

JSValue bitmap_ApplyMask(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Bitmap* mask = bitmap_arg(ctx, argc, argv, 0);
    if (!mask) return JS_EXCEPTION;
    pui::gfx::Pixels* mpx = pixels_of(ctx, *mask);
    if (!mpx) return JS_EXCEPTION;
    const pui::gfx::Pixels maskCopy = *mpx;
    return edit_pixels(ctx, self, [&](pui::gfx::Pixels& px) { return JS_NewBool(ctx, img::apply_mask(px, maskCopy)); });
}

JSValue bitmap_ApplyAlpha(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    const int alpha = arg_int(ctx, argc, argv, 0, 255);
    return derive_pixels(ctx, self, [&](const pui::gfx::Pixels& px) {
        pui::gfx::Pixels out = px;
        img::apply_alpha(out, alpha);
        return out;
    });
}

JSValue bitmap_InvertColours(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    return derive_pixels(ctx, self, [&](const pui::gfx::Pixels& px) {
        pui::gfx::Pixels out = px;
        img::invert(out);
        return out;
    });
}

JSValue bitmap_RotateFlip(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    const int mode = arg_int(ctx, argc, argv, 0);
    return edit_pixels(ctx, self, [&](pui::gfx::Pixels& px) {
        img::rotate_flip(px, mode);
        return JS_UNDEFINED;
    });
}

JSValue bitmap_StackBlur(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    const int radius = arg_int(ctx, argc, argv, 0);
    return edit_pixels(ctx, self, [&](pui::gfx::Pixels& px) {
        img::stack_blur(px, radius);
        return JS_UNDEFINED;
    });
}

template <bool Json>
JSValue bitmap_GetColourScheme(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Bitmap* b = self_bitmap(ctx, self);
    if (!b) return JS_EXCEPTION;
    pui::gfx::Pixels* px = pixels_of(ctx, *b);
    if (!px) return JS_EXCEPTION;
    const auto scheme = img::colour_scheme(*px, arg_int(ctx, argc, argv, 0, 1));
    if (!Json) {
        JSValue arr = JS_NewArray(ctx);
        for (uint32_t i = 0; i < scheme.size(); ++i) JS_SetPropertyUint32(ctx, arr, i, JS_NewUint32(ctx, scheme[i].first));
        return arr;
    }
    std::string json = "[";
    for (size_t i = 0; i < scheme.size(); ++i) {
        char buf[96];
        snprintf(buf, sizeof buf, "%s{\"col\":%u,\"freq\":%.4f}", i ? "," : "", scheme[i].first, scheme[i].second);
        json += buf;
    }
    return new_string(ctx, json + "]");
}

JSValue bitmap_CreateRawBitmap(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    BitmapPtr* b = bitmap_of(self);
    if (!b || !*b) return JS_ThrowTypeError(ctx, "the GdiBitmap was disposed");
    return new_bitmap(ctx, *b);
}

JSValue bitmap_SaveAs(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Bitmap* b = self_bitmap(ctx, self);
    if (!b) return JS_EXCEPTION;
    const pui::gfx::ImagePtr& img = drawable(ctx, *b);
    return JS_NewBool(ctx, img && I(ctx)->host.save_png(*img, fix_path(arg_str(ctx, argc, argv, 0))));
}

JSValue bitmap_Dispose(JSContext*, JSValueConst self, int, JSValueConst*) {
    if (auto* b = bitmap_of(self)) b->reset();
    return JS_UNDEFINED;
}

const JSCFunctionListEntry kBitmapFuncs[] = {
    JS_CGETSET_DEF("Width", bitmap_Width, nullptr),
    JS_CGETSET_DEF("Height", bitmap_Height, nullptr),
    JS_CFUNC_DEF("Dispose", 0, bitmap_Dispose),
    JS_CFUNC_DEF("GetGraphics", 0, bitmap_GetGraphics),
    JS_CFUNC_DEF("ReleaseGraphics", 1, bitmap_ReleaseGraphics),
    JS_CFUNC_DEF("Clone", 4, bitmap_Clone),
    JS_CFUNC_DEF("Resize", 3, bitmap_Resize),
    JS_CFUNC_DEF("ApplyMask", 1, bitmap_ApplyMask),
    JS_CFUNC_DEF("ApplyAlpha", 1, bitmap_ApplyAlpha),
    JS_CFUNC_DEF("InvertColours", 0, bitmap_InvertColours),
    JS_CFUNC_DEF("RotateFlip", 1, bitmap_RotateFlip),
    JS_CFUNC_DEF("StackBlur", 1, bitmap_StackBlur),
    JS_CFUNC_DEF("GetColourScheme", 1, bitmap_GetColourScheme<false>),
    JS_CFUNC_DEF("GetColourSchemeJSON", 1, bitmap_GetColourScheme<true>),
    JS_CFUNC_DEF("CreateRawBitmap", 0, bitmap_CreateRawBitmap),
    JS_CFUNC_DEF("SaveAs", 2, bitmap_SaveAs),
};

JSValue tf_EvalWithMetadb(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    auto* expr = static_cast<std::string*>(JS_GetOpaque(self, g_tfClass));
    TrackPtr t = arg_track(ctx, argc, argv, 0);
    if (!expr || !t) return JS_EXCEPTION;
    const auto out = I(ctx)->host.format_tracks(*expr, { t });
    const std::string v = out.empty() ? std::string() : out[0];
    return JS_NewStringLen(ctx, v.data(), v.size());
}

JSValue tf_EvalWithMetadbs(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    auto* expr = static_cast<std::string*>(JS_GetOpaque(self, g_tfClass));
    Tracks* l = arg_tracks(ctx, argc, argv, 0);
    if (!expr || !l) return JS_EXCEPTION;
    const auto out = I(ctx)->host.format_tracks(*expr, *l);
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < out.size(); ++i) JS_SetPropertyUint32(ctx, arr, i, JS_NewStringLen(ctx, out[i].data(), out[i].size()));
    return arr;
}

const JSCFunctionListEntry kTfFuncs[] = {
    JS_CFUNC_DEF("Eval", 1, tf_Eval),
    JS_CFUNC_DEF("EvalWithMetadb", 1, tf_EvalWithMetadb),
    JS_CFUNC_DEF("EvalWithMetadbs", 1, tf_EvalWithMetadbs),
};

JSValue fb_TitleFormat(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue obj = JS_NewObjectClass(ctx, (int)g_tfClass);
    if (JS_IsException(obj)) return obj;
    JS_SetOpaque(obj, new std::string(arg_str(ctx, argc, argv, 0)));
    return obj;
}

template <Playback P>
JSValue fb_playback(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    I(ctx)->host.playback(P);
    return JS_UNDEFINED;
}

JSValue fb_GetNowPlaying(JSContext* ctx, JSValueConst, int, JSValueConst*) { return new_handle(ctx, I(ctx)->host.now_playing()); }
JSValue fb_GetLibraryItems(JSContext* ctx, JSValueConst, int, JSValueConst*) { return new_list(ctx, I(ctx)->host.library_items()); }
JSValue fb_CreateHandleList(JSContext* ctx, JSValueConst, int, JSValueConst*) { return new_list(ctx, {}); }

JSValue fb_IsPlaying(JSContext* ctx, JSValueConst) { return JS_NewBool(ctx, I(ctx)->host.is_playing()); }
JSValue fb_IsPaused(JSContext* ctx, JSValueConst) { return JS_NewBool(ctx, I(ctx)->host.is_paused()); }
JSValue fb_PlaybackTime(JSContext* ctx, JSValueConst) { return JS_NewFloat64(ctx, I(ctx)->host.playback_time()); }
JSValue fb_PlaybackLength(JSContext* ctx, JSValueConst) { return JS_NewFloat64(ctx, I(ctx)->host.playback_length()); }

JSValue console_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string line;
    for (int i = 0; i < argc; ++i) {
        if (i) line += ' ';
        line += to_string(ctx, argv[i]);
    }
    I(ctx)->host.console(line);
    return JS_UNDEFINED;
}

struct MenuData;
using MenuPtr = std::shared_ptr<MenuData>;
struct MenuData {
    struct Entry {
        MenuItem item;
        MenuPtr sub;
        std::weak_ptr<MenuData> loop;
        MenuPtr child() const { return sub ? sub : loop.lock(); }
    };
    std::vector<Entry> entries;
};

constexpr uint32_t kMfGrayed = 0x1, kMfDisabled = 0x2, kMfChecked = 0x8, kMfPopup = 0x10, kMfRadioCheck = 0x200, kMfSeparator = 0x800;
constexpr int kMenuDepth = 16;

MenuPtr* menu_of(JSValueConst v) { return static_cast<MenuPtr*>(JS_GetOpaque(v, g_menuClass)); }
void menu_finalizer(JSRuntime*, JSValueConst val) { delete menu_of(val); }

struct MenuManager {
    MenuSourcePtr source;
    bool main = false;
};
MenuManager* manager_of(JSValueConst v) { return static_cast<MenuManager*>(JS_GetOpaque(v, g_menuManagerClass)); }
void manager_finalizer(JSRuntime*, JSValueConst val) { delete manager_of(val); }

MenuItem flags_item(uint32_t flags, int id, const std::string& text) {
    MenuItem m;
    m.text = text;
    m.id = id;
    m.separator = (flags & kMfSeparator) != 0;
    m.disabled = (flags & (kMfGrayed | kMfDisabled)) != 0;
    m.checked = (flags & kMfChecked) != 0;
    m.radio = (flags & kMfRadioCheck) != 0;
    return m;
}

Menu flatten(const MenuData& menu, int depth) {
    Menu out;
    for (const auto& e : menu.entries) {
        MenuItem m = e.item;
        if (const MenuPtr c = e.child()) {
            m.submenu = true;
            if (depth < kMenuDepth) m.children = flatten(*c, depth + 1);
        }
        out.push_back(std::move(m));
    }
    return out;
}

template <typename F>
void for_each_item(Menu& items, F&& f) {
    for (auto& m : items) {
        if (!m.separator && !m.submenu) f(m);
        for_each_item(m.children, f);
    }
}

template <typename F>
void for_each_item(MenuData& menu, F&& f, int depth = 0) {
    for (auto& e : menu.entries) {
        const MenuPtr c = e.child();
        if (!e.item.separator && !e.item.submenu && !c) f(e.item);
        for_each_item(e.item.children, f);
        if (c && depth < kMenuDepth) for_each_item(*c, f, depth + 1);
    }
}

MenuData* menu_or_throw(JSContext* ctx, JSValueConst self) {
    MenuPtr* m = menu_of(self);
    if (!m || !*m) JS_ThrowTypeError(ctx, "not a MenuObject");
    return m && *m ? m->get() : nullptr;
}

JSValue menu_AppendMenuItem(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    MenuData* m = menu_or_throw(ctx, self);
    if (!m) return JS_EXCEPTION;
    const uint32_t flags = arg_u32(ctx, argc, argv, 0);
    if (flags & kMfPopup) return JS_ThrowTypeError(ctx, "AppendMenuItem: use AppendTo for submenus");
    m->entries.push_back({ flags_item(flags, arg_int(ctx, argc, argv, 1), arg_str(ctx, argc, argv, 2)), nullptr, {} });
    return JS_UNDEFINED;
}

JSValue menu_AppendMenuSeparator(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    MenuData* m = menu_or_throw(ctx, self);
    if (!m) return JS_EXCEPTION;
    m->entries.push_back({ flags_item(kMfSeparator, 0, {}), nullptr, {} });
    return JS_UNDEFINED;
}

bool menu_reaches(const MenuData& from, const MenuData* target, int depth = 0) {
    if (&from == target) return true;
    if (depth >= kMenuDepth) return false;
    for (const auto& e : from.entries)
        if (e.sub && menu_reaches(*e.sub, target, depth + 1)) return true;
    return false;
}

JSValue menu_AppendTo(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    MenuPtr* m = menu_of(self);
    MenuPtr* parent = argc > 0 ? menu_of(argv[0]) : nullptr;
    if (!m || !*m || !parent || !*parent) return JS_ThrowTypeError(ctx, "AppendTo: argument 1 is not a MenuObject");
    MenuItem item = flags_item(arg_u32(ctx, argc, argv, 1), 0, arg_str(ctx, argc, argv, 2));
    item.submenu = true;
    if (menu_reaches(**m, parent->get())) (*parent)->entries.push_back({ item, nullptr, *m });
    else (*parent)->entries.push_back({ item, *m, {} });
    return JS_UNDEFINED;
}

JSValue menu_CheckMenuItem(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    MenuData* m = menu_or_throw(ctx, self);
    if (!m) return JS_EXCEPTION;
    const int id = arg_int(ctx, argc, argv, 0);
    const bool on = arg_bool(ctx, argc, argv, 1);
    for_each_item(*m, [&](MenuItem& i) {
        if (i.id == id) i.checked = on;
    });
    return JS_UNDEFINED;
}

JSValue menu_CheckMenuRadioItem(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    MenuData* m = menu_or_throw(ctx, self);
    if (!m) return JS_EXCEPTION;
    const int first = arg_int(ctx, argc, argv, 0), last = arg_int(ctx, argc, argv, 1), sel = arg_int(ctx, argc, argv, 2);
    for_each_item(*m, [&](MenuItem& i) {
        if (i.id < first || i.id > last) return;
        i.radio = true;
        i.checked = i.id == sel;
    });
    return JS_UNDEFINED;
}

JSValue menu_EnableMenuItem(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    MenuData* m = menu_or_throw(ctx, self);
    if (!m) return JS_EXCEPTION;
    const int id = arg_int(ctx, argc, argv, 0);
    const bool on = arg_bool(ctx, argc, argv, 1, true);
    for_each_item(*m, [&](MenuItem& i) {
        if (i.id == id) i.disabled = !on;
    });
    return JS_UNDEFINED;
}

JSValue menu_TrackPopupMenu(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    MenuData* m = menu_or_throw(ctx, self);
    if (!m) return JS_EXCEPTION;
    JsEngine::Impl* e = I(ctx);
    const Menu items = flatten(*m, 0);
    const int x = arg_int(ctx, argc, argv, 0), y = arg_int(ctx, argc, argv, 1);
    return JS_NewInt32(ctx, e->modal([&] { return e->host.popup_menu(x, y, items); }));
}

const JSCFunctionListEntry kMenuFuncs[] = {
    JS_CFUNC_DEF("AppendMenuItem", 3, menu_AppendMenuItem),
    JS_CFUNC_DEF("AppendMenuSeparator", 0, menu_AppendMenuSeparator),
    JS_CFUNC_DEF("AppendTo", 3, menu_AppendTo),
    JS_CFUNC_DEF("CheckMenuItem", 2, menu_CheckMenuItem),
    JS_CFUNC_DEF("CheckMenuRadioItem", 3, menu_CheckMenuRadioItem),
    JS_CFUNC_DEF("EnableMenuItem", 2, menu_EnableMenuItem),
    JS_CFUNC_DEF("TrackPopupMenu", 3, menu_TrackPopupMenu),
    JS_CFUNC_DEF("Dispose", 0, noop),
};

JSValue window_CreatePopupMenu(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue obj = JS_NewObjectClass(ctx, (int)g_menuClass);
    if (!JS_IsException(obj)) JS_SetOpaque(obj, new MenuPtr(std::make_shared<MenuData>()));
    return obj;
}

JSValue window_SetCursor(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.set_cursor(arg_int(ctx, argc, argv, 0));
    return JS_UNDEFINED;
}

template <bool Main>
JSValue fb_CreateMenuManager(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JSValue obj = JS_NewObjectClass(ctx, (int)g_menuManagerClass);
    if (!JS_IsException(obj)) JS_SetOpaque(obj, new MenuManager{ nullptr, Main });
    return obj;
}

JSValue manager_init(JSContext* ctx, JSValueConst self, MenuSourcePtr source, const char* what) {
    MenuManager* mm = manager_of(self);
    if (!mm) return JS_ThrowTypeError(ctx, "not a menu manager");
    mm->source = std::move(source);
    if (!mm->source) return JS_ThrowInternalError(ctx, "%s: no menu", what);
    return JS_UNDEFINED;
}

JSValue manager_InitContext(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    Tracks tracks;
    if (argc > 0 && handle_of(argv[0])) tracks.push_back(*handle_of(argv[0]));
    else if (argc > 0 && list_of(argv[0])) tracks = *list_of(argv[0]);
    else return JS_ThrowTypeError(ctx, "argument 1 is not an FbMetadbHandle or FbMetadbHandleList");
    return manager_init(ctx, self, I(ctx)->host.context_menu(ContextKind::Tracks, tracks), "InitContext");
}

template <ContextKind K>
JSValue manager_InitKind(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    return manager_init(ctx, self, I(ctx)->host.context_menu(K, {}), K == ContextKind::Playlist ? "InitContextPlaylist" : "InitNowPlaying");
}

JSValue manager_Init(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    const std::string root = arg_str(ctx, argc, argv, 0);
    MenuSourcePtr src = I(ctx)->host.main_menu(root);
    if (!src) return JS_ThrowRangeError(ctx, "Init: unknown main menu '%s'", root.c_str());
    return manager_init(ctx, self, std::move(src), "Init");
}

JSValue manager_BuildMenu(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    MenuManager* mm = manager_of(self);
    MenuPtr* m = argc > 0 ? menu_of(argv[0]) : nullptr;
    if (!m || !*m) return JS_ThrowTypeError(ctx, "BuildMenu: argument 1 is not a MenuObject");
    if (!mm || !mm->source) return JS_ThrowInternalError(ctx, "BuildMenu: call %s first", mm && mm->main ? "Init" : "InitContext");
    const int base = arg_int(ctx, argc, argv, 1, 1);
    const int count = arg_int(ctx, argc, argv, 2, -1);
    const int maxId = count < 0 ? -1 : mm->main ? base + count - 1 : count;
    for (MenuItem& item : mm->source->build(base, maxId)) (*m)->entries.push_back({ std::move(item), nullptr, {} });
    return JS_UNDEFINED;
}

JSValue manager_ExecuteByID(JSContext* ctx, JSValueConst self, int argc, JSValueConst* argv) {
    MenuManager* mm = manager_of(self);
    return JS_NewBool(ctx, mm && mm->source && mm->source->execute(arg_int(ctx, argc, argv, 0, -1)));
}

const JSCFunctionListEntry kMenuManagerFuncs[] = {
    JS_CFUNC_DEF("InitContext", 1, manager_InitContext),
    JS_CFUNC_DEF("InitContextPlaylist", 0, manager_InitKind<ContextKind::Playlist>),
    JS_CFUNC_DEF("InitNowPlaying", 0, manager_InitKind<ContextKind::NowPlaying>),
    JS_CFUNC_DEF("Init", 1, manager_Init),
    JS_CFUNC_DEF("BuildMenu", 3, manager_BuildMenu),
    JS_CFUNC_DEF("ExecuteByID", 1, manager_ExecuteByID),
    JS_CFUNC_DEF("Dispose", 0, noop),
};

template <std::string (JsHost::*Get)()>
JSValue fb_string(JSContext* ctx, JSValueConst) { return new_string(ctx, (I(ctx)->host.*Get)()); }

JSValue fb_Volume(JSContext* ctx, JSValueConst) { return JS_NewFloat64(ctx, I(ctx)->host.volume()); }
JSValue fb_SetVolume(JSContext* ctx, JSValueConst, JSValueConst v) {
    double d = 0;
    if (JS_ToFloat64(ctx, &d, v) == 0 && std::isfinite(d)) I(ctx)->host.set_volume(std::clamp(d, -100.0, 0.0));
    return JS_UNDEFINED;
}

template <int Step>
JSValue fb_VolumeStep(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    JsHost& h = I(ctx)->host;
    h.set_volume(Step == 0 ? (h.volume() > -100 ? -100.0 : 0.0) : std::clamp(h.volume() + Step, -100.0, 0.0));
    return JS_UNDEFINED;
}

template <Flag F>
JSValue fb_GetFlag(JSContext* ctx, JSValueConst) { return JS_NewBool(ctx, I(ctx)->host.flag(F)); }
template <Flag F>
JSValue fb_SetFlag(JSContext* ctx, JSValueConst, JSValueConst v) {
    I(ctx)->host.set_flag(F, JS_ToBool(ctx, v) > 0);
    return JS_UNDEFINED;
}

JSValue fb_RunMainMenuCommand(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.run_main_menu_command(arg_str(ctx, argc, argv, 0)));
}

JSValue fb_RunContextCommand(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.run_context_command(arg_str(ctx, argc, argv, 0), nullptr));
}

JSValue fb_RunContextCommandWithMetadb(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Tracks tracks;
    if (argc > 1 && handle_of(argv[1])) tracks.push_back(*handle_of(argv[1]));
    else if (argc > 1 && list_of(argv[1])) tracks = *list_of(argv[1]);
    else return JS_ThrowTypeError(ctx, "argument 2 is not an FbMetadbHandle or FbMetadbHandleList");
    return JS_NewBool(ctx, I(ctx)->host.run_context_command(arg_str(ctx, argc, argv, 0), &tracks));
}

JSValue fb_ShowPopupMessage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.popup_message(arg_str(ctx, argc, argv, 0), argc > 1 ? arg_str(ctx, argc, argv, 1) : std::string("JS Panel"));
    return JS_UNDEFINED;
}

JSValue fb_GetFocusItem(JSContext* ctx, JSValueConst, int, JSValueConst*) { return new_handle(ctx, I(ctx)->host.focus_item()); }

JSValue fb_GetQueryItems(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Tracks* l = arg_tracks(ctx, argc, argv, 0);
    if (!l) return JS_EXCEPTION;
    std::string err;
    Tracks out = I(ctx)->host.query_items(*l, arg_str(ctx, argc, argv, 1), err);
    if (!err.empty()) return JS_ThrowSyntaxError(ctx, "GetQueryItems: %s", err.c_str());
    return new_list(ctx, std::move(out));
}

JSValue fb_IsMetadbInMediaLibrary(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    TrackPtr t = arg_track(ctx, argc, argv, 0);
    if (!t) return JS_EXCEPTION;
    return JS_NewBool(ctx, I(ctx)->host.in_library(t));
}

JSValue fb_GetLibraryRelativePath(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    TrackPtr t = arg_track(ctx, argc, argv, 0);
    if (!t) return JS_EXCEPTION;
    return new_string(ctx, I(ctx)->host.library_relative_path(t));
}

JSValue fb_Exit(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    I(ctx)->host.exit_app();
    return JS_UNDEFINED;
}

const JSCFunctionListEntry kFbFuncs[] = {
    JS_CFUNC_DEF("TitleFormat", 1, fb_TitleFormat),
    JS_CFUNC_DEF("Play", 0, fb_playback<Playback::Play>),
    JS_CFUNC_DEF("Pause", 0, fb_playback<Playback::Pause>),
    JS_CFUNC_DEF("PlayOrPause", 0, fb_playback<Playback::PlayOrPause>),
    JS_CFUNC_DEF("Stop", 0, fb_playback<Playback::Stop>),
    JS_CFUNC_DEF("Next", 0, fb_playback<Playback::Next>),
    JS_CFUNC_DEF("Prev", 0, fb_playback<Playback::Prev>),
    JS_CFUNC_DEF("trace", 1, console_log),
    JS_CFUNC_DEF("GetNowPlaying", 0, fb_GetNowPlaying),
    JS_CFUNC_DEF("GetLibraryItems", 0, fb_GetLibraryItems),
    JS_CFUNC_DEF("CreateHandleList", 0, fb_CreateHandleList),
    JS_CGETSET_DEF("IsPlaying", fb_IsPlaying, nullptr),
    JS_CGETSET_DEF("IsPaused", fb_IsPaused, nullptr),
    JS_CGETSET_DEF("PlaybackTime", fb_PlaybackTime, nullptr),
    JS_CGETSET_DEF("PlaybackLength", fb_PlaybackLength, nullptr),
    JS_CFUNC_DEF("Random", 0, fb_playback<Playback::Random>),
    JS_CGETSET_DEF("ProfilePath", fb_string<&JsHost::profile_path>, nullptr),
    JS_CGETSET_DEF("ComponentPath", fb_string<&JsHost::component_path>, nullptr),
    JS_CGETSET_DEF("FoobarPath", fb_string<&JsHost::foobar_path>, nullptr),
    JS_CGETSET_DEF("Version", fb_string<&JsHost::version>, nullptr),
    JS_CGETSET_DEF("Volume", fb_Volume, fb_SetVolume),
    JS_CFUNC_DEF("VolumeUp", 0, fb_VolumeStep<2>),
    JS_CFUNC_DEF("VolumeDown", 0, fb_VolumeStep<-2>),
    JS_CFUNC_DEF("VolumeMute", 0, fb_VolumeStep<0>),
    JS_CGETSET_DEF("StopAfterCurrent", fb_GetFlag<Flag::StopAfterCurrent>, fb_SetFlag<Flag::StopAfterCurrent>),
    JS_CGETSET_DEF("CursorFollowPlayback", fb_GetFlag<Flag::CursorFollowPlayback>, fb_SetFlag<Flag::CursorFollowPlayback>),
    JS_CGETSET_DEF("PlaybackFollowCursor", fb_GetFlag<Flag::PlaybackFollowCursor>, fb_SetFlag<Flag::PlaybackFollowCursor>),
    JS_CFUNC_DEF("RunMainMenuCommand", 1, fb_RunMainMenuCommand),
    JS_CFUNC_DEF("RunContextCommand", 1, fb_RunContextCommand),
    JS_CFUNC_DEF("RunContextCommandWithMetadb", 2, fb_RunContextCommandWithMetadb),
    JS_CFUNC_DEF("ShowPopupMessage", 2, fb_ShowPopupMessage),
    JS_CFUNC_DEF("GetFocusItem", 0, fb_GetFocusItem),
    JS_CFUNC_DEF("GetQueryItems", 2, fb_GetQueryItems),
    JS_CFUNC_DEF("IsMetadbInMediaLibrary", 1, fb_IsMetadbInMediaLibrary),
    JS_CFUNC_DEF("GetLibraryRelativePath", 1, fb_GetLibraryRelativePath),
    JS_CFUNC_DEF("Exit", 0, fb_Exit),
    JS_CFUNC_DEF("CreateContextMenuManager", 0, fb_CreateMenuManager<false>),
    JS_CFUNC_DEF("CreateMainMenuManager", 0, fb_CreateMenuManager<true>),
};

JSValue album_object(JSContext* ctx, const LibraryAlbum& a) {
    JSValue o = JS_NewObject(ctx);
    set_str(ctx, o, "Id", a.id);
    set_str(ctx, o, "Name", a.name);
    set_str(ctx, o, "Artist", a.artist);
    set_str(ctx, o, "ArtistId", a.artistId);
    set_str(ctx, o, "CoverArtId", a.coverArtId);
    set_str(ctx, o, "Created", a.created);
    set_int(ctx, o, "Year", a.year);
    set_int(ctx, o, "SongCount", a.songCount);
    return o;
}

JSValue artist_object(JSContext* ctx, const LibraryArtist& a) {
    JSValue o = JS_NewObject(ctx);
    set_str(ctx, o, "Id", a.id);
    set_str(ctx, o, "Name", a.name);
    set_int(ctx, o, "AlbumCount", a.albumCount);
    set_int(ctx, o, "SongCount", a.songCount);
    return o;
}

JSValue nd_IsAvailable(JSContext* ctx, JSValueConst) {
    return JS_NewBool(ctx, I(ctx)->host.navidrome_state() != LibraryState::Missing);
}

JSValue nd_IsConfigured(JSContext* ctx, JSValueConst) {
    return JS_NewBool(ctx, I(ctx)->host.navidrome_state() == LibraryState::Ready);
}

JSValue nd_GetAlbumsAsync(JSContext* ctx, JSValueConst, int, JSValueConst*) { return I(ctx)->request_albums(false); }
JSValue nd_GetArtistsAsync(JSContext* ctx, JSValueConst, int, JSValueConst*) { return I(ctx)->request_albums(true); }

JSValue nd_GetArtistInfoAsync(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    int id = 0;
    JSValue promise = e->new_promise(id);
    if (JS_IsException(promise)) return promise;
    std::shared_ptr<bool> token = e->alive;
    e->host.navidrome_artist_info(arg_str(ctx, argc, argv, 0), [token, e, id](ArtistInfo info, std::string err) {
        if (!*token) return;
        JSContext* c = e->ctx;
        if (!err.empty()) {
            JSValue error = JS_NewError(c);
            set_str(c, error, "message", err);
            e->settle_promise(id, error, false);
            return;
        }
        JSValue o = JS_NewObject(c);
        set_str(c, o, "Id", info.id);
        set_str(c, o, "Biography", info.biography);
        set_str(c, o, "ImageUrl", info.imageUrl);
        set_str(c, o, "LastFmUrl", info.lastFmUrl);
        e->settle_promise(id, o);
    });
    return promise;
}

template <bool Artist, bool Add>
JSValue nd_play(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const std::string id = arg_str(ctx, argc, argv, 0);
    if (id.empty()) return JS_ThrowTypeError(ctx, "the first argument must be an id");
    bool replace = !Add, play = !Add;
    if (!Add && argc > 1) replace = JS_ToBool(ctx, argv[1]) > 0;
    if (!Add && argc > 2) play = JS_ToBool(ctx, argv[2]) > 0;
    I(ctx)->host.navidrome_play(Artist, id, replace, play);
    return JS_UNDEFINED;
}

const JSCFunctionListEntry kNavidromeFuncs[] = {
    JS_CGETSET_DEF("IsAvailable", nd_IsAvailable, nullptr),
    JS_CGETSET_DEF("IsConfigured", nd_IsConfigured, nullptr),
    JS_CFUNC_DEF("GetAlbumsAsync", 0, nd_GetAlbumsAsync),
    JS_CFUNC_DEF("GetArtistsAsync", 0, nd_GetArtistsAsync),
    JS_CFUNC_DEF("GetArtistInfoAsync", 1, nd_GetArtistInfoAsync),
    JS_CFUNC_DEF("PlayAlbum", 3, (nd_play<false, false>)),
    JS_CFUNC_DEF("AddAlbum", 1, (nd_play<false, true>)),
    JS_CFUNC_DEF("PlayArtist", 3, (nd_play<true, false>)),
    JS_CFUNC_DEF("AddArtist", 1, (nd_play<true, true>)),
};

JSValue gdi_Image(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    return new_bitmap(ctx, e->host.load_image(script_relative(e, arg_str(ctx, argc, argv, 0))));
}

JSValue gdi_CreateImage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const int w = arg_int(ctx, argc, argv, 0), h = arg_int(ctx, argc, argv, 1);
    if (w <= 0 || h <= 0 || (long long)w * h > 64LL * 1024 * 1024) return JS_ThrowRangeError(ctx, "CreateImage: invalid size %dx%d", w, h);
    return new_bitmap(ctx, img::blank(w, h));
}

JSValue gdi_LoadImageAsync(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    const std::string given = arg_str(ctx, argc, argv, 1);
    const int task = e->nextTask++;
    std::shared_ptr<bool> token = e->alive;
    e->host.load_image_async(script_relative(e, given), [token, e, task, given](pui::gfx::ImagePtr img, std::string) {
        if (*token) e->image_loaded(task, std::move(img), given);
    });
    return JS_NewInt32(ctx, task);
}

JSValue gdi_LoadImageAsyncV2(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    int id = 0;
    JSValue promise = e->new_promise(id);
    if (JS_IsException(promise)) return promise;
    std::shared_ptr<bool> token = e->alive;
    e->host.load_image_async(script_relative(e, arg_str(ctx, argc, argv, 1)), [token, e, id](pui::gfx::ImagePtr img, std::string) {
        if (*token) e->settle_promise(id, new_bitmap(e->ctx, img));
    });
    return promise;
}

const JSCFunctionListEntry kGdiFuncs[] = {
    JS_CFUNC_DEF("Font", 3, gdi_Font),
    JS_CFUNC_DEF("Image", 1, gdi_Image),
    JS_CFUNC_DEF("CreateImage", 2, gdi_CreateImage),
    JS_CFUNC_DEF("LoadImageAsync", 2, gdi_LoadImageAsync),
    JS_CFUNC_DEF("LoadImageAsyncV2", 2, gdi_LoadImageAsyncV2),
};

JSValue utils_GetAlbumArtAsync(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    TrackPtr t = arg_track(ctx, argc, argv, 1);
    if (!t) return JS_EXCEPTION;
    const int artId = arg_int(ctx, argc, argv, 2, 0);
    JsEngine::Impl* self = I(ctx);
    std::shared_ptr<bool> token = self->alive;
    self->host.album_art(t, artId, [token, self, t, artId](pui::gfx::ImagePtr img, std::string path) {
        if (*token) self->art_done(t, artId, std::move(img), path);
    });
    return JS_UNDEFINED;
}

JSValue utils_IsKeyPressed(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.key_pressed(arg_int(ctx, argc, argv, 0)));
}

JSValue utils_CheckComponent(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.has_component(arg_str(ctx, argc, argv, 0), arg_bool(ctx, argc, argv, 1, true)));
}

JSValue utils_true(JSContext*, JSValueConst, int, JSValueConst*) { return JS_TRUE; }

JSValue split_path(JSContext* ctx, const std::string& in) {
    const std::string path = fix_path(in);
    const auto cut = path.find_last_of("/\\");
    const std::string dir = cut == std::string::npos ? std::string() : path.substr(0, cut + 1);
    const std::string file = cut == std::string::npos ? path : path.substr(cut + 1);
    const auto dot = file.find_last_of('.');
    JSValue arr = JS_NewArray(ctx);
    JS_SetPropertyUint32(ctx, arr, 0, new_string(ctx, dir));
    JS_SetPropertyUint32(ctx, arr, 1, new_string(ctx, dot == std::string::npos || dot == 0 ? file : file.substr(0, dot)));
    JS_SetPropertyUint32(ctx, arr, 2, new_string(ctx, dot == std::string::npos || dot == 0 ? std::string() : file.substr(dot)));
    return arr;
}

JSValue utils_SplitFilePath(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { return split_path(ctx, arg_str(ctx, argc, argv, 0)); }

enum class FileQuery { Exists, File, Directory, Size };

JSValue file_query(JSContext* ctx, const std::string& path, FileQuery q) {
    std::error_code ec;
    const auto p = fs_path(fix_path(path));
    switch (q) {
    case FileQuery::Exists: return JS_NewBool(ctx, std::filesystem::exists(p, ec));
    case FileQuery::File: return JS_NewBool(ctx, std::filesystem::is_regular_file(p, ec));
    case FileQuery::Directory: return JS_NewBool(ctx, std::filesystem::is_directory(p, ec));
    case FileQuery::Size: {
        const auto n = std::filesystem::file_size(p, ec);
        return JS_NewFloat64(ctx, ec ? 0.0 : (double)n);
    }
    }
    return JS_UNDEFINED;
}

template <FileQuery Q>
JSValue utils_file(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { return file_query(ctx, arg_str(ctx, argc, argv, 0), Q); }

JSValue utils_FileTest(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const std::string path = arg_str(ctx, argc, argv, 0), mode = arg_str(ctx, argc, argv, 1);
    if (mode == "e") return file_query(ctx, path, FileQuery::Exists);
    if (mode == "s") return file_query(ctx, path, FileQuery::Size);
    if (mode == "d") return file_query(ctx, path, FileQuery::Directory);
    if (mode == "split") return split_path(ctx, path);
    if (mode == "chardet") return JS_NewInt32(ctx, 65001);
    return JS_ThrowRangeError(ctx, "FileTest: unknown mode '%s'", mode.c_str());
}

JSValue utils_ReadTextFile(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string out;
    if (!I(ctx)->host.read_text_file(fix_path(arg_str(ctx, argc, argv, 0)), out)) return JS_NewString(ctx, "");
    return new_string(ctx, out);
}

JSValue utils_WriteTextFile(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (!writable(ctx, argc, argv, 0)) return JS_FALSE;
    return JS_NewBool(ctx, I(ctx)->host.write_text_file(fix_path(arg_str(ctx, argc, argv, 0)), arg_str(ctx, argc, argv, 1)));
}

bool wildcard(const char* p, const char* s) {
    for (; *p; ++p, ++s) {
        if (*p == '*') {
            for (const char* t = s;; ++t) {
                if (wildcard(p + 1, t)) return true;
                if (!*t) return false;
            }
        }
        if (!*s || (*p != '?' && std::tolower((unsigned char)*p) != std::tolower((unsigned char)*s))) return false;
    }
    return !*s;
}

JSValue utils_Glob(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    constexpr uint32_t kDirectory = 0x10;
    const std::string pattern = fix_path(arg_str(ctx, argc, argv, 0));
    const uint32_t exclude = arg_u32(ctx, argc, argv, 1, kDirectory), include = arg_u32(ctx, argc, argv, 2, 0xffffffffu);
    const auto cut = pattern.find_last_of("/\\");
    const std::string dir = cut == std::string::npos ? std::string() : pattern.substr(0, cut + 1);
    const std::string mask = cut == std::string::npos ? pattern : pattern.substr(cut + 1);
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(fs_path(dir.empty() ? std::string(".") : dir), ec), end; !ec && it != end; it.increment(ec)) {
        const bool isDir = it->is_directory(ec);
        const uint32_t attr = isDir ? kDirectory : 0x80;
        if ((attr & exclude) || !(attr & include)) continue;
        const std::string name = fs_utf8(it->path().filename());
        if (wildcard(mask.c_str(), name.c_str())) names.push_back(dir + name);
    }
    std::sort(names.begin(), names.end());
    JSValue arr = JS_NewArray(ctx);
    for (uint32_t i = 0; i < names.size(); ++i) JS_SetPropertyUint32(ctx, arr, i, new_string(ctx, names[i]));
    return arr;
}

JSValue utils_FormatDuration(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    long long t = (long long)std::max(0.0, std::floor(arg_num(ctx, argc, argv, 0)));
    const long long w = t / 604800, d = t / 86400 % 7, h = t / 3600 % 24, m = t / 60 % 60, sec = t % 60;
    char buf[64];
    std::string out;
    if (w) out += std::to_string(w) + "wk ";
    if (w || d) out += std::to_string(d) + "d ";
    if (w || d || h) snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", h, m, sec);
    else snprintf(buf, sizeof buf, "%lld:%02lld", m, sec);
    return new_string(ctx, out + buf);
}

JSValue utils_FormatFileSize(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    double n = std::max(0.0, arg_num(ctx, argc, argv, 0));
    static const char* const units[] = { "B", "KB", "MB", "GB", "TB" };
    int u = 0;
    while (n >= 1024 && u < 4) {
        n /= 1024;
        ++u;
    }
    char buf[64];
    if (u == 0) snprintf(buf, sizeof buf, "%.0f B", n);
    else snprintf(buf, sizeof buf, "%.2f %s", n, units[u]);
    return new_string(ctx, buf);
}

JSValue utils_zero(JSContext* ctx, JSValueConst, int, JSValueConst*) { return JS_NewInt32(ctx, 0); }

JSValue utils_InputBox(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    const std::string prompt = arg_str(ctx, argc, argv, 1), caption = arg_str(ctx, argc, argv, 2);
    const std::string def = arg_str(ctx, argc, argv, 3);
    std::string value = def;
    const bool ok = e->modal([&] { return e->host.input_box(prompt, caption, value); });
    if (ok) return new_string(ctx, value);
    if (arg_bool(ctx, argc, argv, 4)) return JS_ThrowInternalError(ctx, "Dialog window was closed");
    return new_string(ctx, def);
}

JSValue utils_GetAlbumArtV2(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    TrackPtr t = arg_track(ctx, argc, argv, 0);
    if (!t) return JS_EXCEPTION;
    std::string path;
    return new_bitmap(ctx, I(ctx)->host.album_art_sync(t, arg_int(ctx, argc, argv, 1, 0), path));
}

JSValue utils_GetAlbumArtAsyncV2(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    TrackPtr t = arg_track(ctx, argc, argv, 1);
    if (!t) return JS_EXCEPTION;
    JsEngine::Impl* e = I(ctx);
    int id = 0;
    JSValue promise = e->new_promise(id);
    if (JS_IsException(promise)) return promise;
    std::shared_ptr<bool> token = e->alive;
    e->host.album_art(t, arg_int(ctx, argc, argv, 2, 0), [token, e, id](pui::gfx::ImagePtr img, std::string path) {
        if (!*token) return;
        JSValue o = JS_NewObject(e->ctx);
        JS_SetPropertyStr(e->ctx, o, "image", new_bitmap(e->ctx, img));
        set_str(e->ctx, o, "path", path);
        e->settle_promise(id, o);
    });
    return promise;
}

const JSCFunctionListEntry kUtilsFuncs[] = {
    JS_CFUNC_DEF("GetAlbumArtAsync", 3, utils_GetAlbumArtAsync),
    JS_CFUNC_DEF("GetAlbumArtV2", 3, utils_GetAlbumArtV2),
    JS_CFUNC_DEF("GetAlbumArtAsyncV2", 3, utils_GetAlbumArtAsyncV2),
    JS_CFUNC_DEF("IsKeyPressed", 1, utils_IsKeyPressed),
    JS_CFUNC_DEF("CheckComponent", 2, utils_CheckComponent),
    JS_CFUNC_DEF("CheckFont", 1, utils_true),
    JS_CFUNC_DEF("SplitFilePath", 1, utils_SplitFilePath),
    JS_CFUNC_DEF("FileTest", 2, utils_FileTest),
    JS_CFUNC_DEF("IsFile", 1, utils_file<FileQuery::File>),
    JS_CFUNC_DEF("IsDirectory", 1, utils_file<FileQuery::Directory>),
    JS_CFUNC_DEF("FileExists", 1, utils_file<FileQuery::Exists>),
    JS_CFUNC_DEF("GetFileSize", 1, utils_file<FileQuery::Size>),
    JS_CFUNC_DEF("ReadTextFile", 2, utils_ReadTextFile),
    JS_CFUNC_DEF("WriteTextFile", 2, utils_WriteTextFile),
    JS_CFUNC_DEF("Glob", 3, utils_Glob),
    JS_CFUNC_DEF("FormatDuration", 1, utils_FormatDuration),
    JS_CFUNC_DEF("FormatFileSize", 1, utils_FormatFileSize),
    JS_CFUNC_DEF("GetSystemMetrics", 1, utils_zero),
    JS_CFUNC_DEF("InputBox", 5, utils_InputBox),
    JS_PROP_STRING_DEF("Version", "1.6.1", 0),
};

JSValue plman_PlaylistCount(JSContext* ctx, JSValueConst) { return JS_NewInt32(ctx, I(ctx)->host.playlist_count()); }
JSValue plman_ActivePlaylist(JSContext* ctx, JSValueConst) { return JS_NewInt32(ctx, I(ctx)->host.active_playlist()); }
JSValue plman_SetActivePlaylist(JSContext* ctx, JSValueConst, JSValueConst v) {
    int32_t p = -1;
    if (JS_ToInt32(ctx, &p, v) == 0) I(ctx)->host.set_active_playlist(p);
    return JS_UNDEFINED;
}
JSValue plman_PlayingPlaylist(JSContext* ctx, JSValueConst) { return JS_NewInt32(ctx, I(ctx)->host.playing_playlist()); }

JSValue plman_GetPlaylistName(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const std::string n = I(ctx)->host.playlist_name(arg_int(ctx, argc, argv, 0, -1));
    return JS_NewStringLen(ctx, n.data(), n.size());
}

JSValue plman_FindOrCreatePlaylist(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const bool unlocked = argc > 1 && JS_ToBool(ctx, argv[1]) > 0;
    return JS_NewInt32(ctx, I(ctx)->host.find_or_create_playlist(arg_str(ctx, argc, argv, 0), unlocked));
}

JSValue plman_ClearPlaylist(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.clear_playlist(arg_int(ctx, argc, argv, 0, -1));
    return JS_UNDEFINED;
}

JSValue plman_InsertPlaylistItems(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Tracks* l = arg_tracks(ctx, argc, argv, 2);
    if (!l) return JS_EXCEPTION;
    const bool select = argc > 3 && JS_ToBool(ctx, argv[3]) > 0;
    I(ctx)->host.insert_playlist_items(arg_int(ctx, argc, argv, 0, -1), arg_int(ctx, argc, argv, 1, 0), *l, select);
    return JS_UNDEFINED;
}

JSValue plman_ExecutePlaylistDefaultAction(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.execute_default_action(arg_int(ctx, argc, argv, 0, -1), arg_int(ctx, argc, argv, 1, 0));
    return JS_UNDEFINED;
}

JSValue plman_PlaylistItemCount(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewInt32(ctx, I(ctx)->host.playlist_item_count(arg_int(ctx, argc, argv, 0, -1)));
}

template <bool Selected>
JSValue plman_GetItems(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return new_list(ctx, I(ctx)->host.playlist_items(arg_int(ctx, argc, argv, 0, -1), Selected));
}

JSValue plman_IsPlaylistItemSelected(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const auto sel = I(ctx)->host.playlist_selection(arg_int(ctx, argc, argv, 0, -1));
    const int i = arg_int(ctx, argc, argv, 1, -1);
    return JS_NewBool(ctx, i >= 0 && i < (int)sel.size() && sel[(size_t)i]);
}

JSValue plman_SetPlaylistSelection(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.set_playlist_selection(arg_int(ctx, argc, argv, 0, -1), arg_ints(ctx, argc, argv, 1), arg_bool(ctx, argc, argv, 2));
    return JS_UNDEFINED;
}

JSValue plman_SetPlaylistSelectionSingle(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.set_playlist_selection(arg_int(ctx, argc, argv, 0, -1), { arg_int(ctx, argc, argv, 1, -1) }, arg_bool(ctx, argc, argv, 2));
    return JS_UNDEFINED;
}

JSValue plman_ClearPlaylistSelection(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.clear_playlist_selection(arg_int(ctx, argc, argv, 0, -1));
    return JS_UNDEFINED;
}

JSValue plman_RemovePlaylistSelection(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.remove_playlist_selection(arg_int(ctx, argc, argv, 0, -1), arg_bool(ctx, argc, argv, 1));
    return JS_UNDEFINED;
}

JSValue plman_MovePlaylistSelection(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.move_playlist_selection(arg_int(ctx, argc, argv, 0, -1), arg_int(ctx, argc, argv, 1));
    return JS_UNDEFINED;
}

JSValue plman_GetPlaylistFocusItemIndex(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewInt32(ctx, I(ctx)->host.playlist_focus(arg_int(ctx, argc, argv, 0, -1)));
}

JSValue plman_SetPlaylistFocusItem(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.set_playlist_focus(arg_int(ctx, argc, argv, 0, -1), arg_int(ctx, argc, argv, 1, -1));
    return JS_UNDEFINED;
}

JSValue plman_SetPlaylistFocusItemByHandle(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    TrackPtr t = arg_track(ctx, argc, argv, 1);
    if (!t) return JS_EXCEPTION;
    const int p = arg_int(ctx, argc, argv, 0, -1);
    const int i = index_in(ctx, I(ctx)->host.playlist_items(p, false), t);
    if (i >= 0) I(ctx)->host.set_playlist_focus(p, i);
    return JS_UNDEFINED;
}

JSValue plman_EnsurePlaylistItemVisible(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.ensure_visible(arg_int(ctx, argc, argv, 0, -1), arg_int(ctx, argc, argv, 1, -1));
    return JS_UNDEFINED;
}

JSValue plman_CreatePlaylist(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewInt32(ctx, I(ctx)->host.create_playlist(arg_int(ctx, argc, argv, 0, -1), arg_str(ctx, argc, argv, 1)));
}

JSValue plman_DuplicatePlaylist(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewInt32(ctx, I(ctx)->host.duplicate_playlist(arg_int(ctx, argc, argv, 0, -1), arg_str(ctx, argc, argv, 1)));
}

template <bool Switch>
JSValue plman_RemovePlaylist(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.remove_playlist(arg_int(ctx, argc, argv, 0, -1), Switch));
}

JSValue plman_RenamePlaylist(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.rename_playlist(arg_int(ctx, argc, argv, 0, -1), arg_str(ctx, argc, argv, 1)));
}

JSValue plman_MovePlaylist(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.move_playlist(arg_int(ctx, argc, argv, 0, -1), arg_int(ctx, argc, argv, 1, -1)));
}

JSValue plman_IsAutoPlaylist(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.playlist_is_auto(arg_int(ctx, argc, argv, 0, -1)));
}

JSValue plman_IsPlaylistLocked(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.playlist_is_locked(arg_int(ctx, argc, argv, 0, -1)));
}

JSValue plman_CreateAutoPlaylist(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewInt32(ctx, I(ctx)->host.create_autoplaylist(arg_int(ctx, argc, argv, 0, -1), arg_str(ctx, argc, argv, 1),
                                                             arg_str(ctx, argc, argv, 2), arg_str(ctx, argc, argv, 3),
                                                             arg_int(ctx, argc, argv, 4)));
}

JSValue plman_ShowAutoPlaylistUI(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.show_autoplaylist_ui(arg_int(ctx, argc, argv, 0, -1));
    return JS_UNDEFINED;
}

JSValue plman_PlaybackOrder(JSContext* ctx, JSValueConst) { return JS_NewInt32(ctx, I(ctx)->host.playback_order()); }
JSValue plman_SetPlaybackOrder(JSContext* ctx, JSValueConst, JSValueConst v) {
    int32_t o = 0;
    if (JS_ToInt32(ctx, &o, v) == 0) I(ctx)->host.set_playback_order(o);
    return JS_UNDEFINED;
}

JSValue plman_GetPlayingItemLocation(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    const ItemLocation loc = I(ctx)->host.playing_location();
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "IsValid", JS_NewBool(ctx, loc.playlist >= 0 && loc.item >= 0));
    set_int(ctx, o, "PlaylistIndex", loc.playlist);
    set_int(ctx, o, "PlaylistItemIndex", loc.item);
    return o;
}

JSValue plman_SortByFormat(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.sort_playlist(arg_int(ctx, argc, argv, 0, -1), arg_str(ctx, argc, argv, 1), arg_bool(ctx, argc, argv, 2)));
}

JSValue plman_SortByFormatV2(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsHost& h = I(ctx)->host;
    const int p = arg_int(ctx, argc, argv, 0, -1);
    if (p < 0 || p >= h.playlist_count()) return JS_FALSE;
    const Tracks items = h.playlist_items(p, false);
    return JS_NewBool(ctx, h.reorder_playlist(p, sorted_order(h.format_tracks(arg_str(ctx, argc, argv, 1), items), arg_int(ctx, argc, argv, 2, 1))));
}

JSValue plman_UndoBackup(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.undo_backup(arg_int(ctx, argc, argv, 0, -1));
    return JS_UNDEFINED;
}

JSValue plman_AddItemToPlaybackQueue(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    TrackPtr t = arg_track(ctx, argc, argv, 0);
    if (!t) return JS_EXCEPTION;
    I(ctx)->host.queue_add(t);
    return JS_UNDEFINED;
}

JSValue plman_AddPlaylistItemToPlaybackQueue(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.queue_add_playlist_item(arg_int(ctx, argc, argv, 0, -1), arg_int(ctx, argc, argv, 1, -1));
    return JS_UNDEFINED;
}

JSValue plman_FlushPlaybackQueue(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    I(ctx)->host.queue_flush();
    return JS_UNDEFINED;
}

JSValue plman_GetPlaybackQueueHandles(JSContext* ctx, JSValueConst, int, JSValueConst*) { return new_list(ctx, I(ctx)->host.queue_items()); }

const JSCFunctionListEntry kPlmanFuncs[] = {
    JS_CGETSET_DEF("PlaylistCount", plman_PlaylistCount, nullptr),
    JS_CGETSET_DEF("ActivePlaylist", plman_ActivePlaylist, plman_SetActivePlaylist),
    JS_CGETSET_DEF("PlayingPlaylist", plman_PlayingPlaylist, nullptr),
    JS_CFUNC_DEF("GetPlaylistName", 1, plman_GetPlaylistName),
    JS_CFUNC_DEF("FindOrCreatePlaylist", 2, plman_FindOrCreatePlaylist),
    JS_CFUNC_DEF("ClearPlaylist", 1, plman_ClearPlaylist),
    JS_CFUNC_DEF("InsertPlaylistItems", 4, plman_InsertPlaylistItems),
    JS_CFUNC_DEF("ExecutePlaylistDefaultAction", 2, plman_ExecutePlaylistDefaultAction),
    JS_CFUNC_DEF("PlaylistItemCount", 1, plman_PlaylistItemCount),
    JS_CFUNC_DEF("GetPlaylistItems", 1, plman_GetItems<false>),
    JS_CFUNC_DEF("GetPlaylistSelectedItems", 1, plman_GetItems<true>),
    JS_CFUNC_DEF("IsPlaylistItemSelected", 2, plman_IsPlaylistItemSelected),
    JS_CFUNC_DEF("SetPlaylistSelection", 3, plman_SetPlaylistSelection),
    JS_CFUNC_DEF("SetPlaylistSelectionSingle", 3, plman_SetPlaylistSelectionSingle),
    JS_CFUNC_DEF("ClearPlaylistSelection", 1, plman_ClearPlaylistSelection),
    JS_CFUNC_DEF("RemovePlaylistSelection", 2, plman_RemovePlaylistSelection),
    JS_CFUNC_DEF("MovePlaylistSelection", 2, plman_MovePlaylistSelection),
    JS_CFUNC_DEF("GetPlaylistFocusItemIndex", 1, plman_GetPlaylistFocusItemIndex),
    JS_CFUNC_DEF("SetPlaylistFocusItem", 2, plman_SetPlaylistFocusItem),
    JS_CFUNC_DEF("SetPlaylistFocusItemByHandle", 2, plman_SetPlaylistFocusItemByHandle),
    JS_CFUNC_DEF("EnsurePlaylistItemVisible", 2, plman_EnsurePlaylistItemVisible),
    JS_CFUNC_DEF("CreatePlaylist", 2, plman_CreatePlaylist),
    JS_CFUNC_DEF("DuplicatePlaylist", 2, plman_DuplicatePlaylist),
    JS_CFUNC_DEF("RemovePlaylist", 1, plman_RemovePlaylist<false>),
    JS_CFUNC_DEF("RemovePlaylistSwitch", 1, plman_RemovePlaylist<true>),
    JS_CFUNC_DEF("RenamePlaylist", 2, plman_RenamePlaylist),
    JS_CFUNC_DEF("MovePlaylist", 2, plman_MovePlaylist),
    JS_CFUNC_DEF("IsAutoPlaylist", 1, plman_IsAutoPlaylist),
    JS_CFUNC_DEF("IsPlaylistLocked", 1, plman_IsPlaylistLocked),
    JS_CFUNC_DEF("CreateAutoPlaylist", 5, plman_CreateAutoPlaylist),
    JS_CFUNC_DEF("ShowAutoPlaylistUI", 1, plman_ShowAutoPlaylistUI),
    JS_CGETSET_DEF("PlaybackOrder", plman_PlaybackOrder, plman_SetPlaybackOrder),
    JS_CFUNC_DEF("GetPlayingItemLocation", 0, plman_GetPlayingItemLocation),
    JS_CFUNC_DEF("SortByFormat", 3, plman_SortByFormat),
    JS_CFUNC_DEF("SortByFormatV2", 3, plman_SortByFormatV2),
    JS_CFUNC_DEF("UndoBackup", 1, plman_UndoBackup),
    JS_CFUNC_DEF("AddItemToPlaybackQueue", 1, plman_AddItemToPlaybackQueue),
    JS_CFUNC_DEF("AddPlaylistItemToPlaybackQueue", 2, plman_AddPlaylistItemToPlaybackQueue),
    JS_CFUNC_DEF("FlushPlaybackQueue", 0, plman_FlushPlaybackQueue),
    JS_CFUNC_DEF("GetPlaybackQueueHandles", 0, plman_GetPlaybackQueueHandles),
    JS_CFUNC_DEF("SetActivePlaylistContext", 0, noop),
};

const JSCFunctionListEntry kConsoleFuncs[] = {
    JS_CFUNC_DEF("log", 1, console_log),
};

JSValue window_Width(JSContext* ctx, JSValueConst) { return JS_NewInt32(ctx, I(ctx)->w); }
JSValue window_ID(JSContext* ctx, JSValueConst) { return JS_NewInt32(ctx, I(ctx)->windowId); }
JSValue window_Height(JSContext* ctx, JSValueConst) { return JS_NewInt32(ctx, I(ctx)->h); }

JSValue window_Repaint(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    I(ctx)->host.repaint();
    return JS_UNDEFINED;
}

JSValue window_RepaintRect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.repaint_rect(arg_rect(ctx, argc, argv, 0));
    return JS_UNDEFINED;
}

template <bool Repeat>
JSValue window_SetTimer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) return JS_ThrowTypeError(ctx, "the first argument must be a function");
    return JS_NewInt32(ctx, I(ctx)->add_timer(argv[0], std::max(10, arg_int(ctx, argc, argv, 1)), Repeat));
}

JSValue window_ClearTimer(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->clear_timer(arg_int(ctx, argc, argv, 0));
    return JS_UNDEFINED;
}

JSValue window_GetProperty(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    const std::string name = arg_str(ctx, argc, argv, 0);
    JSValueConst props = e->properties();
    JSAtom atom = JS_NewAtomLen(ctx, name.data(), name.size());
    JSValue out = JS_NULL;
    if (JS_GetOwnProperty(ctx, nullptr, props, atom) > 0) out = JS_GetProperty(ctx, props, atom);
    else if (argc > 1 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
        JS_SetProperty(ctx, props, atom, JS_DupValue(ctx, argv[1]));
        e->propsDirty = true;
        out = JS_DupValue(ctx, argv[1]);
    }
    JS_FreeAtom(ctx, atom);
    return out;
}

JSValue window_SetProperty(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    const std::string name = arg_str(ctx, argc, argv, 0);
    JSValueConst props = e->properties();
    JSAtom atom = JS_NewAtomLen(ctx, name.data(), name.size());
    if (argc < 2 || JS_IsUndefined(argv[1]) || JS_IsNull(argv[1])) JS_DeleteProperty(ctx, props, atom, 0);
    else JS_SetProperty(ctx, props, atom, JS_DupValue(ctx, argv[1]));
    JS_FreeAtom(ctx, atom);
    e->propsDirty = true;
    return JS_UNDEFINED;
}

std::string to_json(JSContext* ctx, JSValueConst v) {
    if (JS_IsUndefined(v)) return {};
    JSValue json = JS_JSONStringify(ctx, v, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(json)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return {};
    }
    std::string out = JS_IsString(json) ? to_string(ctx, json) : std::string();
    JS_FreeValue(ctx, json);
    return out;
}

JSValue from_json(JSContext* ctx, const std::string& json) {
    if (json.empty()) return JS_UNDEFINED;
    JSValue v = JS_ParseJSON(ctx, json.data(), json.size(), "<json>");
    if (JS_IsException(v)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return JS_UNDEFINED;
    }
    return v;
}

JSValue native_notify(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::vector<JsHost::NotifyItem> items;
    int64_t n = 0;
    if (argc > 2 && JS_IsArray(argv[2]) && JS_GetLength(ctx, argv[2], &n) == 0)
        for (int64_t i = 0; i < n; ++i) {
            JSValue v = JS_GetPropertyUint32(ctx, argv[2], (uint32_t)i);
            JsHost::NotifyItem item;
            if (TrackPtr* t = handle_of(v)) {
                item.tracks.push_back(*t);
                item.single = true;
            } else if (Tracks* l = list_of(v)) {
                item.tracks = *l;
            }
            items.push_back(std::move(item));
            JS_FreeValue(ctx, v);
        }
    I(ctx)->host.notify_others(arg_str(ctx, argc, argv, 0), arg_str(ctx, argc, argv, 1), items);
    return JS_UNDEFINED;
}

JSValue native_kind(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return new_string(ctx, "");
    return new_string(ctx, handle_of(argv[0]) ? "handle" : list_of(argv[0]) ? "list" : "");
}

template <void (JsHost::*Fn)()>
JSValue window_host(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    (I(ctx)->host.*Fn)();
    return JS_UNDEFINED;
}

JSValue window_IsVisible(JSContext* ctx, JSValueConst) { return JS_NewBool(ctx, I(ctx)->host.visible()); }
JSValue window_false(JSContext*, JSValueConst) { return JS_FALSE; }
JSValue window_InstanceType(JSContext* ctx, JSValueConst) { return JS_NewInt32(ctx, 1); }

template <int JsEngine::Impl::*Field>
JSValue window_GetInt(JSContext* ctx, JSValueConst) { return JS_NewInt32(ctx, I(ctx)->*Field); }
template <int JsEngine::Impl::*Field>
JSValue window_SetInt(JSContext* ctx, JSValueConst, JSValueConst v) {
    int32_t x = 0;
    if (JS_ToInt32(ctx, &x, v) == 0) I(ctx)->*Field = x;
    return JS_UNDEFINED;
}

std::string panel_name(JsEngine::Impl* e) { return e->scriptName.empty() ? std::string("JS Panel #") + std::to_string(e->windowId) : e->scriptName; }

JSValue window_Name(JSContext* ctx, JSValueConst) { return new_string(ctx, panel_name(I(ctx))); }

JSValue window_ScriptInfo(JSContext* ctx, JSValueConst) {
    JsEngine::Impl* e = I(ctx);
    JSValue o = JS_NewObject(ctx);
    set_str(ctx, o, "Name", panel_name(e));
    set_str(ctx, o, "Author", e->scriptAuthor);
    set_str(ctx, o, "Version", e->scriptVersion);
    return o;
}

JSValue window_DefineScript(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    e->scriptName = arg_str(ctx, argc, argv, 0);
    if (argc > 1 && JS_IsObject(argv[1])) {
        for (auto [key, field] : { std::pair{ "author", &e->scriptAuthor }, std::pair{ "version", &e->scriptVersion } }) {
            JSValue v = JS_GetPropertyStr(ctx, argv[1], key);
            if (!JS_IsUndefined(v)) *field = to_string(ctx, v);
            JS_FreeValue(ctx, v);
        }
    }
    return JS_UNDEFINED;
}

JSMemoryUsage memory(JSContext* ctx) {
    JSMemoryUsage m{};
    JS_ComputeMemoryUsage(JS_GetRuntime(ctx), &m);
    return m;
}

JSValue window_MemoryLimit(JSContext* ctx, JSValueConst) { return JS_NewFloat64(ctx, (double)JsEngine::kMemoryLimit); }
JSValue window_MemoryUsage(JSContext* ctx, JSValueConst) { return JS_NewFloat64(ctx, (double)memory(ctx).malloc_size); }

JSValue window_JsMemoryStats(JSContext* ctx, JSValueConst) {
    const double used = (double)memory(ctx).malloc_size;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "MemoryUsage", JS_NewFloat64(ctx, used));
    JS_SetPropertyStr(ctx, o, "TotalMemoryUsage", JS_NewFloat64(ctx, used));
    JS_SetPropertyStr(ctx, o, "TotalMemoryLimit", JS_NewFloat64(ctx, (double)JsEngine::kMemoryLimit));
    return o;
}

template <bool Cui>
JSValue window_GetColour(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    static const uint32_t dui[] = { 0xffe0e0e0, 0xff1e1e1e, 0xff6cb4ff, 0xff2f4f6f };
    static const uint32_t cui[] = { 0xffe0e0e0, 0xffffffff, 0xffe0e0e0, 0xff1e1e1e, 0xff2f4f6f, 0xff3a3a3a, 0xff6cb4ff };
    const int t = arg_int(ctx, argc, argv, 0);
    const uint32_t* table = Cui ? cui : dui;
    const int n = Cui ? (int)std::size(cui) : (int)std::size(dui);
    return JS_NewUint32(ctx, t >= 0 && t < n ? table[t] : table[0]);
}

JSValue window_GetFont(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue obj = JS_NewObjectClass(ctx, (int)g_fontClass);
    if (JS_IsException(obj)) return obj;
    JS_SetOpaque(obj, new FontSpec(font_spec(arg_int(ctx, argc, argv, 0) == 5 ? "Consolas" : "Segoe UI", 12, 0)));
    return obj;
}

const JSCFunctionListEntry kWindowFuncs[] = {
    JS_CGETSET_DEF("Width", window_Width, nullptr),
    JS_CGETSET_DEF("ID", window_ID, nullptr),
    JS_CGETSET_DEF("Height", window_Height, nullptr),
    JS_CFUNC_DEF("Repaint", 0, window_Repaint),
    JS_CFUNC_DEF("RepaintRect", 4, window_RepaintRect),
    JS_CFUNC_DEF("SetInterval", 2, window_SetTimer<true>),
    JS_CFUNC_DEF("SetTimeout", 2, window_SetTimer<false>),
    JS_CFUNC_DEF("ClearInterval", 1, window_ClearTimer),
    JS_CFUNC_DEF("ClearTimeout", 1, window_ClearTimer),
    JS_CFUNC_DEF("GetProperty", 2, window_GetProperty),
    JS_CFUNC_DEF("SetProperty", 2, window_SetProperty),
    JS_CFUNC_DEF("Reload", 0, window_host<&JsHost::reload_script>),
    JS_CFUNC_DEF("EditScript", 0, window_host<&JsHost::edit_script>),
    JS_CFUNC_DEF("ShowConfigure", 0, window_host<&JsHost::edit_script>),
    JS_CFUNC_DEF("ShowProperties", 0, noop),
    JS_CFUNC_DEF("SetCursor", 1, window_SetCursor),
    JS_CFUNC_DEF("CreatePopupMenu", 0, window_CreatePopupMenu),
    JS_CGETSET_DEF("IsVisible", window_IsVisible, nullptr),
    JS_CGETSET_DEF("IsTransparent", window_false, nullptr),
    JS_CGETSET_DEF("InstanceType", window_InstanceType, nullptr),
    JS_CGETSET_DEF("DlgCode", window_GetInt<&JsEngine::Impl::dlgCode>, window_SetInt<&JsEngine::Impl::dlgCode>),
    JS_CGETSET_DEF("MinWidth", window_GetInt<&JsEngine::Impl::minW>, window_SetInt<&JsEngine::Impl::minW>),
    JS_CGETSET_DEF("MinHeight", window_GetInt<&JsEngine::Impl::minH>, window_SetInt<&JsEngine::Impl::minH>),
    JS_CGETSET_DEF("MaxWidth", window_GetInt<&JsEngine::Impl::maxW>, window_SetInt<&JsEngine::Impl::maxW>),
    JS_CGETSET_DEF("MaxHeight", window_GetInt<&JsEngine::Impl::maxH>, window_SetInt<&JsEngine::Impl::maxH>),
    JS_CGETSET_DEF("ScriptInfo", window_ScriptInfo, nullptr),
    JS_CGETSET_DEF("Name", window_Name, nullptr),
    JS_CFUNC_DEF("DefineScript", 2, window_DefineScript),
    JS_CFUNC_DEF("DefinePanel", 2, window_DefineScript),
    JS_CGETSET_DEF("MemoryLimit", window_MemoryLimit, nullptr),
    JS_CGETSET_DEF("TotalMemoryUsage", window_MemoryUsage, nullptr),
    JS_CGETSET_DEF("PanelMemoryUsage", window_MemoryUsage, nullptr),
    JS_CGETSET_DEF("JsMemoryStats", window_JsMemoryStats, nullptr),
    JS_CFUNC_DEF("GetColourCUI", 1, window_GetColour<true>),
    JS_CFUNC_DEF("GetColourDUI", 1, window_GetColour<false>),
    JS_CFUNC_DEF("GetFontCUI", 1, window_GetFont),
    JS_CFUNC_DEF("GetFontDUI", 1, window_GetFont),
};

std::filesystem::path native_path(JSContext* ctx, int argc, JSValueConst* argv, int i) { return fs_path(fix_path(arg_str(ctx, argc, argv, i))); }


double epoch_ms(std::filesystem::file_time_type t) {
    using namespace std::chrono;
    const auto sys = time_point_cast<milliseconds>(t - std::filesystem::file_time_type::clock::now() + system_clock::now());
    return (double)sys.time_since_epoch().count();
}

JSValue entry_object(JSContext* ctx, const std::filesystem::path& p) {
    std::error_code ec;
    const auto st = std::filesystem::status(p, ec);
    if (ec || !std::filesystem::exists(st)) return JS_NULL;
    JSValue o = JS_NewObject(ctx);
    set_str(ctx, o, "name", fs_utf8(p.filename()));
    set_str(ctx, o, "path", fs_utf8(p));
    const bool dir = std::filesystem::is_directory(st);
    JS_SetPropertyStr(ctx, o, "dir", JS_NewBool(ctx, dir));
    const auto size = dir ? 0 : std::filesystem::file_size(p, ec);
    JS_SetPropertyStr(ctx, o, "size", JS_NewFloat64(ctx, ec ? 0.0 : (double)size));
    const auto mt = std::filesystem::last_write_time(p, ec);
    JS_SetPropertyStr(ctx, o, "mtime", JS_NewFloat64(ctx, ec ? 0.0 : epoch_ms(mt)));
    return o;
}

JSValue native_stat(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { return entry_object(ctx, native_path(ctx, argc, argv, 0)); }

JSValue native_list(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue arr = JS_NewArray(ctx);
    std::error_code ec;
    std::vector<std::filesystem::path> entries;
    for (std::filesystem::directory_iterator it(native_path(ctx, argc, argv, 0), ec), end; !ec && it != end; it.increment(ec)) entries.push_back(it->path());
    std::sort(entries.begin(), entries.end());
    uint32_t i = 0;
    for (const auto& e : entries) {
        JSValue o = entry_object(ctx, e);
        if (JS_IsNull(o)) continue;
        JS_SetPropertyUint32(ctx, arr, i++, o);
    }
    return arr;
}

JSValue native_mkdir(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (!writable(ctx, argc, argv, 0)) return JS_FALSE;
    std::error_code ec;
    return JS_NewBool(ctx, std::filesystem::create_directories(native_path(ctx, argc, argv, 0), ec) && !ec);
}

JSValue native_remove(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (!writable(ctx, argc, argv, 0)) return JS_FALSE;
    std::error_code ec;
    const auto p = native_path(ctx, argc, argv, 0);
    const bool done = arg_bool(ctx, argc, argv, 1) ? std::filesystem::remove_all(p, ec) > 0 : std::filesystem::remove(p, ec);
    return JS_NewBool(ctx, done && !ec);
}

JSValue native_rename(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (!writable(ctx, argc, argv, 0) || !writable(ctx, argc, argv, 1)) return JS_FALSE;
    std::error_code ec;
    std::filesystem::rename(native_path(ctx, argc, argv, 0), native_path(ctx, argc, argv, 1), ec);
    return JS_NewBool(ctx, !ec);
}

JSValue native_copy(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (!writable(ctx, argc, argv, 1)) return JS_FALSE;
    std::error_code ec;
    const auto opts = std::filesystem::copy_options::recursive |
                      (arg_bool(ctx, argc, argv, 2, true) ? std::filesystem::copy_options::overwrite_existing : std::filesystem::copy_options::skip_existing);
    std::filesystem::copy(native_path(ctx, argc, argv, 0), native_path(ctx, argc, argv, 1), opts, ec);
    return JS_NewBool(ctx, !ec);
}

JSValue native_readText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const std::string path = fix_path(arg_str(ctx, argc, argv, 0));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(fs_path(path), ec)) return JS_NULL;
    std::string out = read_file(path);
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) out.erase(0, 3);
    return new_string(ctx, out);
}

JSValue native_writeText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const std::string path = fix_path(arg_str(ctx, argc, argv, 0)), text = arg_str(ctx, argc, argv, 1);
    if (!writable(ctx, argc, argv, 0)) return JS_FALSE;
    FILE* f = fs_open(path, arg_bool(ctx, argc, argv, 2) ? "ab" : "wb");
    if (!f) return JS_FALSE;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    fclose(f);
    return JS_NewBool(ctx, ok);
}

HttpRequest http_args(JSContext* ctx, int argc, JSValueConst* argv) {
    HttpRequest r;
    r.method = arg_str(ctx, argc, argv, 0);
    for (char& c : r.method) c = (char)std::toupper((unsigned char)c);
    r.url = arg_str(ctx, argc, argv, 1);
    int64_t n = 0;
    if (argc > 2 && JS_IsArray(argv[2]) && JS_GetLength(ctx, argv[2], &n) == 0)
        for (int64_t i = 0; i < n; ++i) {
            JSValue v = JS_GetPropertyUint32(ctx, argv[2], (uint32_t)i);
            r.headers.push_back(to_string(ctx, v));
            JS_FreeValue(ctx, v);
        }
    if (argc > 3 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) r.body = to_string(ctx, argv[3]);
    return r;
}

JSValue response_object(JSContext* ctx, const HttpResponse& r) {
    JSValue o = JS_NewObject(ctx);
    set_int(ctx, o, "status", r.status);
    set_str(ctx, o, "text", r.text);
    set_str(ctx, o, "error", r.error);
    return o;
}

JSValue native_http(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    const HttpRequest req = http_args(ctx, argc, argv);
    return response_object(ctx, e->modal([&] { return e->host.http_request_sync(req); }));
}

JSValue native_httpAsync(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    int id = 0;
    JSValue promise = e->new_promise(id);
    if (JS_IsException(promise)) return promise;
    std::shared_ptr<bool> token = e->alive;
    e->host.http_request(http_args(ctx, argc, argv), [token, e, id](HttpResponse r) {
        if (*token) e->settle_promise(id, response_object(e->ctx, r));
    });
    return promise;
}

JSValue native_download(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    int id = 0;
    JSValue promise = e->new_promise(id);
    if (JS_IsException(promise)) return promise;
    const std::string path = fix_path(arg_str(ctx, argc, argv, 1));
    if (!writable(ctx, argc, argv, 1)) {
        e->settle_promise(id, JS_FALSE);
        return promise;
    }
    std::shared_ptr<bool> token = e->alive;
    e->host.http_request(HttpRequest{ "GET", arg_str(ctx, argc, argv, 0), "", {} }, [token, e, id, path](HttpResponse r) {
        bool ok = false;
        if (r.status == 200 && !r.text.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(fs_path(path).parent_path(), ec);
            if (FILE* f = fs_open(path, "wb")) {
                ok = fwrite(r.text.data(), 1, r.text.size(), f) == r.text.size();
                fclose(f);
            }
        }
        if (*token) e->settle_promise(id, JS_NewBool(e->ctx, ok));
    });
    return promise;
}

JSValue native_shellRun(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.shell_run(fix_path(arg_str(ctx, argc, argv, 0))));
}

JSValue native_clipboard(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc > 0 && !JS_IsUndefined(argv[0])) return JS_NewBool(ctx, I(ctx)->host.set_clipboard_text(arg_str(ctx, argc, argv, 0)));
    return new_string(ctx, I(ctx)->host.clipboard_text());
}

JSValue native_messageBox(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    const std::string text = arg_str(ctx, argc, argv, 0), caption = arg_str(ctx, argc, argv, 1);
    const int type = arg_int(ctx, argc, argv, 2);
    return JS_NewInt32(ctx, e->modal([&] { return e->host.message_box(text, caption, type); }));
}

JSValue native_fixPath(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) { return new_string(ctx, fix_path(arg_str(ctx, argc, argv, 0))); }

JSValue native_windowState(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc > 0 && !JS_IsUndefined(argv[0])) I(ctx)->host.set_window_state(std::clamp(arg_int(ctx, argc, argv, 0), 0, 2));
    return JS_NewInt32(ctx, I(ctx)->host.window_state());
}

JSValue native_fullscreen(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc > 0 && !JS_IsUndefined(argv[0])) I(ctx)->host.set_fullscreen(arg_bool(ctx, argc, argv, 0));
    return JS_NewBool(ctx, I(ctx)->host.fullscreen());
}

JSValue native_windowLimits(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.set_window_limits(arg_int(ctx, argc, argv, 0), arg_int(ctx, argc, argv, 1), arg_int(ctx, argc, argv, 2), arg_int(ctx, argc, argv, 3));
    return JS_UNDEFINED;
}

JSValue native_caption(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.set_caption(arg_rect(ctx, argc, argv, 0));
    return JS_UNDEFINED;
}

JSValue pui_IsAvailable(JSContext* ctx, JSValueConst) { return JS_NewBool(ctx, I(ctx)->host.panels_ui_available()); }

JSValue pui_GetVariable(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string value;
    if (!I(ctx)->host.panels_ui_variable(arg_str(ctx, argc, argv, 0), value)) return argc > 1 ? JS_DupValue(ctx, argv[1]) : new_string(ctx, "");
    return new_string(ctx, value);
}

JSValue pui_SetVariable(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    I(ctx)->host.set_panels_ui_variable(arg_str(ctx, argc, argv, 0), arg_str(ctx, argc, argv, 1));
    return JS_UNDEFINED;
}

JSValue pui_RunAction(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, I(ctx)->host.panels_ui_action(arg_str(ctx, argc, argv, 0)));
}

const JSCFunctionListEntry kPanelsUiFuncs[] = {
    JS_CGETSET_DEF("IsAvailable", pui_IsAvailable, nullptr),
    JS_CFUNC_DEF("GetVariable", 2, pui_GetVariable),
    JS_CFUNC_DEF("SetVariable", 2, pui_SetVariable),
    JS_CFUNC_DEF("RunAction", 1, pui_RunAction),
};

JSValue native_selection(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    const int mode = arg_int(ctx, argc, argv, 0);
    Tracks tracks;
    if (argc > 1 && handle_of(argv[1])) tracks.push_back(*handle_of(argv[1]));
    else if (argc > 1 && list_of(argv[1])) tracks = *list_of(argv[1]);
    I(ctx)->host.ui_selection(static_cast<JsHost::SelectionMode>(std::clamp(mode, 0, 3)), tracks);
    return JS_UNDEFINED;
}

JSValue fb_GetSelection(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    Tracks t = I(ctx)->host.get_ui_selection();
    return t.empty() ? JS_NULL : new_handle(ctx, t.front());
}

JSValue fb_GetSelections(JSContext* ctx, JSValueConst, int, JSValueConst*) { return new_list(ctx, I(ctx)->host.get_ui_selection()); }

const JSCFunctionListEntry kNativeFuncs[] = {
    JS_CFUNC_DEF("stat", 1, native_stat),
    JS_CFUNC_DEF("list", 1, native_list),
    JS_CFUNC_DEF("mkdir", 1, native_mkdir),
    JS_CFUNC_DEF("remove", 2, native_remove),
    JS_CFUNC_DEF("rename", 2, native_rename),
    JS_CFUNC_DEF("copy", 3, native_copy),
    JS_CFUNC_DEF("readText", 1, native_readText),
    JS_CFUNC_DEF("writeText", 3, native_writeText),
    JS_CFUNC_DEF("http", 4, native_http),
    JS_CFUNC_DEF("httpAsync", 4, native_httpAsync),
    JS_CFUNC_DEF("download", 2, native_download),
    JS_CFUNC_DEF("shellRun", 1, native_shellRun),
    JS_CFUNC_DEF("clipboard", 1, native_clipboard),
    JS_CFUNC_DEF("messageBox", 3, native_messageBox),
    JS_CFUNC_DEF("fixPath", 1, native_fixPath),
    JS_CFUNC_DEF("windowState", 1, native_windowState),
    JS_CFUNC_DEF("notify", 3, native_notify),
    JS_CFUNC_DEF("kind", 1, native_kind),
    JS_CFUNC_DEF("fullscreen", 1, native_fullscreen),
    JS_CFUNC_DEF("windowLimits", 4, native_windowLimits),
    JS_CFUNC_DEF("caption", 4, native_caption),
    JS_CFUNC_DEF("selection", 2, native_selection),
    JS_CFUNC_DEF("getSelection", 0, fb_GetSelection),
    JS_CFUNC_DEF("getSelections", 0, fb_GetSelections),
#ifdef _WIN32
    JS_PROP_STRING_DEF("os", "windows", 0),
#else
    JS_PROP_STRING_DEF("os", "mac", 0),
#endif
};

const char* const kPrelude = R"JS(
(function () {
    const tooltip = { Text: '', TrackActivate: false, Width: 0, Height: 0, Activate() {}, Deactivate() {},
                      SetMaxWidth() {}, TrackPosition() {}, GetDelayTime() { return 0; }, SetDelayTime() {}, SetFont() {} };
    window.CreateTooltip = function () { return tooltip; };
    Object.defineProperty(window, 'Tooltip', { get() { return tooltip; } });
    fb.CreateProfiler = function (name) {
        let start = Date.now();
        return {
            Name: name || '',
            get Time() { return Date.now() - start; },
            Reset() { start = Date.now(); },
            Print(msg) { console.log('FbProfiler (' + this.Name + '): ' + (msg ? msg + ' ' : '') + this.Time + ' ms'); },
        };
    };
})();
(function () {
    const native = __jsp;
    const pack = (v, items, depth) => {
        if (depth > 32) return null;
        if (v && typeof v === 'object') {
            if (native.kind(v)) {
                items.push(v);
                return { __jspItem: items.length - 1 };
            }
            if (Array.isArray(v)) return v.map(e => pack(e, items, depth + 1));
            const o = {};
            for (const k of Object.keys(v)) o[k] = pack(v[k], items, depth + 1);
            return o;
        }
        return v;
    };
    const unpack = (v, items) => {
        if (v && typeof v === 'object') {
            if (typeof v.__jspItem === 'number' && Object.keys(v).length === 1) return items[v.__jspItem];
            if (Array.isArray(v)) return v.map(e => unpack(e, items));
            for (const k of Object.keys(v)) v[k] = unpack(v[k], items);
        }
        return v;
    };
    window.NotifyOthers = function (name, info) {
        const items = [];
        const packed = pack(info, items, 0);
        let json = '';
        try { json = packed === undefined ? '' : JSON.stringify(packed); } catch (e) { json = ''; }
        native.notify(String(name), json === undefined ? '' : json, items);
    };
    native.unpack = unpack;
})();
(function () {
    const wrap = (set) => (fn, delay, ...args) => {
        if (typeof fn !== 'function') { const code = String(fn); fn = () => (0, eval)(code); }
        return set(args.length ? () => fn(...args) : fn, delay | 0);
    };
    globalThis.setTimeout = wrap((f, d) => window.SetTimeout(f, d));
    globalThis.setInterval = wrap((f, d) => window.SetInterval(f, d));
    globalThis.clearTimeout = id => window.ClearTimeout(id);
    globalThis.clearInterval = id => window.ClearInterval(id);
    window.CreateThemeManager = function (classList) {
        return {
            part: 0, state: 0,
            SetPartAndStateID(p, s) { this.part = p; this.state = s; },
            SetPartAndStateId(p, s) { this.part = p; this.state = s; },
            IsThemePartDefined: () => true,
            GetThemeColour: () => 0xff000000,
            DrawThemeBackground(gr, x, y, w, h) {
                if (!/treeview/i.test(String(classList)) || this.part !== 2) return;
                const s = Math.max(4, Math.min(w, h) * 0.45), cx = x + w / 2, cy = y + h / 2;
                const pts = this.state === 2 ? [cx - s / 2, cy - s / 4, cx + s / 2, cy - s / 4, cx, cy + s / 3]
                                             : [cx - s / 4, cy - s / 2, cx + s / 3, cy, cx - s / 4, cy + s / 2];
                gr.FillPolygon(0xff808080, 0, pts);
            },
        };
    };
    utils.ShowHtmlDialog = function (id, code, options) {
        const data = (options && options.data) || [];
        const callback = data.find(d => typeof d === 'function');
        const html = String(code);
        const plain = s => String(s === undefined || s === null ? '' : s).replace(/<br\s*\/?>/gi, '\n').replace(/<[^>]*>/g, '')
            .replace(/&nbsp;/g, ' ').replace(/&lt;/g, '<').replace(/&gt;/g, '>').replace(/&quot;/g, '"').replace(/&amp;/g, '&');
        const title = plain(data[0]) || 'JS Panel', text = plain(data[1]);
        if (/input_container|radio_container/.test(html)) {
            console.log('ShowHtmlDialog: "' + title + '" is an HTML form, which JS Panel can\'t show; answered Cancel');
            if (callback) callback('cancel', false);
            return;
        }
        if (/btn_later/.test(html)) {
            const r = __jsp.messageBox(text, title, 1);
            if (callback) r === 1 ? callback('ok', data[5]) : callback('never', null);
            return;
        }
        if (/btn_cancel/.test(html)) {
            const r = __jsp.messageBox(text, title, 4);
            if (callback) callback('ok', r === 6);
            return;
        }
        __jsp.messageBox(text, title, 0);
        if (callback) callback('ok', true);
    };
    globalThis.FbTitleFormat = function (expr) { return fb.TitleFormat(expr); };
    globalThis.FbProfiler = function (name) { return fb.CreateProfiler(name); };
    fb.AcquireUiSelectionHolder = function () {
        return {
            SetSelection(handles) { __jsp.selection(0, handles); },
            SetPlaylistSelectionTracking() { __jsp.selection(1); },
            SetPlaylistTracking() { __jsp.selection(2); },
            Dispose() { __jsp.selection(3); },
        };
    };
    fb.GetSelection = () => __jsp.getSelection();
    fb.GetSelections = () => __jsp.getSelections();
    if (typeof globalThis.Intl === 'undefined') {
        const months = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
        const two = n => (n < 10 ? '0' : '') + n;
        const DateTimeFormat = function (locale, options) {
            if (!(this instanceof DateTimeFormat)) return new DateTimeFormat(locale, options);
            const o = options || { year: 'numeric', month: 'numeric', day: 'numeric' };
            this.format = date => {
                const d = date === undefined ? new Date() : new Date(date);
                const parts = [];
                if (o.day) parts.push(o.day === '2-digit' ? two(d.getDate()) : String(d.getDate()));
                if (o.month) parts.push(o.month === 'long' ? months[d.getMonth()] : o.month === 'short' ? months[d.getMonth()].slice(0, 3)
                                       : o.month === '2-digit' ? two(d.getMonth() + 1) : String(d.getMonth() + 1));
                if (o.year) parts.push(o.year === '2-digit' ? two(d.getFullYear() % 100) : String(d.getFullYear()));
                return parts.join(o.month === 'long' || o.month === 'short' ? ' ' : '/');
            };
            this.resolvedOptions = () => ({ locale: locale || 'en-GB', ...o });
        };
        const NumberFormat = function (locale, options) {
            if (!(this instanceof NumberFormat)) return new NumberFormat(locale, options);
            const o = options || {};
            this.format = n => {
                const digits = o.maximumFractionDigits !== undefined ? o.maximumFractionDigits : 3;
                const [int, frac] = Number(n).toFixed(digits).replace(/(\.\d*?)0+$/, '$1').replace(/\.$/, '').split('.');
                return int.replace(/\B(?=(\d{3})+(?!\d))/g, ',') + (frac ? '.' + frac : '');
            };
        };
        const Collator = function (locale, options) {
            if (!(this instanceof Collator)) return new Collator(locale, options);
            const numeric = !!(options && options.numeric);
            const split = s => String(s).toLowerCase().match(numeric ? /\d+|\D+/g : /[\s\S]+/g) || [];
            this.compare = (a, b) => {
                const x = split(a), y = split(b);
                for (let i = 0; i < Math.min(x.length, y.length); i++) {
                    if (x[i] === y[i]) continue;
                    const nx = numeric && /^\d/.test(x[i]) && /^\d/.test(y[i]);
                    return nx ? Number(x[i]) - Number(y[i]) : x[i] < y[i] ? -1 : 1;
                }
                return x.length - y.length;
            };
        };
        globalThis.Intl = { DateTimeFormat, NumberFormat, Collator };
    }
    globalThis.FbMetadbHandleList = function (from) {
        const list = fb.CreateHandleList();
        if (from === undefined || from === null) return list;
        if (Array.isArray(from)) from.forEach(h => list.Add(h));
        else if (typeof from.Count === 'number') list.AddRange(from);
        else list.Add(from);
        return list;
    };
})();
(function () {
    const native = __jsp;
    const fail = (message, number) => { const e = new Error(message); e.number = number; throw e; };
    const sep = p => Math.max(p.lastIndexOf('\\'), p.lastIndexOf('/'));
    const fileName = p => { p = String(p).replace(/[\\/]+$/, ''); return p.slice(sep(p) + 1); };
    const parentName = p => { p = String(p).replace(/[\\/]+$/, ''); const i = sep(p); return i < 0 ? '' : p.slice(0, i); };
    const extName = p => { const n = fileName(p), i = n.lastIndexOf('.'); return i > 0 ? n.slice(i + 1) : ''; };
    const baseName = p => { const n = fileName(p), i = n.lastIndexOf('.'); return i > 0 ? n.slice(0, i) : n; };
    const joinPath = (a, b) => { a = String(a); return a === '' || /[\\/]$/.test(a) ? a + b : a + (native.os === 'windows' ? '\\' : '/') + b; };
    const collection = items => { const c = items.slice(); c.Count = c.length; c.Item = i => c[i]; return c; };
    const hasWildcard = p => /[*?]/.test(fileName(p));
    const matches = (path, wantDir) => {
        if (!hasWildcard(path)) { const st = native.stat(path); return st && st.dir === wantDir ? [path] : []; }
        const dir = parentName(path), re = new RegExp('^' + fileName(path).replace(/[.+^${}()|[\]\\]/g, '\\$&').replace(/\*/g, '.*').replace(/\?/g, '.') + '$', 'i');
        return native.list(dir).filter(e => e.dir === wantDir && re.test(e.name)).map(e => joinPath(dir, e.name));
    };

    class FsoFile {
        constructor(path, st) { this.Path = String(path); this.Name = fileName(path); this.Size = st.size; this.DateLastModified = new Date(st.mtime);
                                this.DateLastAccessed = new Date(st.mtime); this.DateCreated = new Date(st.mtime); this.Type = extName(path).toUpperCase() + ' File'; }
        get ParentFolder() { return fso.GetFolder(parentName(this.Path)); }
    }
    class FsoFolder {
        constructor(path) { this.Path = String(path).replace(/[\\/]+$/, '') || String(path); this.Name = fileName(path); }
        get Files() { return collection(native.list(this.Path).filter(e => !e.dir).map(e => new FsoFile(joinPath(this.Path, e.name), e))); }
        get SubFolders() { return collection(native.list(this.Path).filter(e => e.dir).map(e => new FsoFolder(joinPath(this.Path, e.name)))); }
        get Size() { return this.Files.reduce((n, f) => n + f.Size, 0); }
        get ParentFolder() { return new FsoFolder(parentName(this.Path)); }
        get IsRootFolder() { return parentName(this.Path) === ''; }
    }
    class TextStream {
        constructor(path, mode, text) { this.path = path; this.mode = mode; this.text = text || ''; this.pos = 0; this.Line = 1; }
        get AtEndOfStream() { return this.pos >= this.text.length; }
        ReadAll() { const r = this.text.slice(this.pos); this.pos = this.text.length; return r; }
        ReadLine() {
            let end = this.text.indexOf('\n', this.pos);
            if (end < 0) end = this.text.length;
            const line = this.text.slice(this.pos, end).replace(/\r$/, '');
            this.pos = end + 1;
            this.Line++;
            return line;
        }
        Read(n) { const r = this.text.slice(this.pos, this.pos + n); this.pos += r.length; return r; }
        SkipLine() { this.ReadLine(); }
        Write(t) { if (this.mode === 1) fail('Bad file mode', -2146828234); native.writeText(this.path, String(t), true); }
        WriteLine(t) { this.Write((t === undefined ? '' : t) + '\r\n'); }
        WriteBlankLines(n) { for (let i = 0; i < n; i++) this.WriteLine(); }
        Close() {}
    }
    const fso = {
        FileExists: p => { const st = native.stat(p); return !!st && !st.dir; },
        FolderExists: p => { const st = native.stat(p); return !!st && st.dir; },
        DriveExists: () => true,
        CreateFolder(p) { if (native.stat(p)) fail('File already exists', -2146828230); native.mkdir(p); return new FsoFolder(p); },
        DeleteFile(p) { const list = matches(p, false); if (!list.length) fail('File not found', -2146828235); list.forEach(f => native.remove(f, false)); },
        DeleteFolder(p) { const list = matches(p, true); if (!list.length) fail('Path not found', -2146828212); list.forEach(f => native.remove(f, true)); },
        MoveFile(a, b) { if (!native.rename(a, /[\\/]$/.test(b) ? joinPath(b, fileName(a)) : b)) fail('Permission denied', -2146828218); },
        MoveFolder(a, b) { this.MoveFile(a, b); },
        CopyFile(a, b, overwrite) { if (!native.copy(a, /[\\/]$/.test(b) ? joinPath(b, fileName(a)) : b, overwrite !== false)) fail('Permission denied', -2146828218); },
        CopyFolder(a, b, overwrite) { this.CopyFile(a, b, overwrite); },
        GetFile(p) { const st = native.stat(p); if (!st || st.dir) fail('File not found', -2146828235); return new FsoFile(p, st); },
        GetFolder(p) { const st = native.stat(p); if (!st || !st.dir) fail('Path not found', -2146828212); return new FsoFolder(p); },
        GetFileName: fileName,
        GetBaseName: baseName,
        GetExtensionName: extName,
        GetParentFolderName: parentName,
        GetAbsolutePathName: p => String(p),
        BuildPath: joinPath,
        GetSpecialFolder: () => new FsoFolder(fb.ProfilePath),
        GetTempName: () => 'rad' + Math.random().toString(16).slice(2, 7) + '.tmp',
        CreateTextFile(p, overwrite) {
            if (overwrite === false && native.stat(p)) fail('File already exists', -2146828230);
            native.writeText(p, '', false);
            return new TextStream(p, 2);
        },
        OpenTextFile(p, mode, create) {
            mode = mode || 1;
            const st = native.stat(p);
            if (!st && (mode === 1 || !create)) fail('File not found', -2146828235);
            if (mode === 1) return new TextStream(p, 1, native.readText(p));
            if (mode === 2 || !st) native.writeText(p, '', false);
            return new TextStream(p, mode);
        },
    };

    const registry = {
        'hklm\\software\\microsoft\\windows nt\\currentversion\\currentmajorversionnumber': 10,
        'hklm\\software\\microsoft\\windows nt\\currentversion\\currentminorversionnumber': 0,
        'hkcu\\control panel\\desktop\\windowmetrics\\applieddpi': 96,
        'hkcu\\software\\microsoft\\windows\\currentversion\\themes\\personalize\\appsuselighttheme': 0,
    };
    const shell = () => ({
        Run(command, style, wait) {
            const vbs = /^\s*cscript(?:\.exe)?\s+(?:\/\/\S+\s+)*"([^"]+\.vbs)"\s+"([^"]+)"\s+"([^"]+)"\s*$/i.exec(String(command));
            if (vbs && /XMLHTTP/i.test(native.readText(vbs[1])) && /SaveToFile/i.test(native.readText(vbs[1]))) {
                if (!native.stat(vbs[3])) native.download(vbs[2], vbs[3]);
                return 0;
            }
            if (!native.shellRun(String(command))) console.log('WScript.Shell.Run: can\'t run ' + command);
            return 0;
        },
        Exec(command) { this.Run(command); return { Status: 1, ExitCode: 0, StdOut: { ReadAll: () => '', AtEndOfStream: true } }; },
        RegRead(key) {
            const k = String(key).toLowerCase().replace(/^hkey_local_machine/, 'hklm').replace(/^hkey_current_user/, 'hkcu');
            if (k in registry) return registry[k];
            fail('Unable to open registry key "' + key + '" for reading.', -2147024894);
        },
        RegWrite() {},
        Popup(text, seconds, title, type) { return native.messageBox(String(text), title === undefined ? 'Windows Script Host' : String(title), type | 0); },
        ExpandEnvironmentStrings: s => String(s).replace(/%APPDATA%/ig, parentName(fb.ProfilePath)),
        SpecialFolders: () => '',
        CreateShortcut() { fail('CreateShortcut is not available in JS Panel', -2146827859); },
    });

    class Http {
        constructor() { this.readyState = 0; this.status = 0; this.statusText = ''; this.responseText = ''; this.onreadystatechange = null; this.headers = []; }
        open(method, url, async) { this.method = String(method); this.url = String(url); this.async = async !== false; this.headers = []; this.aborted = false; this.readyState = 1; }
        setRequestHeader(k, v) { this.headers.push(k + ': ' + v); }
        send(body) {
            const finish = r => {
                if (this.aborted) return;
                this.status = r.status;
                this.statusText = r.error || (r.status >= 200 && r.status < 300 ? 'OK' : '');
                this.responseText = r.text;
                this.readyState = 4;
                if (typeof this.onreadystatechange === 'function') this.onreadystatechange();
            };
            this.readyState = 2;
            if (this.async) native.httpAsync(this.method, this.url, this.headers, body).then(finish);
            else finish(native.http(this.method, this.url, this.headers, body));
        }
        abort() { this.aborted = true; this.readyState = 0; }
        getResponseHeader() { return ''; }
        getAllResponseHeaders() { return ''; }
        Open(m, u, a) { this.open(m, u, a === true); }
        Send(b) { this.send(b); }
        SetRequestHeader(k, v) { this.setRequestHeader(k, v); }
        SetTimeouts() {}
        Option() {}
        WaitForResponse() { return this.readyState === 4; }
        Abort() { this.abort(); }
        get Status() { return this.status; }
        get StatusText() { return this.statusText; }
        get ResponseText() { return this.responseText; }
    }

    const voidTags = new Set(['AREA', 'BASE', 'BR', 'COL', 'EMBED', 'HR', 'IMG', 'INPUT', 'LINK', 'META', 'PARAM', 'SOURCE', 'TRACK', 'WBR']);
    const blockTags = ['P', 'DIV', 'UL', 'OL', 'DL', 'TABLE', 'H1', 'H2', 'H3', 'H4', 'H5', 'H6', 'SECTION', 'HEADER', 'FOOTER', 'BLOCKQUOTE', 'PRE', 'FORM', 'HR'];
    const implicitClose = { LI: ['LI', 'P'], DT: ['DT', 'DD', 'P'], DD: ['DT', 'DD', 'P'], TR: ['TR', 'TD', 'TH'], TD: ['TD', 'TH'], TH: ['TD', 'TH'], OPTION: ['OPTION'] };
    for (const t of blockTags) implicitClose[t] = [...(implicitClose[t] || []), 'P'];
    const entities = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'", nbsp: ' ' };
    const decode = t => t.replace(/&(#x[0-9a-f]+|#\d+|[a-z]+);/gi, (m, e) => e[0] === '#'
        ? String.fromCodePoint(e[1] === 'x' || e[1] === 'X' ? parseInt(e.slice(2), 16) : parseInt(e.slice(1), 10))
        : entities[e.toLowerCase()] ?? m);
    const tagRe = /<!--[\s\S]*?-->|<![^>]*>|<\?[^>]*>|<(\/?)([a-zA-Z][^\s\/>]*)((?:\s*[^\s=\/>]+(?:\s*=\s*(?:"[^"]*"|'[^']*'|[^\s>]+))?)*)\s*(\/?)>/g;
    const attrRe = /([^\s=\/>]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))?/g;
    class HtmlElement {
        constructor(tag) { this.tagName = this.nodeName = tag.toUpperCase(); this.attrs = {}; this.children = []; this.parentNode = null; this.src_ = ''; this.a_ = 0; this.b_ = 0; this.style = {}; }
        get childNodes() { return this.children; }
        get firstChild() { return this.children[0] || null; }
        get className() { return this.attrs['class'] || ''; }
        get id() { return this.attrs.id || ''; }
        get title() { return this.attrs.title || ''; }
        get src() { return this.attrs.src || ''; }
        get href() { const h = this.attrs.href || ''; return h[0] === '/' && h[1] !== '/' ? 'about:' + h : h; }
        getAttribute(n) { const v = this.attrs[String(n).toLowerCase()]; return v === undefined ? null : v; }
        setAttribute(n, v) { this.attrs[String(n).toLowerCase()] = String(v); }
        get innerHTML() { return this.src_.slice(this.a_, this.b_); }
        set innerHTML(html) {
            html = String(html);
            this.children = []; this.src_ = html; this.a_ = 0; this.b_ = html.length;
            const stack = [this];
            const raw = /^(SCRIPT|STYLE|TEXTAREA|TITLE)$/;
            tagRe.lastIndex = 0;
            let m;
            while ((m = tagRe.exec(html))) {
                if (!m[2]) continue;
                const tag = m[2].toUpperCase();
                if (m[1]) {
                    const i = stack.map(e => e.tagName).lastIndexOf(tag);
                    if (i < 1) continue;
                    while (stack.length > i) stack.pop().b_ = m.index;
                    continue;
                }
                for (let top = stack[stack.length - 1]; stack.length > 1 && implicitClose[tag]?.includes(top.tagName); top = stack[stack.length - 1])
                    stack.pop().b_ = m.index;
                const el = new HtmlElement(tag);
                el.src_ = html; el.a_ = el.b_ = tagRe.lastIndex;
                attrRe.lastIndex = 0;
                let a;
                while ((a = attrRe.exec(m[3]))) el.attrs[a[1].toLowerCase()] = decode(a[2] ?? a[3] ?? a[4] ?? '');
                const parent = stack[stack.length - 1];
                el.parentNode = parent;
                parent.children.push(el);
                if (raw.test(tag)) {
                    const end = html.toLowerCase().indexOf('</' + tag.toLowerCase(), tagRe.lastIndex);
                    el.b_ = end < 0 ? html.length : end;
                    if (end >= 0) tagRe.lastIndex = end;
                    continue;
                }
                if (!m[4] && !voidTags.has(tag)) stack.push(el);
            }
            while (stack.length > 1) stack.pop().b_ = html.length;
        }
        get innerText() {
            return decode(this.innerHTML.replace(/<(script|style)\b[\s\S]*?<\/\1\s*>/gi, '').replace(/<br\s*\/?>/gi, '\n')
                .replace(/<\/(p|div|li|h[1-6]|tr|dt|dd)\s*>/gi, '\n').replace(/<[^>]*>/g, '')).replace(/[ \t\r\f]+/g, ' ').replace(/ *\n */g, '\n');
        }
        set innerText(t) { this.innerHTML = String(t).replace(/&/g, '&amp;').replace(/</g, '&lt;'); }
        get textContent() { return this.innerText; }
        set textContent(t) { this.innerText = t; }
        get outerHTML() { return this.parentNode ? this.src_.slice(this.src_.lastIndexOf('<', this.a_ - 1), this.b_) : this.innerHTML; }
        getElementsByTagName(name) {
            const want = String(name).toUpperCase(), out = [];
            const walk = e => { for (const c of e.children) { if (want === '*' || c.tagName === want) out.push(c); walk(c); } };
            walk(this);
            return out;
        }
        getElementsByClassName(name) {
            const want = String(name).split(/\s+/).filter(Boolean);
            return this.getElementsByTagName('*').filter(e => want.every(w => e.className.split(/\s+/).includes(w)));
        }
        getElementById(id) { return this.getElementsByTagName('*').find(e => e.id === id) || null; }
        appendChild(c) { this.children.push(c); c.parentNode = this; return c; }
    }
    const htmlDocument = () => {
        const body = new HtmlElement('body');
        let buffer = '';
        return {
            parentWindow: { clipboardData: clipboard }, body, documentElement: body,
            open() { buffer = ''; body.innerHTML = ''; },
            write(...t) { buffer += t.join(''); body.innerHTML = buffer; },
            close() {},
            createElement: tag => new HtmlElement(String(tag)),
            getElementsByTagName: name => body.getElementsByTagName(name),
            getElementsByClassName: name => body.getElementsByClassName(name),
            getElementById: id => body.getElementById(id),
        };
    };

    const clipboard = { getData: () => native.clipboard(), setData: (format, text) => native.clipboard(String(text)), clearData: () => native.clipboard('') };
    const inert = () => {
        const p = new Proxy(function () {}, {
            get: (t, k) => (k === Symbol.toPrimitive ? () => '' : k === 'toString' || k === 'valueOf' ? () => '' : p),
            apply: () => p,
            set: () => true,
        });
        return p;
    };
    const uiHacks = () => {
        const limits = { min: { Enabled: false, Width: 0, Height: 0 }, max: { Enabled: false, Width: 0, Height: 0 } };
        const apply = () => native.windowLimits(limits.min.Enabled ? limits.min.Width : 0, limits.min.Enabled ? limits.min.Height : 0,
                                                limits.max.Enabled ? limits.max.Width : 0, limits.max.Enabled ? limits.max.Height : 0);
        const size = which => {
            const o = {};
            for (const k of ['Enabled', 'Width', 'Height'])
                Object.defineProperty(o, k, { enumerable: true, get: () => limits[which][k], set: v => { limits[which][k] = k === 'Enabled' ? !!v : v | 0; apply(); } });
            return o;
        };
        return {
            FrameStyle: 0, DisableSizing: false, MoveStyle: 0, BlockMaximize: false,
            Aero: { Effect: 0, Left: 0, Top: 0, Right: 0, Bottom: 0 },
            MinSize: size('min'), MaxSize: size('max'),
            get MainWindowState() { return native.windowState(); },
            set MainWindowState(v) { native.windowState(v | 0); },
            get FullScreen() { return native.fullscreen(); },
            set FullScreen(v) { native.fullscreen(!!v); },
            SetPseudoCaption(x, y, w, h) { native.caption(x | 0, y | 0, w | 0, h | 0); }, EnableSizing() {},
            ToggleFullscreen() { native.fullscreen(!native.fullscreen()); },
        };
    };
    const factories = {
        'scripting.filesystemobject': () => fso,
        'wscript.shell': shell,
        'microsoft.xmlhttp': () => new Http(),
        'msxml2.xmlhttp': () => new Http(),
        'msxml2.xmlhttp.6.0': () => new Http(),
        'msxml2.serverxmlhttp': () => new Http(),
        'msxml2.serverxmlhttp.6.0': () => new Http(),
        'winhttp.winhttprequest.5.1': () => new Http(),
        'htmlfile': htmlDocument,
        'uihacks': uiHacks,
        'eslyric': inert,
        'shell.application': () => ({ BrowseForFolder: () => null, NameSpace: () => null, ShellExecute: (f, a) => native.shellRun(String(f) + (a ? ' ' + a : '')) }),
        'scriptcontrol': () => ({ Language: '', AddCode() {}, Eval() { fail('ScriptControl (VBScript) is not available in JS Panel', -2146827859); },
                                  Run() { fail('ScriptControl (VBScript) is not available in JS Panel', -2146827859); } }),
    };
    globalThis.ActiveXObject = function (progId) {
        const make = factories[String(progId).toLowerCase()];
        if (!make) fail('Automation server can\'t create object (' + progId + ')', -2146827859);
        return make();
    };
})();
)JS";

JSValue global_include(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    const std::string path = script_relative(e, arg_str(ctx, argc, argv, 0));
    std::string source;
    if (!e->host.read_text_file(path, source)) return JS_ThrowInternalError(ctx, "include: can't read %s", path.c_str());
    e->includeDirs.push_back(script_dir_of(path));
    JSValue r = JS_Eval(ctx, source.data(), source.size(), path.c_str(), JS_EVAL_TYPE_GLOBAL);
    e->includeDirs.pop_back();
    if (JS_IsException(r)) return r;
    JS_FreeValue(ctx, r);
    return JS_UNDEFINED;
}

int interrupt(JSRuntime*, void* opaque) {
    auto* e = static_cast<JsEngine::Impl*>(opaque);
    return e->deadline != 0 && now_ms() > e->deadline;
}

void add_object(JSContext* ctx, JSValueConst global, const char* name, const JSCFunctionListEntry* fns, int n) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, obj, fns, n);
    JS_SetPropertyStr(ctx, global, name, obj);
}

void add_class(JSContext* ctx, JSClassID id, const JSCFunctionListEntry* fns, int n) {
    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, proto, fns, n);
    JS_SetClassProto(ctx, id, proto);
}

template <std::size_t N> constexpr int count(const JSCFunctionListEntry (&)[N]) { return (int)N; }

}

JsEngine::Impl::Impl(JsHost& hst, int budget) : host(hst), budgetMs(budget) {
    rt = JS_NewRuntime();
    JS_SetMemoryLimit(rt, kMemoryLimit);
    JS_SetMaxStackSize(rt, 1024 * 1024);
    JS_SetInterruptHandler(rt, interrupt, this);
    JS_NewClassID(rt, &g_grClass);
    JS_NewClassID(rt, &g_fontClass);
    JS_NewClassID(rt, &g_tfClass);
    JS_NewClassID(rt, &g_handleClass);
    JS_NewClassID(rt, &g_listClass);
    JS_NewClassID(rt, &g_bitmapClass);
    JS_NewClassID(rt, &g_infoClass);
    JS_NewClassID(rt, &g_menuClass);
    JS_NewClassID(rt, &g_menuManagerClass);
    const JSClassDef grDef{ "GdiGraphics", graphics_finalizer, nullptr, nullptr, nullptr };
    const JSClassDef fontDef{ "GdiFont", font_finalizer, nullptr, nullptr, nullptr };
    const JSClassDef tfDef{ "FbTitleFormat", tf_finalizer, nullptr, nullptr, nullptr };
    JS_NewClass(rt, g_grClass, &grDef);
    JS_NewClass(rt, g_fontClass, &fontDef);
    JS_NewClass(rt, g_tfClass, &tfDef);
    const JSClassDef handleDef{ "FbMetadbHandle", handle_finalizer, nullptr, nullptr, nullptr };
    const JSClassDef listDef{ "FbMetadbHandleList", list_finalizer, nullptr, nullptr, &kListExotic };
    const JSClassDef bitmapDef{ "GdiBitmap", bitmap_finalizer, nullptr, nullptr, nullptr };
    JS_NewClass(rt, g_handleClass, &handleDef);
    JS_NewClass(rt, g_listClass, &listDef);
    JS_NewClass(rt, g_bitmapClass, &bitmapDef);
    const JSClassDef infoDef{ "FbFileInfo", info_finalizer, nullptr, nullptr, nullptr };
    JS_NewClass(rt, g_infoClass, &infoDef);
    const JSClassDef menuDef{ "MenuObject", menu_finalizer, nullptr, nullptr, nullptr };
    const JSClassDef managerDef{ "MenuManager", manager_finalizer, nullptr, nullptr, nullptr };
    JS_NewClass(rt, g_menuClass, &menuDef);
    JS_NewClass(rt, g_menuManagerClass, &managerDef);
    ctx = JS_NewContext(rt);
    JS_SetContextOpaque(ctx, this);
    install_api();
}

JsEngine::Impl::~Impl() {
    if (error.empty()) call("on_script_unload", 0, nullptr);
    *alive = false;
    flush_properties();
    JS_FreeValue(ctx, props);
    free_pending();
    free_timers();
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

void JsEngine::Impl::install_api() {
    add_class(ctx, g_grClass, kGrFuncs, count(kGrFuncs));
    add_class(ctx, g_fontClass, kFontFuncs, count(kFontFuncs));
    add_class(ctx, g_tfClass, kTfFuncs, count(kTfFuncs));
    add_class(ctx, g_handleClass, kHandleFuncs, count(kHandleFuncs));
    add_class(ctx, g_listClass, kListFuncs, count(kListFuncs));
    add_class(ctx, g_bitmapClass, kBitmapFuncs, count(kBitmapFuncs));
    add_class(ctx, g_infoClass, kInfoFuncs, count(kInfoFuncs));
    add_class(ctx, g_menuClass, kMenuFuncs, count(kMenuFuncs));
    add_class(ctx, g_menuManagerClass, kMenuManagerFuncs, count(kMenuManagerFuncs));
    JSValue global = JS_GetGlobalObject(ctx);
    add_object(ctx, global, "window", kWindowFuncs, count(kWindowFuncs));
    add_object(ctx, global, "fb", kFbFuncs, count(kFbFuncs));
    add_object(ctx, global, "gdi", kGdiFuncs, count(kGdiFuncs));
    add_object(ctx, global, "console", kConsoleFuncs, count(kConsoleFuncs));
    add_object(ctx, global, "navidrome", kNavidromeFuncs, count(kNavidromeFuncs));
    add_object(ctx, global, "utils", kUtilsFuncs, count(kUtilsFuncs));
    add_object(ctx, global, "plman", kPlmanFuncs, count(kPlmanFuncs));
    add_object(ctx, global, "panelsui", kPanelsUiFuncs, count(kPanelsUiFuncs));
    JSValue native = JS_NewObject(ctx);
    JS_SetPropertyFunctionList(ctx, native, kNativeFuncs, count(kNativeFuncs));
    JS_DefinePropertyValueStr(ctx, global, "__jsp", native, JS_PROP_CONFIGURABLE);
    JS_SetPropertyStr(ctx, global, "include", JS_NewCFunction(ctx, global_include, "include", 1));
    JS_FreeValue(ctx, global);
    JS_FreeValue(ctx, JS_Eval(ctx, kPrelude, std::strlen(kPrelude), "<prelude>", JS_EVAL_TYPE_GLOBAL));
}

void JsEngine::Impl::drain_jobs() {
    JSContext* jobCtx = nullptr;
    while (error.empty()) {
        const int r = JS_ExecutePendingJob(rt, &jobCtx);
        if (r == 0) break;
        if (r < 0) {
            report_exception();
            break;
        }
    }
    flush_properties();
}

JSValueConst JsEngine::Impl::properties() {
    if (JS_IsUndefined(props)) {
        props = from_json(ctx, host.load_properties());
        if (!JS_IsObject(props) || JS_IsArray(props)) {
            JS_FreeValue(ctx, props);
            props = JS_NewObject(ctx);
        }
    }
    return props;
}

void JsEngine::Impl::flush_properties() {
    if (!propsDirty || !JS_IsObject(props)) return;
    propsDirty = false;
    host.save_properties(to_json(ctx, props));
}

bool JsEngine::Impl::eval(const std::string& source, const std::string& filename) {
    JSValue r;
    {
        Budget b(*this);
        r = JS_Eval(ctx, source.data(), source.size(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    }
    if (JS_IsException(r)) {
        report_exception();
        return false;
    }
    JS_FreeValue(ctx, r);
    drain_jobs();
    return error.empty();
}

bool JsEngine::Impl::call(const char* name, int argc, JSValue* argv, bool* truthy) {
    if (truthy) *truthy = false;
    if (!error.empty()) return false;
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue fn = JS_GetPropertyStr(ctx, global, name);
    bool called = false;
    if (JS_IsFunction(ctx, fn)) {
        called = true;
        JSValue r;
        {
            Budget b(*this);
            r = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
        }
        if (JS_IsException(r)) report_exception();
        else {
            if (truthy) *truthy = JS_ToBool(ctx, r) > 0;
            JS_FreeValue(ctx, r);
            drain_jobs();
        }
    }
    JS_FreeValue(ctx, fn);
    JS_FreeValue(ctx, global);
    return called && error.empty();
}

void JsEngine::Impl::report_exception() {
    JSValue ex = JS_GetException(ctx);
    std::string msg = to_string(ctx, ex);
    if (JS_IsObject(ex)) {
        JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");
        if (JS_IsString(stack)) {
            const std::string s = to_string(ctx, stack);
            if (!s.empty()) msg += "\n" + s;
        }
        JS_FreeValue(ctx, stack);
    }
    JS_FreeValue(ctx, ex);
    if (msg.find("interrupted") != std::string::npos)
        msg = "script took longer than " + std::to_string(budgetMs / 1000) + " s and was stopped\n" + msg;
    fail(msg);
}

void JsEngine::Impl::fail(const std::string& msg) {
    error = msg.empty() ? std::string("unknown error") : msg;
    while (!error.empty() && error.back() == '\n') error.pop_back();
    host.console("error: " + error);
    flush_properties();
    free_timers();
    host.repaint();
}

JSValue JsEngine::Impl::request_albums(bool artists) {
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, funcs);
    if (JS_IsException(promise)) return promise;
    const int id = nextPending++;
    pending[id] = Pending{ funcs[0], funcs[1], artists };
    std::shared_ptr<bool> token = alive;
    Impl* self = this;
    host.navidrome_albums([token, self, id](std::vector<LibraryAlbum> albums, std::string err) {
        if (*token) self->settle(id, albums, err);
    });
    return promise;
}

void JsEngine::Impl::settle(int id, const std::vector<LibraryAlbum>& albums, const std::string& err) {
    auto it = pending.find(id);
    if (it == pending.end()) return;
    Pending p = it->second;
    pending.erase(it);
    JSValue arg;
    JSValue fn = p.resolve;
    if (!err.empty()) {
        arg = JS_NewError(ctx);
        set_str(ctx, arg, "message", err);
        fn = p.reject;
    } else if (p.artists) {
        const auto list = library_artists(albums);
        arg = JS_NewArray(ctx);
        for (uint32_t i = 0; i < list.size(); ++i) JS_SetPropertyUint32(ctx, arg, i, artist_object(ctx, list[i]));
    } else {
        arg = JS_NewArray(ctx);
        for (uint32_t i = 0; i < albums.size(); ++i) JS_SetPropertyUint32(ctx, arg, i, album_object(ctx, albums[i]));
    }
    if (error.empty()) {
        JSValue r;
        {
            Budget b(*this);
            r = JS_Call(ctx, fn, JS_UNDEFINED, 1, &arg);
        }
        if (JS_IsException(r)) report_exception();
        else JS_FreeValue(ctx, r);
    }
    JS_FreeValue(ctx, arg);
    JS_FreeValue(ctx, p.resolve);
    JS_FreeValue(ctx, p.reject);
    drain_jobs();
}

void JsEngine::Impl::art_done(const TrackPtr& track, int artId, pui::gfx::ImagePtr image, const std::string& path) {
    JSValue args[4] = { new_handle(ctx, track), JS_NewInt32(ctx, artId), new_bitmap(ctx, image),
                        JS_NewStringLen(ctx, path.data(), path.size()) };
    call("on_get_album_art_done", 4, args);
    for (JSValue& v : args) JS_FreeValue(ctx, v);
}

void JsEngine::Impl::free_pending() {
    for (auto& [id, p] : pending) {
        JS_FreeValue(ctx, p.resolve);
        JS_FreeValue(ctx, p.reject);
    }
    pending.clear();
    for (auto& [id, p] : promises) {
        JS_FreeValue(ctx, p.first);
        JS_FreeValue(ctx, p.second);
    }
    promises.clear();
}

JSValue JsEngine::Impl::new_promise(int& id) {
    JSValue funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, funcs);
    if (JS_IsException(promise)) return promise;
    id = nextTask++;
    promises[id] = { funcs[0], funcs[1] };
    return promise;
}

void JsEngine::Impl::settle_promise(int id, JSValue value, bool ok) {
    auto it = promises.find(id);
    if (it == promises.end()) {
        JS_FreeValue(ctx, value);
        return;
    }
    const auto fns = it->second;
    promises.erase(it);
    if (error.empty()) {
        JSValue r;
        {
            Budget b(*this);
            r = JS_Call(ctx, ok ? fns.first : fns.second, JS_UNDEFINED, 1, &value);
        }
        if (JS_IsException(r)) report_exception();
        else JS_FreeValue(ctx, r);
    }
    JS_FreeValue(ctx, value);
    JS_FreeValue(ctx, fns.first);
    JS_FreeValue(ctx, fns.second);
    drain_jobs();
}

void JsEngine::Impl::image_loaded(int task, pui::gfx::ImagePtr image, const std::string& path) {
    JSValue args[3] = { JS_NewInt32(ctx, task), new_bitmap(ctx, image), new_string(ctx, path) };
    call("on_load_image_done", 3, args);
    for (JSValue& v : args) JS_FreeValue(ctx, v);
}

void JsEngine::Impl::free_timers() {
    for (auto& [id, t] : timers) {
        host.kill_timer(id);
        JS_FreeValue(ctx, t.fn);
    }
    timers.clear();
}

int JsEngine::Impl::add_timer(JSValueConst fn, int ms, bool repeat) {
    const int id = nextTimer++;
    timers[id] = Timer{ JS_DupValue(ctx, fn), repeat };
    host.set_timer(id, ms);
    return id;
}

void JsEngine::Impl::clear_timer(int id) {
    auto it = timers.find(id);
    if (it == timers.end()) return;
    host.kill_timer(id);
    JS_FreeValue(ctx, it->second.fn);
    timers.erase(it);
}

std::vector<LibraryArtist> library_artists(const std::vector<LibraryAlbum>& albums) {
    std::map<std::string, LibraryArtist> byKey;
    for (const auto& a : albums) {
        const std::string key = a.artistId.empty() ? "name:" + a.artist : a.artistId;
        LibraryArtist& r = byKey[key];
        if (r.name.empty()) {
            r.id = a.artistId;
            r.name = a.artist;
        }
        ++r.albumCount;
        r.songCount += a.songCount;
    }
    std::vector<LibraryArtist> out;
    for (auto& [k, v] : byKey) out.push_back(std::move(v));
    std::stable_sort(out.begin(), out.end(), [](const LibraryArtist& x, const LibraryArtist& y) { return lower_ascii(x.name) < lower_ascii(y.name); });
    return out;
}

std::string check_syntax(const std::string& source, const std::string& name) {
    JSRuntime* rt = JS_NewRuntime();
    JS_SetMemoryLimit(rt, kSyntaxCheckMemory);
    JSContext* ctx = JS_NewContext(rt);
    std::string err;
    JSValue r = JS_Eval(ctx, source.data(), source.size(), name.c_str(), JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(r)) {
        JSValue ex = JS_GetException(ctx);
        err = to_string(ctx, ex);
        if (JS_IsObject(ex)) {
            JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");
            const std::string s = JS_IsString(stack) ? to_string(ctx, stack) : std::string();
            JS_FreeValue(ctx, stack);
            const auto at = s.find(name + ":");
            if (at != std::string::npos) {
                const auto end = s.find_first_of(")\n", at);
                err += " (line " + s.substr(at + name.size() + 1, end == std::string::npos ? std::string::npos : end - at - name.size() - 1) + ")";
            }
        }
        JS_FreeValue(ctx, ex);
    } else {
        JS_FreeValue(ctx, r);
    }
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return err;
}

TextPos text_pos(const std::string& utf8, std::size_t byteOffset) {
    TextPos p;
    const std::size_t end = std::min(byteOffset, utf8.size());
    for (std::size_t i = 0; i < end; ++i) {
        const unsigned char c = (unsigned char)utf8[i];
        if (c == '\n') {
            ++p.line;
            p.col = 1;
        } else if ((c & 0xC0) != 0x80) {
            ++p.col;
        }
    }
    return p;
}

std::string editor_status(const TextPos& pos, const std::string& syntaxError, const std::string& applied) {
    std::string s = "Line " + std::to_string(pos.line) + ", col " + std::to_string(pos.col) + "   ";
    s += syntaxError.empty() ? std::string("Syntax OK") : syntaxError;
    if (!applied.empty()) s += "   \u00b7   " + applied;
    return s;
}

JsEngine::JsEngine(JsHost& host, int callBudgetMs) : m(std::make_unique<Impl>(host, callBudgetMs)) {}
JsEngine::~JsEngine() = default;

bool JsEngine::load(const std::string& source, const std::string& path) {
    m->dir = script_dir_of(path);
    return m->eval(source, path.empty() ? std::string("<sample>") : path);
}

bool JsEngine::failed() const { return !m->error.empty(); }
const std::string& JsEngine::error() const { return m->error; }

void JsEngine::paint(Canvas& canvas) {
    const Rect all{ 0, 0, canvas.width(), canvas.height() };
    if (failed()) {
        canvas.fill_rect(all, kErrorBackground);
        canvas.set_font(font_spec("Segoe UI", 13, 0));
        canvas.draw_text("JS Panel: script error\n\n" + m->error, Rect{ 8, 8, all.w - 16, all.h - 16 },
                         pui::gfx::kWordWrap, kErrorText);
        return;
    }
    canvas.fill_rect(all, kBackground);
    JSValue gr = JS_NewObjectClass(m->ctx, (int)g_grClass);
    auto* g = new Graphics;
    g->canvas = &canvas;
    JS_SetOpaque(gr, g);
    m->call("on_paint", 1, &gr);
    g->canvas = nullptr;
    JS_FreeValue(m->ctx, gr);
    if (failed()) paint(canvas);
}

void JsEngine::resize(int w, int h) {
    if (w == m->w && h == m->h) return;
    m->w = w;
    m->h = h;
    JSValue args[2] = { JS_NewInt32(m->ctx, w), JS_NewInt32(m->ctx, h) };
    m->call("on_size", 2, args);
}

bool JsEngine::mouse(Mouse kind, int x, int y, unsigned mask) {
    static const char* const names[] = { "on_mouse_move",        "on_mouse_lbtn_down", "on_mouse_lbtn_up",
                                         "on_mouse_lbtn_dblclk", "on_mouse_rbtn_down", "on_mouse_rbtn_up",
                                         "on_mouse_leave",       "on_mouse_mbtn_down", "on_mouse_mbtn_up",
                                         "on_mouse_mbtn_dblclk" };
    bool truthy = false;
    if (kind == Mouse::Leave) {
        m->call(names[(int)kind], 0, nullptr, &truthy);
        return truthy;
    }
    JSValue args[3] = { JS_NewInt32(m->ctx, x), JS_NewInt32(m->ctx, y), JS_NewInt32(m->ctx, (int)mask) };
    m->call(names[(int)kind], 3, args, &truthy);
    return truthy;
}

void JsEngine::wheel(int step) {
    JSValue arg = JS_NewInt32(m->ctx, step);
    m->call("on_mouse_wheel", 1, &arg);
}

bool JsEngine::key_down(int vk) {
    if (vk <= 0) return false;
    JSValue arg = JS_NewInt32(m->ctx, vk);
    bool truthy = false;
    m->call("on_key_down", 1, &arg, &truthy);
    return truthy;
}

void JsEngine::timer(int id) {
    auto it = m->timers.find(id);
    if (it == m->timers.end()) {
        m->host.kill_timer(id);
        return;
    }
    JSValue fn = JS_DupValue(m->ctx, it->second.fn);
    if (!it->second.repeat) m->clear_timer(id);
    if (m->error.empty()) {
        JSValue r;
        {
            Impl::Budget b(*m);
            r = JS_Call(m->ctx, fn, JS_UNDEFINED, 0, nullptr);
        }
        if (JS_IsException(r)) m->report_exception();
        else {
            JS_FreeValue(m->ctx, r);
            m->drain_jobs();
        }
    }
    JS_FreeValue(m->ctx, fn);
}

void JsEngine::playback_new_track(const TrackPtr& track) {
    JSValue arg = new_handle(m->ctx, track);
    m->call("on_playback_new_track", 1, &arg);
    JS_FreeValue(m->ctx, arg);
}

void JsEngine::library_changed(LibraryChange kind, Tracks tracks) {
    static const char* const names[] = { "on_library_items_added", "on_library_items_removed", "on_library_items_changed" };
    JSValue arg = new_list(m->ctx, std::move(tracks));
    m->call(names[(int)kind], 1, &arg);
    JS_FreeValue(m->ctx, arg);
}

void JsEngine::playback_stop(int reason) {
    JSValue arg = JS_NewInt32(m->ctx, reason);
    m->call("on_playback_stop", 1, &arg);
}

void JsEngine::playback_pause(bool paused) {
    JSValue arg = JS_NewBool(m->ctx, paused);
    m->call("on_playback_pause", 1, &arg);
}

void JsEngine::playback_time(double t) {
    JSValue arg = JS_NewFloat64(m->ctx, t);
    m->call("on_playback_time", 1, &arg);
}

bool JsEngine::invoke(const char* name, const std::vector<Arg>& args) {
    JSContext* ctx = m->ctx;
    std::vector<JSValue> argv;
    argv.reserve(args.size());
    for (const Arg& a : args)
        argv.push_back(std::visit(
            [ctx](const auto& v) -> JSValue {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, int>) return JS_NewInt32(ctx, v);
                else if constexpr (std::is_same_v<T, double>) return JS_NewFloat64(ctx, v);
                else if constexpr (std::is_same_v<T, bool>) return JS_NewBool(ctx, v);
                else if constexpr (std::is_same_v<T, std::string>) return new_string(ctx, v);
                else if constexpr (std::is_same_v<T, TrackPtr>) return new_handle(ctx, v);
                else return new_list(ctx, v);
            },
            a));
    bool truthy = false;
    m->call(name, (int)argv.size(), argv.data(), &truthy);
    for (JSValue& v : argv) JS_FreeValue(ctx, v);
    return truthy;
}

bool JsEngine::in_modal() const { return m->modalDepth > 0; }

void JsEngine::notify_data(const std::string& name, const std::string& json, const std::vector<JsHost::NotifyItem>& items) {
    JSValue info = from_json(m->ctx, json);
    if (!items.empty()) {
        JSValue arr = JS_NewArray(m->ctx);
        for (uint32_t i = 0; i < items.size(); ++i)
            JS_SetPropertyUint32(m->ctx, arr, i, items[i].single && !items[i].tracks.empty() ? new_handle(m->ctx, items[i].tracks.front()) : new_list(m->ctx, items[i].tracks));
        JSValue global = JS_GetGlobalObject(m->ctx);
        JSValue native = JS_GetPropertyStr(m->ctx, global, "__jsp");
        JSValue fn = JS_GetPropertyStr(m->ctx, native, "unpack");
        JSValue args[2] = { info, arr };
        JSValue r = JS_IsFunction(m->ctx, fn) ? JS_Call(m->ctx, fn, JS_UNDEFINED, 2, args) : JS_DupValue(m->ctx, info);
        if (JS_IsException(r)) {
            JS_FreeValue(m->ctx, JS_GetException(m->ctx));
            r = JS_DupValue(m->ctx, info);
        }
        JS_FreeValue(m->ctx, info);
        info = r;
        JS_FreeValue(m->ctx, arr);
        JS_FreeValue(m->ctx, fn);
        JS_FreeValue(m->ctx, native);
        JS_FreeValue(m->ctx, global);
    }
    JSValue args[2] = { new_string(m->ctx, name), info };
    m->call("on_notify_data", 2, args);
    for (JSValue& v : args) JS_FreeValue(m->ctx, v);
}

void JsEngine::playback_starting(int cmd, bool paused) {
    JSValue args[2] = { JS_NewInt32(m->ctx, cmd), JS_NewBool(m->ctx, paused) };
    m->call("on_playback_starting", 2, args);
}

}
