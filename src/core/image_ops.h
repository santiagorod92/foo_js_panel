#pragma once
#include "gfx/canvas.h"
#include <cstdint>
#include <utility>
#include <vector>

namespace jsp::img {

using pui::gfx::Pixels;

Pixels blank(int w, int h);
Pixels crop(const Pixels& src, int x, int y, int w, int h);
Pixels resize(const Pixels& src, int w, int h, bool nearest);
bool apply_mask(Pixels& img, const Pixels& mask);
void apply_alpha(Pixels& img, int alpha);
void invert(Pixels& img);
void rotate_flip(Pixels& img, int mode);
void stack_blur(Pixels& img, int radius);
uint32_t straight_argb(uint32_t pargb);
Pixels gradient(int w, int h, double angle, uint32_t from, uint32_t to, double focus);
std::vector<std::pair<uint32_t, double>> colour_scheme(const Pixels& img, int count);

}
