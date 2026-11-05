#pragma once
#include <string>
#include <vector>

namespace jsp {

std::string patch_theme_source(const std::string& path, std::string text, std::vector<std::string>* applied = nullptr);

}
