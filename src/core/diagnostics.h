#pragma once
#include <string>
#include <vector>

namespace jsp {

struct PanelInfo {
    std::string script;
    std::string status;
};

struct DiagnosticsInfo {
    std::string componentVersion;
    std::string foobarVersion;
    std::string platform;
    std::string osVersion;
    std::string arch;
    std::string wineVersion;
    std::string home;
    std::string engineVersion;

    std::vector<PanelInfo> panels;
    bool verboseLogging = false;
    bool panelsUi = false;
    std::vector<std::string> components;

    std::string logPath;
    std::vector<std::string> logLines;
};

const char* build_arch();
std::string redact_home(std::string text, const std::string& home);
std::string build_diagnostics(const DiagnosticsInfo& d);
std::string new_issue_url();

std::string collect_diagnostics();
void start_logging();

}
