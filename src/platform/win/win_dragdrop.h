#pragma once
#include "win_sdk.h"
#include <functional>

namespace jsp {
class Panel;
}

namespace jsp::win {

IDropTarget* create_drop_target(HWND hwnd, std::function<Panel*()> panel);
int drag_tracks(metadb_handle_list_cref tracks, int effects);

}
