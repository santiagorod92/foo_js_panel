#pragma once
#include "win_sdk.h"
#include "../../core/panel.h"

namespace jsp::win {

HWND open_editor(HWND existing, const std::string& title, const std::string& text, EditorHooks hooks);
void close_editor(HWND editor);

}
