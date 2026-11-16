#pragma once
#include "panel.h"
#include <memory>

namespace jsp {

std::unique_ptr<Surface> make_owner_surface(void* native_window);
bool windowless_key(Panel& panel, const void* native_event);
int windowless_drag(Panel& panel, int& allowed, Drag kind, int x, int y, unsigned keys, int effects, const void* native_data, void* native_window);

}
