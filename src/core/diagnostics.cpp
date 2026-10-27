#include "diagnostics.h"
#include <cctype>
#include <string>

namespace jsp {

const char* build_arch() {
#if defined(_M_ARM64EC)
    return "ARM64EC";
#elif defined(_M_X64) || defined(__x86_64__)
    return "x64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "arm64";
#elif defined(_M_IX86) || defined(__i386__)
    return "x86";
#else
    return "unknown";
#endif
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

void replace_ci(std::string& text, const std::string& needle, const std::string& with) {
    if (needle.empty()) return;
    const std::string n = lower(needle);
    std::string hay = lower(text);
    size_t pos = 0;
    while ((pos = hay.find(n, pos)) != std::string::npos) {
        text.replace(pos, n.size(), with);
        hay.replace(pos, n.size(), with);
        pos += with.size();
    }
}

bool is_sep(char c) { return c == '/' || c == '\\'; }

std::string flip_seps(std::string s) {
    for (char& c : s) c = c == '/' ? '\\' : c == '\\' ? '/' : c;
    return s;
}

std::string on_off(bool b) { return b ? "on" : "off"; }

}

std::string redact_home(std::string text, const std::string& home) {
    std::string h = home;
    while (h.size() > 1 && is_sep(h.back())) h.pop_back();
    if (h.size() < 2) return text;
    replace_ci(text, h, "~");
    replace_ci(text, flip_seps(h), "~");
    size_t cut = h.size();
    while (cut > 0 && !is_sep(h[cut - 1])) --cut;
    const std::string user = h.substr(cut);
    if (user.size() < 2) return text;
    for (const char* a : { "/", "\\" })
        for (const char* b : { "/", "\\" }) replace_ci(text, a + user + b, std::string(a) + "<user>" + b);
    return text;
}

std::string build_diagnostics(const DiagnosticsInfo& d) {
    auto clean = [&](const std::string& s) { return redact_home(s, d.home); };
    std::string out;
    out += "### foo_js_panel diagnostics\n\n";
    out += "- Component: " + d.componentVersion + " (" + d.arch + "), QuickJS " + d.engineVersion + "\n";
    out += "- foobar2000: " + d.foobarVersion + "\n";
    out += "- OS: " + d.platform + " " + d.osVersion;
    if (!d.wineVersion.empty()) out += ", Wine " + d.wineVersion;
    out += "\n";
    out += "- Panels: " + std::to_string(d.panels.size()) + "\n";
    out += "- Panels UI: " + std::string(d.panelsUi ? "installed" : "not installed") + "\n";
    out += "- Verbose logging: " + on_off(d.verboseLogging) + "\n";
    if (!d.logPath.empty()) out += "- Log file: " + clean(d.logPath) + "\n";

    if (!d.panels.empty()) {
        out += "\n<details><summary>Panels (" + std::to_string(d.panels.size()) + ")</summary>\n\n";
        for (const auto& p : d.panels)
            out += "- " + (p.script.empty() ? std::string("(built-in sample)") : clean(p.script)) + ": " +
                   clean(p.status) + "\n";
        out += "\n</details>\n";
    }

    if (!d.components.empty()) {
        out += "\n<details><summary>Installed components (" + std::to_string(d.components.size()) +
               ")</summary>\n\n";
        for (const auto& c : d.components) out += "- " + c + "\n";
        out += "\n</details>\n";
    }

    out += "\n<details><summary>Recent log (" + std::to_string(d.logLines.size()) + " lines)</summary>\n\n```\n";
    for (const auto& l : d.logLines) out += clean(l) + "\n";
    out += "```\n</details>\n";
    return out;
}

std::string new_issue_url() {
    return "https://github.com/santiagorod92/foo_js_panel/issues/new?template=bug_report.yml";
}

}
