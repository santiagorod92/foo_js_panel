#include "../fb2k.h"
#include "diagnostics.h"
#include "log.h"
#include "panel.h"
#include "platform.h"
#include "settings.h"
#include "quickjs.h"
#include <algorithm>

#if __has_include("version_generated.h")
#include "version_generated.h"
#endif
#ifndef JSP_VERSION
#define JSP_VERSION "0.0.0-dev"
#endif

namespace jsp {

namespace {

const char* platform_name() {
#ifdef _WIN32
    return "Windows";
#else
    return "macOS";
#endif
}

std::vector<std::string> installed_components() {
    std::vector<std::string> out;
    service_enum_t<componentversion> e;
    service_ptr_t<componentversion> c;
    while (e.next(c)) {
        pfc::string8 file, name, version;
        try {
            c->get_file_name(file);
            c->get_component_name(name);
            c->get_component_version(version);
        } catch (...) { continue; }
        std::string line = std::string(file.get_ptr());
        if (!name.is_empty()) line += " (" + std::string(name.get_ptr()) + ")";
        if (!version.is_empty()) line += " " + std::string(version.get_ptr());
        out.push_back(line);
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool component_installed(const char* file) {
    const auto components = installed_components();
    return std::any_of(components.begin(), components.end(),
                       [file](const std::string& c) { return c.find(file) != std::string::npos; });
}

std::string session_summary() {
    const std::string wine = ui::wine_version();
    return std::string("foo_js_panel ") + JSP_VERSION + " (" + build_arch() + ") on " +
           core_version_info::g_get_version_string() + ", " + platform_name() + " " + ui::os_version() +
           (wine.empty() ? "" : ", Wine " + wine) + ", QuickJS " + JS_GetVersion() +
           ", verbose=" + (verbose_logging() ? "on" : "off") +
           ", panels_ui=" + (component_installed("foo_ui_panels") ? "yes" : "no");
}

class LogInit : public initquit {
public:
    void on_init() override { start_logging(); }
    void on_quit() override { log::note("env", "foobar2000 is closing"); }
};
FB2K_SERVICE_FACTORY(LogInit);

}

void start_logging() {
    static bool started = false;
    if (started) return;
    started = true;
    log::Logger& l = log::Logger::get();
    l.set_verbose(verbose_logging());
    l.set_console([](const std::string& msg) { console::print(("JS Panel: " + msg).c_str()); });
    try {
        const pfc::string8 native = filesystem::g_get_native_path(core_api::pathInProfile("foo_js_panel.log"));
        l.configure(native.c_str());
    } catch (const std::exception& e) {
        console::printf("JS Panel: can't open the component log: %s", e.what());
    }
    log::note("env", session_summary());
}

std::string collect_diagnostics() {
    DiagnosticsInfo d;
    d.componentVersion = JSP_VERSION;
    d.engineVersion = JS_GetVersion();
    d.foobarVersion = core_version_info::g_get_version_string();
    d.platform = platform_name();
    d.osVersion = ui::os_version();
    d.arch = build_arch();
    d.wineVersion = ui::wine_version();
    d.home = ui::home_dir();
    for (Panel* p : Panel::live()) d.panels.push_back({ p->script_path(), p->status() });
    d.verboseLogging = verbose_logging();
    d.components = installed_components();
    d.panelsUi = component_installed("foo_ui_panels");
    d.logPath = log::Logger::get().path();
    log::info("env", "diagnostics collected: " + std::to_string(d.panels.size()) + " panel(s)");
    d.logLines = log::Logger::get().recent();
    return build_diagnostics(d);
}

}
