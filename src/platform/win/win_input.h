#pragma once
#include "win_sdk.h"
#include <string>

namespace jsp::win {

bool input_box(HWND owner, const std::string& caption, const std::string& prompt, std::string& value);

}
