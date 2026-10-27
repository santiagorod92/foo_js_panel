#pragma once
#include <string>

namespace jsp::ui {

std::string os_version();
std::string wine_version();
std::string home_dir();
bool copy_to_clipboard(const std::string& text);

}
