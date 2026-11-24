#pragma once
#include <string>

namespace jsp::ui {

std::string os_version();
std::string wine_version();
std::string home_dir();
std::string app_dir();
bool copy_to_clipboard(const std::string& text);
std::string clipboard_text();
bool shell_run(const std::string& command);
std::string map_chinese(const std::string& text, bool traditional);
bool move_to_trash(const std::string& path);
int system_metric(int index);
bool register_font(const std::string& path);

}
