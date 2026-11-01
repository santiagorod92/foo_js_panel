#include "image_ops.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace jsp::img {

namespace {

int channel(uint32_t p, int shift) { return (int)((p >> shift) & 0xFF); }

uint32_t pack(int a, int r, int g, int b) {
    auto c = [](int v) { return (uint32_t)std::clamp(v, 0, 255); };
    return c(a) << 24 | c(r) << 16 | c(g) << 8 | c(b);
}

uint32_t scale(uint32_t p, int num, int den) {
    return pack(channel(p, 24) * num / den, channel(p, 16) * num / den, channel(p, 8) * num / den, channel(p, 0) * num / den);
}

bool valid(const Pixels& p) { return p.w > 0 && p.h > 0 && p.pargb.size() >= (size_t)p.w * p.h; }

}

Pixels blank(int w, int h) {
    Pixels p;
    p.w = std::max(0, w);
    p.h = std::max(0, h);
    p.pargb.assign((size_t)p.w * p.h, 0);
    return p;
}

Pixels crop(const Pixels& src, int x, int y, int w, int h) {
    Pixels out = blank(w, h);
    if (!valid(src)) return out;
    for (int j = 0; j < out.h; ++j) {
        const int sy = y + j;
        if (sy < 0 || sy >= src.h) continue;
        for (int i = 0; i < out.w; ++i) {
            const int sx = x + i;
            if (sx >= 0 && sx < src.w) out.pargb[(size_t)j * out.w + i] = src.pargb[(size_t)sy * src.w + sx];
        }
    }
    return out;
}

Pixels resize(const Pixels& src, int w, int h, bool nearest) {
    Pixels out = blank(w, h);
    if (!valid(src) || out.w == 0 || out.h == 0) return out;
    const double fx = (double)src.w / out.w, fy = (double)src.h / out.h;
    for (int j = 0; j < out.h; ++j) {
        for (int i = 0; i < out.w; ++i) {
            uint32_t& dst = out.pargb[(size_t)j * out.w + i];
            if (nearest) {
                const int sx = std::min(src.w - 1, (int)(i * fx)), sy = std::min(src.h - 1, (int)(j * fy));
                dst = src.pargb[(size_t)sy * src.w + sx];
                continue;
            }
            if (fx > 1 || fy > 1) {
                const int x0 = (int)(i * fx), x1 = std::max(x0 + 1, std::min(src.w, (int)std::ceil((i + 1) * fx)));
                const int y0 = (int)(j * fy), y1 = std::max(y0 + 1, std::min(src.h, (int)std::ceil((j + 1) * fy)));
                long long sum[4] = {};
                int n = 0;
                for (int sy = y0; sy < y1 && sy < src.h; ++sy)
                    for (int sx = x0; sx < x1 && sx < src.w; ++sx, ++n) {
                        const uint32_t p = src.pargb[(size_t)sy * src.w + sx];
                        for (int k = 0; k < 4; ++k) sum[k] += channel(p, 24 - 8 * k);
                    }
                n = std::max(n, 1);
                dst = pack((int)((sum[0] + n / 2) / n), (int)((sum[1] + n / 2) / n), (int)((sum[2] + n / 2) / n), (int)((sum[3] + n / 2) / n));
                continue;
            }
            const double sx = std::max(0.0, (i + 0.5) * fx - 0.5), sy = std::max(0.0, (j + 0.5) * fy - 0.5);
            const int x0 = std::min(src.w - 1, (int)sx), y0 = std::min(src.h - 1, (int)sy);
            const int x1 = std::min(src.w - 1, x0 + 1), y1 = std::min(src.h - 1, y0 + 1);
            const double tx = sx - x0, ty = sy - y0;
            int v[4];
            for (int k = 0; k < 4; ++k) {
                const int s = 24 - 8 * k;
                const double top = channel(src.pargb[(size_t)y0 * src.w + x0], s) * (1 - tx) + channel(src.pargb[(size_t)y0 * src.w + x1], s) * tx;
                const double bot = channel(src.pargb[(size_t)y1 * src.w + x0], s) * (1 - tx) + channel(src.pargb[(size_t)y1 * src.w + x1], s) * tx;
                v[k] = (int)std::lround(top * (1 - ty) + bot * ty);
            }
            dst = pack(v[0], v[1], v[2], v[3]);
        }
    }
    return out;
}

bool apply_mask(Pixels& img, const Pixels& mask) {
    if (!valid(img) || !valid(mask) || img.w != mask.w || img.h != mask.h) return false;
    for (size_t i = 0; i < (size_t)img.w * img.h; ++i) img.pargb[i] = scale(img.pargb[i], 255 - channel(mask.pargb[i], 0), 255);
    return true;
}

void apply_alpha(Pixels& img, int alpha) {
    alpha = std::clamp(alpha, 0, 255);
    for (uint32_t& p : img.pargb) p = scale(p, alpha, 255);
}

void invert(Pixels& img) {
    for (uint32_t& p : img.pargb) {
        const int a = channel(p, 24);
        p = pack(a, a - channel(p, 16), a - channel(p, 8), a - channel(p, 0));
    }
}

void rotate_flip(Pixels& img, int mode) {
    if (!valid(img)) return;
    const int turns = mode & 3;
    const bool flipX = (mode & 4) != 0;
    const int w = img.w, h = img.h;
    const int ow = turns % 2 ? h : w, oh = turns % 2 ? w : h;
    std::vector<uint32_t> out((size_t)ow * oh);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int nx = x, ny = y;
            switch (turns) {
            case 1: nx = h - 1 - y; ny = x; break;
            case 2: nx = w - 1 - x; ny = h - 1 - y; break;
            case 3: nx = y; ny = w - 1 - x; break;
            default: break;
            }
            if (flipX) nx = ow - 1 - nx;
            out[(size_t)ny * ow + nx] = img.pargb[(size_t)y * w + x];
        }
    img.w = ow;
    img.h = oh;
    img.pargb = std::move(out);
}

void stack_blur(Pixels& img, int radius) {
    radius = std::clamp(radius, 0, 254);
    if (!valid(img) || radius < 1) return;
    const int w = img.w, h = img.h;
    std::vector<int> buf((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; ++i)
        for (int k = 0; k < 4; ++k) buf[i * 4 + k] = channel(img.pargb[i], 24 - 8 * k);
    std::vector<int> line;
    auto box = [&](int count, int len, auto at, int before, int after) {
        const int n = before + after + 1;
        line.resize((size_t)len * 4);
        for (int c = 0; c < count; ++c) {
            for (int i = 0; i < len; ++i)
                for (int k = 0; k < 4; ++k) line[(size_t)i * 4 + k] = buf[at(c, i) * 4 + k];
            for (int k = 0; k < 4; ++k) {
                auto px = [&](int i) { return line[(size_t)std::clamp(i, 0, len - 1) * 4 + k]; };
                long long sum = 0;
                for (int i = -before; i <= after; ++i) sum += px(i);
                for (int i = 0; i < len; ++i) {
                    buf[at(c, i) * 4 + k] = (int)((sum + n / 2) / n);
                    sum += px(i + after + 1) - px(i - before);
                }
            }
        }
    };
    auto row = [w](int r, int i) { return (size_t)r * w + i; };
    auto col = [w](int c, int i) { return (size_t)i * w + c; };
    box(h, w, row, 0, radius);
    box(h, w, row, radius, 0);
    box(w, h, col, 0, radius);
    box(w, h, col, radius, 0);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        const int a = std::clamp(buf[i * 4], 0, 255);
        img.pargb[i] = pack(a, std::min(buf[i * 4 + 1], a), std::min(buf[i * 4 + 2], a), std::min(buf[i * 4 + 3], a));
    }
}

Pixels gradient(int w, int h, double angle, uint32_t from, uint32_t to, double focus) {
    Pixels out = blank(w, h);
    if (out.w == 0 || out.h == 0) return out;
    const double rad = angle * 3.14159265358979 / 180.0, dx = std::cos(rad), dy = std::sin(rad);
    const double c0 = std::min(0.0, dx * (w - 1)) + std::min(0.0, dy * (h - 1));
    const double c1 = std::max(0.0, dx * (w - 1)) + std::max(0.0, dy * (h - 1));
    const double span = c1 - c0 > 1e-9 ? c1 - c0 : 1.0;
    focus = std::clamp(focus, 0.0, 1.0);
    auto lerp = [](int a, int b, double t) { return (int)std::lround(a + (b - a) * t); };
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double t = ((x * dx + y * dy) - c0) / span;
            t = focus >= 1 ? t : focus <= 0 ? 1 - t : (t <= focus ? t / focus : (1 - t) / (1 - focus));
            const int a = lerp(channel(from, 24), channel(to, 24), t);
            const int r = lerp(channel(from, 16), channel(to, 16), t), g = lerp(channel(from, 8), channel(to, 8), t),
                      b = lerp(channel(from, 0), channel(to, 0), t);
            out.pargb[(size_t)y * w + x] = pack(a, r * a / 255, g * a / 255, b * a / 255);
        }
    return out;
}

uint32_t straight_argb(uint32_t p) {
    const int a = channel(p, 24);
    if (a == 0) return 0;
    if (a == 255) return p;
    auto un = [a](int c) { return std::min(255, (c * 255 + a / 2) / a); };
    return pack(a, un(channel(p, 16)), un(channel(p, 8)), un(channel(p, 0)));
}

std::vector<std::pair<uint32_t, double>> colour_scheme(const Pixels& img, int count) {
    std::vector<std::pair<uint32_t, double>> out;
    if (!valid(img) || count <= 0) return out;
    const Pixels small = img.w * img.h > 64 * 64 ? resize(img, std::min(img.w, 64), std::min(img.h, 64), true) : img;
    std::map<uint32_t, int> hist;
    int total = 0;
    for (uint32_t p : small.pargb) {
        if (channel(p, 24) < 128) continue;
        const uint32_t s = straight_argb(p);
        const uint32_t q = 0xFF000000u | ((s & 0xF0F0F0u) | 0x080808u);
        ++hist[q];
        ++total;
    }
    std::vector<std::pair<uint32_t, int>> sorted(hist.begin(), hist.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (size_t i = 0; i < sorted.size() && (int)i < count; ++i) out.emplace_back(sorted[i].first, (double)sorted[i].second / total);
    return out;
}

}
