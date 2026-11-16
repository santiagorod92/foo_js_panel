#pragma once
#include "win_sdk.h"
#include <functional>

namespace jsp {
class Panel;
enum class Drag;
}

namespace jsp::win {

IDropTarget* create_drop_target(HWND hwnd, std::function<Panel*()> panel);
int drag_event(Panel* panel, DWORD& allowed, Drag kind, int x, int y, DWORD keys, DWORD offered, IDataObject* data, HWND owner);
int drag_tracks(metadb_handle_list_cref tracks, int effects);

}
