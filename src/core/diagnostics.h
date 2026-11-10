#pragma once
#include <string>
#include <vector>

namespace jsp {

struct CallTiming {
    std::string name;
    long long calls = 0;
    double totalMs = 0, maxMs = 0;
};

struct PanelInfo {
    std::string script;
    std::string status;
    size_t memoryBytes = 0;
    std::vector<CallTiming> calls = {};
    std::string dialect = {};
    std::vector<std::string> missing = {};
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
