#pragma once
#include <string>

namespace jsp {

bool verbose_logging();
void set_verbose_logging(bool on);
std::string default_script();
void set_default_script(const std::string& path);

}
