#include "test.h"
#include <cstdlib>
#include <cstring>

int main() {
    int failedCases = 0;
    const bool verbose = std::getenv("T_VERBOSE") != nullptr;
    const char* only = std::getenv("T_ONLY");
    for (auto& c : t::cases()) {
        if (only && !std::strstr(c.name, only)) continue;
        if (verbose) { std::fprintf(stderr, "... %s\n", c.name); std::fflush(stderr); }
        const int before = t::failures();
        c.fn();
        if (t::failures() != before) { ++failedCases; std::fprintf(stderr, "FAIL %s\n", c.name); }
    }
    std::printf("%zu tests, %d failed\n", t::cases().size(), failedCases);
    return failedCases ? 1 : 0;
}
