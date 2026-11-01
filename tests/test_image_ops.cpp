#include "test.h"
#include "core/image_ops.h"

using namespace jsp;
using pui::gfx::Pixels;

namespace {

Pixels make(int w, int h, std::vector<uint32_t> px) {
    Pixels p;
    p.w = w;
    p.h = h;
    p.pargb = std::move(px);
    return p;
}

}

TEST("image ops: crop copies a window and leaves outside pixels transparent") {
    const Pixels src = make(3, 2, { 1, 2, 3, 4, 5, 6 });
    const Pixels c = img::crop(src, 1, 0, 3, 2);
    CHECK_EQ(c.w, 3);
    CHECK(c.pargb == (std::vector<uint32_t>{ 2, 3, 0, 5, 6, 0 }));
}

TEST("image ops: resize averages when shrinking, keeps colours when growing, nearest picks pixels") {
    const Pixels src = make(2, 2, { 0xFF000000u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFF000000u });
    const Pixels half = img::resize(src, 1, 1, false);
    CHECK_EQ(half.pargb[0], 0xFF808080u);
    const Pixels flat = make(1, 1, { 0xFF336699u });
    const Pixels big = img::resize(flat, 4, 3, false);
    CHECK_EQ(big.w * big.h, 12);
    for (uint32_t p : big.pargb) CHECK_EQ(p, 0xFF336699u);
    const Pixels near = img::resize(src, 4, 4, true);
    CHECK_EQ(near.pargb[0], 0xFF000000u);
    CHECK_EQ(near.pargb[3], 0xFFFFFFFFu);
}

TEST("image ops: a mask makes white transparent and keeps black, sizes must match") {
    Pixels im = make(2, 1, { 0xFFFF0000u, 0xFFFF0000u });
    CHECK(img::apply_mask(im, make(2, 1, { 0xFF000000u, 0xFFFFFFFFu })));
    CHECK_EQ(im.pargb[0], 0xFFFF0000u);
    CHECK_EQ(im.pargb[1], 0u);
    CHECK(!img::apply_mask(im, make(1, 1, { 0 })));
}

TEST("image ops: alpha, invert and straight colours work on premultiplied pixels") {
    Pixels im = make(1, 1, { 0xFFFF8000u });
    img::apply_alpha(im, 128);
    CHECK_EQ(im.pargb[0], 0x80804000u);
    CHECK_EQ(img::straight_argb(im.pargb[0]), 0x80FF8000u);
    img::invert(im);
    CHECK_EQ(im.pargb[0], 0x80004080u);
}

TEST("image ops: rotate and flip follow GDI+ RotateFlipType") {
    const Pixels src = make(2, 3, { 1, 2, 3, 4, 5, 6 });
    Pixels r90 = src;
    img::rotate_flip(r90, 1);
    CHECK(r90.w == 3 && r90.h == 2);
    CHECK(r90.pargb == (std::vector<uint32_t>{ 5, 3, 1, 6, 4, 2 }));
    Pixels r180 = src;
    img::rotate_flip(r180, 2);
    CHECK(r180.pargb == (std::vector<uint32_t>{ 6, 5, 4, 3, 2, 1 }));
    Pixels fx = src;
    img::rotate_flip(fx, 4);
    CHECK(fx.pargb == (std::vector<uint32_t>{ 2, 1, 4, 3, 6, 5 }));
    Pixels fy = src;
    img::rotate_flip(fy, 6);
    CHECK(fy.pargb == (std::vector<uint32_t>{ 5, 6, 3, 4, 1, 2 }));
    Pixels r270 = src;
    img::rotate_flip(r270, 3);
    CHECK(r270.pargb == (std::vector<uint32_t>{ 2, 4, 6, 1, 3, 5 }));
}

TEST("image ops: stack blur keeps flat images, spreads a point symmetrically and keeps its energy") {
    Pixels flat = make(5, 4, std::vector<uint32_t>(20, 0xFF204060u));
    img::stack_blur(flat, 3);
    for (uint32_t p : flat.pargb) CHECK_EQ(p, 0xFF204060u);
    Pixels dot = make(9, 9, std::vector<uint32_t>(81, 0));
    dot.pargb[4 * 9 + 4] = 0xFFFFFFFFu;
    img::stack_blur(dot, 2);
    const auto a = [&](int x, int y) { return (int)(dot.pargb[(size_t)y * 9 + x] >> 24); };
    CHECK(a(4, 4) > a(5, 4) && a(5, 4) > a(6, 4) && a(6, 4) > 0 && a(7, 4) == 0);
    CHECK_EQ(a(3, 4), a(5, 4));
    CHECK_EQ(a(4, 3), a(4, 5));
    int sum = 0;
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) sum += a(x, y);
    CHECK(sum > 200 && sum < 320);
}

TEST("image ops: the colour scheme lists the most frequent colours, transparent pixels left out") {
    std::vector<uint32_t> px(100, 0xFF0000FFu);
    for (int i = 0; i < 30; ++i) px[(size_t)i] = 0xFFFF0000u;
    for (int i = 30; i < 40; ++i) px[(size_t)i] = 0x00000000u;
    const auto scheme = img::colour_scheme(make(10, 10, px), 5);
    CHECK_EQ(scheme.size(), (size_t)2);
    CHECK_EQ(scheme[0].first, 0xFF0808F8u);
    CHECK_EQ(scheme[1].first, 0xFFF80808u);
    CHECK(scheme[0].second > 0.66 && scheme[0].second < 0.67);
}

TEST("image ops: gradients follow the angle, carry alpha and peak at the focus") {
    const Pixels h = img::gradient(3, 1, 0, 0xFF000000u, 0xFFFFFFFFu, 1);
    CHECK_EQ(h.pargb[0], 0xFF000000u);
    CHECK_EQ(h.pargb[2], 0xFFFFFFFFu);
    const Pixels v = img::gradient(1, 3, 90, 0x00FF0000u, 0xFFFF0000u, 1);
    CHECK_EQ(v.pargb[0], 0u);
    CHECK_EQ(v.pargb[1] >> 24, 128u);
    CHECK_EQ(v.pargb[2], 0xFFFF0000u);
    const Pixels up = img::gradient(1, 3, 270, 0xFF000000u, 0xFFFFFFFFu, 1);
    CHECK_EQ(up.pargb[2], 0xFF000000u);
    CHECK_EQ(up.pargb[0], 0xFFFFFFFFu);
    const Pixels mid = img::gradient(5, 1, 0, 0xFF000000u, 0xFFFFFFFFu, 0.5);
    CHECK_EQ(mid.pargb[0], 0xFF000000u);
    CHECK_EQ(mid.pargb[2], 0xFFFFFFFFu);
    CHECK_EQ(mid.pargb[4], 0xFF000000u);
}
