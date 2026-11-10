#include "test.h"
#include "core/diagnostics.h"

using namespace jsp;

TEST("diagnostics: redact_home hides the home folder and user name") {
    CHECK_EQ(redact_home("C:\\Users\\Ana\\AppData\\foo.log", "C:\\Users\\Ana"), std::string("~\\AppData\\foo.log"));
    CHECK_EQ(redact_home("c:/users/ana/x", "C:\\Users\\Ana\\"), std::string("~/x"));
    CHECK_EQ(redact_home("Z:\\home\\ana\\skins", "C:\\users\\ana"), std::string("Z:\\home\\<user>\\skins"));
    CHECK_EQ(redact_home("/Users/ana/Library", "/Users/ana"), std::string("~/Library"));
    CHECK_EQ(redact_home("nothing here", ""), std::string("nothing here"));
}

TEST("diagnostics: report has versions, panels and the log") {
    DiagnosticsInfo d;
    d.componentVersion = "1.2.3";
    d.engineVersion = "0.17.0";
    d.foobarVersion = "foobar2000 v2.26";
    d.platform = "Windows";
    d.osVersion = "10.0.26100";
    d.arch = "ARM64EC";
    d.home = "C:\\Users\\ana";
    d.panels = { { "C:\\Users\\ana\\scripts\\main.js", "running" }, { "", "error: SyntaxError" } };
    d.panelsUi = true;
    d.logLines = { "12:00:00.000  WARN   js  C:\\Users\\ana\\x" };
    const std::string r = build_diagnostics(d);
    CHECK(r.find("- Component: 1.2.3 (ARM64EC), QuickJS 0.17.0") != std::string::npos);
    CHECK(r.find("- OS: Windows 10.0.26100\n") != std::string::npos);
    CHECK(r.find("- Panels: 2") != std::string::npos);
    CHECK(r.find("- Panels UI: installed") != std::string::npos);
    CHECK(r.find("- ~\\scripts\\main.js: running") != std::string::npos);
    CHECK(r.find("- (built-in sample): error: SyntaxError") != std::string::npos);
    CHECK(r.find("WARN   js  ~\\x") != std::string::npos);
    CHECK(r.find("ana") == std::string::npos);
}

TEST("diagnostics: panels show their memory and slowest callbacks, at most five") {
    jsp::DiagnosticsInfo d;
    jsp::PanelInfo p{ "/s/main.js", "running", 3 * 1048576 / 2 };
    for (int i = 0; i < 6; ++i) p.calls.push_back({ "cb" + std::to_string(i), 4, 10.0 - i, 5.0 });
    d.panels = { p };
    const std::string r = jsp::build_diagnostics(d);
    CHECK(r.find("- /s/main.js: running, 1.5 MB\n  - cb0: 4 calls, 10.0 ms total, 2.50 ms avg, 5.0 ms max\n") != std::string::npos);
    CHECK(r.find("cb4") != std::string::npos && r.find("cb5") == std::string::npos);
}

TEST("diagnostics: panels name their script dialect and the API names they use that are missing") {
    jsp::DiagnosticsInfo d;
    jsp::PanelInfo p{ "/s/wsh.js", "running" };
    p.dialect = "WSH Panel Mod";
    for (int i = 0; i < 22; ++i) p.missing.push_back("fb.Name" + std::to_string(i));
    d.panels = { p };
    const std::string r = jsp::build_diagnostics(d);
    CHECK(r.find("- /s/wsh.js: running (WSH Panel Mod)\n") != std::string::npos);
    CHECK(r.find("  - Not implemented: fb.Name0, fb.Name1, ") != std::string::npos);
    CHECK(r.find("fb.Name19 and 2 more\n") != std::string::npos);
}

TEST("diagnostics: no panels, Wine") {
    DiagnosticsInfo d;
    d.platform = "Windows";
    d.wineVersion = "11.0";
    const std::string r = build_diagnostics(d);
    CHECK(r.find("- Panels: 0") != std::string::npos);
    CHECK(r.find(", Wine 11.0") != std::string::npos);
    CHECK(r.find("Recent log (0 lines)") != std::string::npos);
}
