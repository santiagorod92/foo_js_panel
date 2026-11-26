#include "../fb2k.h"
#include "settings.h"
#include "diagnostics.h"
#include "fs_util.h"
#include "js_api_util.h"
#include "log.h"
#include "panel.h"
#include "platform.h"

namespace jsp {

namespace {

constexpr GUID kVerboseGuid = { 0xBBE2ECF8, 0x9204, 0x42D9, { 0xA7, 0xF1, 0x0D, 0x52, 0x93, 0xB9, 0x50, 0x80 } };
constexpr GUID kMenuGroupGuid = { 0xDF1018F2, 0x36D7, 0x4DF9, { 0xAE, 0xC4, 0xB3, 0xC5, 0x55, 0x79, 0x44, 0x53 } };
constexpr GUID kVerboseCmdGuid = { 0x10914481, 0xB7F2, 0x4A6E, { 0xB1, 0x84, 0x42, 0x29, 0xD5, 0x1B, 0x66, 0xB5 } };
constexpr GUID kCopyDiagCmdGuid = { 0x48994873, 0x45A0, 0x483C, { 0xBA, 0xD1, 0x2D, 0xB0, 0xF5, 0x87, 0xFE, 0x6F } };
constexpr GUID kNavidromeLibraryGuid = { 0x7E1C5A39, 0x2B64, 0x4F0D, { 0x8C, 0x51, 0xA9, 0x3E, 0x06, 0xD2, 0x4B, 0x17 } };
constexpr GUID kNavidromeLibraryCmdGuid = { 0x91D4E6A2, 0x5C83, 0x4B7E, { 0xB0, 0x2F, 0x6A, 0x18, 0xC9, 0x3D, 0x75, 0xE4 } };
constexpr GUID kScriptCommandsGuid = { 0xAD1F2B8C, 0x9666, 0x4E65, { 0x93, 0xC7, 0xD8, 0xDB, 0x58, 0xBC, 0xCB, 0x8E } };
constexpr GUID kHotReloadGuid = { 0x3E9A7C21, 0x5B04, 0x4D8F, { 0x9A, 0x61, 0x2C, 0x7E, 0x40, 0xB3, 0x18, 0xD5 } };
constexpr GUID kHotReloadCmdGuid = { 0x6F2D8B40, 0x91C3, 0x4E57, { 0xB2, 0x0A, 0x7D, 0x35, 0xE9, 0x46, 0x0C, 0x81 } };
constexpr GUID kScaleDpiGuid = { 0x5C2E9B71, 0x0A4D, 0x4F36, { 0x8E, 0x13, 0xB7, 0x62, 0xD9, 0x04, 0x3A, 0xC8 } };
constexpr GUID kScaleDpiCmdGuid = { 0xA3F07D52, 0x6E19, 0x4B8C, { 0x95, 0x2D, 0x41, 0xC8, 0x7B, 0xE6, 0x10, 0x9F } };
constexpr GUID kDefaultScriptGuid = { 0x4876F6EE, 0xF0D3, 0x47D7, { 0xBF, 0xA4, 0x58, 0xD6, 0x77, 0xFC, 0x9A, 0xDF } };

cfg_bool cfg_verbose(kVerboseGuid, false);
cfg_string cfg_default_script(kDefaultScriptGuid, "");
cfg_bool cfg_navidrome_library(kNavidromeLibraryGuid, true);
cfg_bool cfg_hot_reload(kHotReloadGuid, true);
cfg_bool cfg_scale_dpi(kScaleDpiGuid, false);

#ifdef _WIN32
constexpr bool kCaselessPaths = true;
#else
constexpr bool kCaselessPaths = false;
#endif

mainmenu_group_popup_factory g_group(kMenuGroupGuid, mainmenu_groups::view, mainmenu_commands::sort_priority_dontcare,
                                     "JS Panel");

mainmenu_group_popup_factory g_scriptGroup(kScriptCommandsGuid, kMenuGroupGuid, mainmenu_commands::sort_priority_base,
                                           "Script commands");

class ScriptCommands : public mainmenu_commands {
public:
    static constexpr t_uint32 kCount = 10;
    t_uint32 get_command_count() override { return kCount; }
    GUID get_command(t_uint32 i) override {
        GUID g = { 0xD62DE1D6, 0x6729, 0x4ACD, { 0x92, 0x6B, 0x59, 0x74, 0x10, 0x99, 0x23, 0x5F } };
        g.Data4[7] = (unsigned char)(g.Data4[7] + i);
        return g;
    }
    void get_name(t_uint32 i, pfc::string_base& out) override { out = pfc::format("Script command ", i + 1); }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        out = pfc::format("Calls on_main_menu(", i + 1, ") in every JS Panel.");
        return true;
    }
    GUID get_parent() override { return kScriptCommandsGuid; }
    void execute(t_uint32 i, service_ptr_t<service_base>) override { Panel::broadcast("on_main_menu", { (int)i + 1 }); }
};
FB2K_SERVICE_FACTORY(ScriptCommands);

class JsPanelMenu : public mainmenu_commands {
public:
    enum { kVerbose, kNavidromeLibrary, kHotReload, kScaleDpi, kCopyDiagnostics, kCount };
    t_uint32 get_command_count() override { return kCount; }
    GUID get_command(t_uint32 i) override {
        return i == kVerbose ? kVerboseCmdGuid : i == kNavidromeLibrary ? kNavidromeLibraryCmdGuid : i == kHotReload ? kHotReloadCmdGuid
             : i == kScaleDpi ? kScaleDpiCmdGuid : kCopyDiagCmdGuid;
    }
    void get_name(t_uint32 i, pfc::string_base& out) override {
        out = i == kVerbose ? "Verbose logging" : i == kNavidromeLibrary ? "Navidrome tracks in the library"
            : i == kHotReload ? "Reload scripts when their files change"
            : i == kScaleDpi ? "Scale scripts with the display DPI" : "Copy diagnostics";
    }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        out = i == kVerbose ? "Write INFO lines to foo_js_panel.log in the profile folder."
            : i == kNavidromeLibrary ? "Scripts see foo_navidrome's tracks in fb.GetLibraryItems(), next to the Media Library (applies on restart)."
            : i == kHotReload ? "A panel reloads when its script or a file it includes is saved, from any editor."
            : i == kScaleDpi ? "window.DPI and the AppliedDPI registry value report the display's DPI instead of 96, so scripts that scale by them (JScript Panel 3) size their fonts for the display."
                              : "Copy a redacted report (versions, panels, recent log) to paste into a bug report.";
        return true;
    }
    GUID get_parent() override { return kMenuGroupGuid; }
    bool get_display(t_uint32 i, pfc::string_base& text, t_uint32& flags) override {
        get_name(i, text);
        flags = (i == kVerbose && verbose_logging()) || (i == kNavidromeLibrary && navidrome_in_library()) || (i == kHotReload && hot_reload()) ||
                        (i == kScaleDpi && scale_dpi())
                    ? flag_checked : 0;
        return true;
    }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i == kVerbose) {
            set_verbose_logging(!verbose_logging());
            return;
        }
        if (i == kNavidromeLibrary) {
            cfg_navidrome_library = !cfg_navidrome_library.get();
            return;
        }
        if (i == kHotReload) {
            cfg_hot_reload = !cfg_hot_reload.get();
            return;
        }
        if (i == kScaleDpi) {
            cfg_scale_dpi = !cfg_scale_dpi.get();
            for (Panel* p : Panel::live()) p->reload_script();
            return;
        }
        const bool ok = ui::copy_to_clipboard(collect_diagnostics());
        const std::string msg = ok ? "The diagnostics report is on the clipboard. Paste it into a bug report:\n" +
                                         new_issue_url()
                                   : std::string("Couldn't copy to the clipboard.");
        popup_message::g_show(msg.c_str(), "JS Panel");
    }
};
FB2K_SERVICE_FACTORY(JsPanelMenu);

}

bool verbose_logging() { return cfg_verbose.get(); }

bool navidrome_in_library() { return cfg_navidrome_library.get(); }

bool hot_reload() { return cfg_hot_reload.get(); }

bool scale_dpi() { return cfg_scale_dpi.get(); }

std::string profile_dir() {
    std::string p = filesystem::g_get_native_path(core_api::get_profile_path()).c_str();
    if (!p.empty() && p.back() != kPathSep) p += kPathSep;
    return p;
}

std::string stored_script_path(const std::string& path) { return portable_path(path, profile_dir(), kCaselessPaths); }

std::string script_from_stored(const std::string& stored) { return expand_portable_path(stored, profile_dir(), kPathSep); }

std::string default_script() { return script_from_stored(cfg_default_script.get().c_str()); }

void set_default_script(const std::string& path) { cfg_default_script = stored_script_path(path).c_str(); }

void set_verbose_logging(bool on) {
    cfg_verbose = on;
    log::Logger::get().set_verbose(on);
    log::note("env", std::string("verbose logging ") + (on ? "on" : "off"));
}

}
