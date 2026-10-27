#include "js_engine.h"
#include "js_api_util.h"
#include "quickjs.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
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

JSClassID g_grClass = 0, g_fontClass = 0, g_tfClass = 0, g_handleClass = 0, g_listClass = 0, g_bitmapClass = 0;
int g_nextWindowId = 1;
pui::gfx::ImagePtr* bitmap_of(JSValueConst v);

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
    Canvas* canvas = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    struct Pending {
        JSValue resolve, reject;
        bool artists;
    };
    std::map<int, Pending> pending;
    int nextPending = 1;
    int windowId = g_nextWindowId++;

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

Rect arg_rect(JSContext* ctx, int argc, JSValueConst* argv, int i) {
    return Rect{ arg_int(ctx, argc, argv, i), arg_int(ctx, argc, argv, i + 1), arg_int(ctx, argc, argv, i + 2),
                 arg_int(ctx, argc, argv, i + 3) };
}

Canvas* canvas_or_throw(JSContext* ctx) {
    Canvas* c = I(ctx)->canvas;
    if (!c) JS_ThrowTypeError(ctx, "gr can only be used inside on_paint");
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

JSValue gr_FillSolidRect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx);
    if (!c) return JS_EXCEPTION;
    fill(*c, arg_rect(ctx, argc, argv, 0), arg_u32(ctx, argc, argv, 4));
    return JS_UNDEFINED;
}

JSValue gr_DrawRect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx);
    if (!c) return JS_EXCEPTION;
    Rect r = arg_rect(ctx, argc, argv, 0);
    const int lw = std::max(1, arg_int(ctx, argc, argv, 4, 1));
    const Color col = argb_color(arg_u32(ctx, argc, argv, 5));
    r.w += 1;
    r.h += 1;
    for (int i = 0; i < lw && !r.empty(); ++i) {
        c->frame_rect(r, col);
        r = Rect{ r.x + 1, r.y + 1, r.w - 2, r.h - 2 };
    }
    return JS_UNDEFINED;
}

JSValue gr_DrawLine(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx);
    if (!c) return JS_EXCEPTION;
    const double x0 = arg_num(ctx, argc, argv, 0), y0 = arg_num(ctx, argc, argv, 1);
    const double x1 = arg_num(ctx, argc, argv, 2), y1 = arg_num(ctx, argc, argv, 3);
    const double lw = arg_num(ctx, argc, argv, 4, 1);
    const Color col = argb_color(arg_u32(ctx, argc, argv, 5));
    const double len = std::hypot(x1 - x0, y1 - y0);
    if (lw <= 1 || len == 0) {
        c->line((int)std::lround(x0), (int)std::lround(y0), (int)std::lround(x1), (int)std::lround(y1), col);
        return JS_UNDEFINED;
    }
    const double nx = -(y1 - y0) / len * lw / 2, ny = (x1 - x0) / len * lw / 2;
    const pui::gfx::PointF pts[4] = { { (float)(x0 + nx), (float)(y0 + ny) }, { (float)(x1 + nx), (float)(y1 + ny) },
                                      { (float)(x1 - nx), (float)(y1 - ny) }, { (float)(x0 - nx), (float)(y0 - ny) } };
    c->fill_polygon(pts, 4, col);
    return JS_UNDEFINED;
}

JSValue gr_FillRoundRect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx);
    if (!c) return JS_EXCEPTION;
    c->fill_round_rect(arg_rect(ctx, argc, argv, 0), arg_int(ctx, argc, argv, 4), arg_int(ctx, argc, argv, 5),
                       argb_color(arg_u32(ctx, argc, argv, 6)));
    return JS_UNDEFINED;
}

JSValue gr_FillEllipse(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx);
    if (!c) return JS_EXCEPTION;
    const Rect r = arg_rect(ctx, argc, argv, 0);
    c->fill_round_rect(r, r.w / 2, r.h / 2, argb_color(arg_u32(ctx, argc, argv, 4)));
    return JS_UNDEFINED;
}

JSValue gr_FillGradRect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx);
    if (!c) return JS_EXCEPTION;
    const Rect r = arg_rect(ctx, argc, argv, 0);
    if (r.empty()) return JS_UNDEFINED;
    const double angle = std::fmod(std::fmod(arg_num(ctx, argc, argv, 4), 360) + 360, 360);
    Color a = argb_color(arg_u32(ctx, argc, argv, 5)), b = argb_color(arg_u32(ctx, argc, argv, 6));
    if (angle >= 45 && angle < 135) c->gradient_v(r, a, b);
    else if (angle >= 225 && angle < 315) c->gradient_v(r, b, a);
    else {
        if (angle >= 135 && angle < 225) std::swap(a, b);
        for (int x = 0; x < r.w; ++x)
            c->fill_rect(Rect{ r.x + x, r.y, 1, r.h }, mix(a, b, r.w > 1 ? (double)x / (r.w - 1) : 0));
    }
    return JS_UNDEFINED;
}

JSValue draw_text(JSContext* ctx, int argc, JSValueConst* argv, unsigned flags) {
    Canvas* c = canvas_or_throw(ctx);
    if (!c) return JS_EXCEPTION;
    FontSpec* f = font_arg(ctx, argc, argv, 1);
    if (!f) return JS_EXCEPTION;
    c->set_font(*f);
    c->draw_text(arg_str(ctx, argc, argv, 0), arg_rect(ctx, argc, argv, 3), flags,
                 argb_color(arg_u32(ctx, argc, argv, 2)));
    return JS_UNDEFINED;
}

JSValue gr_DrawString(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return draw_text(ctx, argc, argv, string_format_to_text_flags(arg_u32(ctx, argc, argv, 7)));
}

JSValue gr_GdiDrawText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return draw_text(ctx, argc, argv, dt_to_text_flags(arg_u32(ctx, argc, argv, 7)));
}

JSValue gr_CalcTextWidth(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx);
    if (!c) return JS_EXCEPTION;
    FontSpec* f = font_arg(ctx, argc, argv, 1);
    if (!f) return JS_EXCEPTION;
    c->set_font(*f);
    return JS_NewInt32(ctx, c->text_width(arg_str(ctx, argc, argv, 0)));
}

JSValue gr_CalcTextHeight(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx);
    if (!c) return JS_EXCEPTION;
    FontSpec* f = font_arg(ctx, argc, argv, 1);
    if (!f) return JS_EXCEPTION;
    c->set_font(*f);
    return JS_NewInt32(ctx, c->text_height(arg_str(ctx, argc, argv, 0), 1 << 20, pui::gfx::kSingleLine));
}

JSValue gr_DrawImage(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Canvas* c = canvas_or_throw(ctx);
    if (!c) return JS_EXCEPTION;
    auto* b = argc > 0 ? bitmap_of(argv[0]) : nullptr;
    if (!b) return JS_ThrowTypeError(ctx, "argument 1 is not a GdiBitmap");
    if (!*b) return JS_UNDEFINED;
    const pui::gfx::Image& img = **b;
    auto f = [&](int i, double def) { return (float)arg_num(ctx, argc, argv, i, def); };
    const pui::gfx::RectF dst{ f(1, 0), f(2, 0), f(3, (float)img.width()), f(4, (float)img.height()) };
    const pui::gfx::RectF src{ f(5, 0), f(6, 0), f(7, (float)img.width()), f(8, (float)img.height()) };
    const int alpha = std::clamp(arg_int(ctx, argc, argv, 10, 255), 0, 255);
    if (dst.w > 0 && dst.h > 0 && src.w > 0 && src.h > 0 && alpha > 0) c->draw_image(img, dst, src, alpha, false, pui::gfx::Interp::High);
    return JS_UNDEFINED;
}

JSValue gr_noop(JSContext*, JSValueConst, int, JSValueConst*) { return JS_UNDEFINED; }

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
pui::gfx::ImagePtr* bitmap_of(JSValueConst v) { return static_cast<pui::gfx::ImagePtr*>(JS_GetOpaque(v, g_bitmapClass)); }

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

JSValue new_bitmap(JSContext* ctx, const pui::gfx::ImagePtr& img) {
    if (!img) return JS_NULL;
    JSValue obj = JS_NewObjectClass(ctx, (int)g_bitmapClass);
    if (!JS_IsException(obj)) JS_SetOpaque(obj, new pui::gfx::ImagePtr(img));
    return obj;
}

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
void bitmap_finalizer(JSRuntime*, JSValueConst val) { delete bitmap_of(val); }

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
};

JSValue bitmap_Width(JSContext* ctx, JSValueConst self) {
    auto* b = bitmap_of(self);
    return JS_NewInt32(ctx, b && *b ? (*b)->width() : 0);
}

JSValue bitmap_Height(JSContext* ctx, JSValueConst self) {
    auto* b = bitmap_of(self);
    return JS_NewInt32(ctx, b && *b ? (*b)->height() : 0);
}

JSValue bitmap_Dispose(JSContext*, JSValueConst self, int, JSValueConst*) {
    if (auto* b = bitmap_of(self)) b->reset();
    return JS_UNDEFINED;
}

const JSCFunctionListEntry kBitmapFuncs[] = {
    JS_CGETSET_DEF("Width", bitmap_Width, nullptr),
    JS_CGETSET_DEF("Height", bitmap_Height, nullptr),
    JS_CFUNC_DEF("Dispose", 0, bitmap_Dispose),
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
};

void set_str(JSContext* ctx, JSValueConst obj, const char* k, const std::string& v) {
    JS_SetPropertyStr(ctx, obj, k, JS_NewStringLen(ctx, v.data(), v.size()));
}

void set_int(JSContext* ctx, JSValueConst obj, const char* k, int v) { JS_SetPropertyStr(ctx, obj, k, JS_NewInt32(ctx, v)); }

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
    JS_CFUNC_DEF("PlayAlbum", 3, (nd_play<false, false>)),
    JS_CFUNC_DEF("AddAlbum", 1, (nd_play<false, true>)),
    JS_CFUNC_DEF("PlayArtist", 3, (nd_play<true, false>)),
    JS_CFUNC_DEF("AddArtist", 1, (nd_play<true, true>)),
};

JSValue gdi_Image(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    std::string path = arg_str(ctx, argc, argv, 0);
    const bool absolute = !path.empty() && (path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos);
    if (!absolute) path = e->dir + path;
    return new_bitmap(ctx, e->host.load_image(path));
}

const JSCFunctionListEntry kGdiFuncs[] = {
    JS_CFUNC_DEF("Font", 3, gdi_Font),
    JS_CFUNC_DEF("Image", 1, gdi_Image),
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

const JSCFunctionListEntry kUtilsFuncs[] = {
    JS_CFUNC_DEF("GetAlbumArtAsync", 3, utils_GetAlbumArtAsync),
    JS_CFUNC_DEF("IsKeyPressed", 1, utils_IsKeyPressed),
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

const JSCFunctionListEntry kPlmanFuncs[] = {
    JS_CGETSET_DEF("PlaylistCount", plman_PlaylistCount, nullptr),
    JS_CGETSET_DEF("ActivePlaylist", plman_ActivePlaylist, plman_SetActivePlaylist),
    JS_CGETSET_DEF("PlayingPlaylist", plman_PlayingPlaylist, nullptr),
    JS_CFUNC_DEF("GetPlaylistName", 1, plman_GetPlaylistName),
    JS_CFUNC_DEF("FindOrCreatePlaylist", 2, plman_FindOrCreatePlaylist),
    JS_CFUNC_DEF("ClearPlaylist", 1, plman_ClearPlaylist),
    JS_CFUNC_DEF("InsertPlaylistItems", 4, plman_InsertPlaylistItems),
    JS_CFUNC_DEF("ExecutePlaylistDefaultAction", 2, plman_ExecutePlaylistDefaultAction),
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
};

JSValue global_include(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JsEngine::Impl* e = I(ctx);
    std::string path = arg_str(ctx, argc, argv, 0);
    const bool absolute = !path.empty() && (path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos);
    if (!absolute) path = e->dir + path;
    std::string source;
    if (!e->host.read_text_file(path, source)) return JS_ThrowInternalError(ctx, "include: can't read %s", path.c_str());
    JSValue r = JS_Eval(ctx, source.data(), source.size(), path.c_str(), JS_EVAL_TYPE_GLOBAL);
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
    const JSClassDef grDef{ "GdiGraphics", nullptr, nullptr, nullptr, nullptr };
    const JSClassDef fontDef{ "GdiFont", font_finalizer, nullptr, nullptr, nullptr };
    const JSClassDef tfDef{ "FbTitleFormat", tf_finalizer, nullptr, nullptr, nullptr };
    JS_NewClass(rt, g_grClass, &grDef);
    JS_NewClass(rt, g_fontClass, &fontDef);
    JS_NewClass(rt, g_tfClass, &tfDef);
    const JSClassDef handleDef{ "FbMetadbHandle", handle_finalizer, nullptr, nullptr, nullptr };
    const JSClassDef listDef{ "FbMetadbHandleList", list_finalizer, nullptr, nullptr, nullptr };
    const JSClassDef bitmapDef{ "GdiBitmap", bitmap_finalizer, nullptr, nullptr, nullptr };
    JS_NewClass(rt, g_handleClass, &handleDef);
    JS_NewClass(rt, g_listClass, &listDef);
    JS_NewClass(rt, g_bitmapClass, &bitmapDef);
    ctx = JS_NewContext(rt);
    JS_SetContextOpaque(ctx, this);
    install_api();
}

JsEngine::Impl::~Impl() {
    if (error.empty()) call("on_script_unload", 0, nullptr);
    *alive = false;
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
    JSValue global = JS_GetGlobalObject(ctx);
    add_object(ctx, global, "window", kWindowFuncs, count(kWindowFuncs));
    add_object(ctx, global, "fb", kFbFuncs, count(kFbFuncs));
    add_object(ctx, global, "gdi", kGdiFuncs, count(kGdiFuncs));
    add_object(ctx, global, "console", kConsoleFuncs, count(kConsoleFuncs));
    add_object(ctx, global, "navidrome", kNavidromeFuncs, count(kNavidromeFuncs));
    add_object(ctx, global, "utils", kUtilsFuncs, count(kUtilsFuncs));
    add_object(ctx, global, "plman", kPlmanFuncs, count(kPlmanFuncs));
    JS_SetPropertyStr(ctx, global, "include", JS_NewCFunction(ctx, global_include, "include", 1));
    JS_FreeValue(ctx, global);
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
}

bool JsEngine::Impl::eval(const std::string& source, const std::string& filename) {
    deadline = now_ms() + budgetMs;
    JSValue r = JS_Eval(ctx, source.data(), source.size(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    deadline = 0;
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
        deadline = now_ms() + budgetMs;
        JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, argc, argv);
        deadline = 0;
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
        deadline = now_ms() + budgetMs;
        JSValue r = JS_Call(ctx, fn, JS_UNDEFINED, 1, &arg);
        deadline = 0;
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
    auto lower = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    std::stable_sort(out.begin(), out.end(), [&](const LibraryArtist& x, const LibraryArtist& y) { return lower(x.name) < lower(y.name); });
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
    m->canvas = &canvas;
    m->call("on_paint", 1, &gr);
    m->canvas = nullptr;
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
    static const char* const names[] = { "on_mouse_move",    "on_mouse_lbtn_down", "on_mouse_lbtn_up",
                                         "on_mouse_lbtn_dblclk", "on_mouse_rbtn_down", "on_mouse_rbtn_up",
                                         "on_mouse_leave" };
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
        m->deadline = now_ms() + m->budgetMs;
        JSValue r = JS_Call(m->ctx, fn, JS_UNDEFINED, 0, nullptr);
        m->deadline = 0;
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

void JsEngine::playback_starting(int cmd, bool paused) {
    JSValue args[2] = { JS_NewInt32(m->ctx, cmd), JS_NewBool(m->ctx, paused) };
    m->call("on_playback_starting", 2, args);
}

}
