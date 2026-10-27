#include "../fb2k.h"
#include "settings.h"
#include "diagnostics.h"
#include "log.h"
#include "platform.h"

namespace jsp {

namespace {

constexpr GUID kVerboseGuid = { 0xBBE2ECF8, 0x9204, 0x42D9, { 0xA7, 0xF1, 0x0D, 0x52, 0x93, 0xB9, 0x50, 0x80 } };
constexpr GUID kMenuGroupGuid = { 0xDF1018F2, 0x36D7, 0x4DF9, { 0xAE, 0xC4, 0xB3, 0xC5, 0x55, 0x79, 0x44, 0x53 } };
constexpr GUID kVerboseCmdGuid = { 0x10914481, 0xB7F2, 0x4A6E, { 0xB1, 0x84, 0x42, 0x29, 0xD5, 0x1B, 0x66, 0xB5 } };
constexpr GUID kCopyDiagCmdGuid = { 0x48994873, 0x45A0, 0x483C, { 0xBA, 0xD1, 0x2D, 0xB0, 0xF5, 0x87, 0xFE, 0x6F } };
constexpr GUID kDefaultScriptGuid = { 0x4876F6EE, 0xF0D3, 0x47D7, { 0xBF, 0xA4, 0x58, 0xD6, 0x77, 0xFC, 0x9A, 0xDF } };

cfg_bool cfg_verbose(kVerboseGuid, false);
cfg_string cfg_default_script(kDefaultScriptGuid, "");

mainmenu_group_popup_factory g_group(kMenuGroupGuid, mainmenu_groups::view, mainmenu_commands::sort_priority_dontcare,
                                     "JS Panel");

class JsPanelMenu : public mainmenu_commands {
public:
    enum { kVerbose, kCopyDiagnostics, kCount };
    t_uint32 get_command_count() override { return kCount; }
    GUID get_command(t_uint32 i) override { return i == kVerbose ? kVerboseCmdGuid : kCopyDiagCmdGuid; }
    void get_name(t_uint32 i, pfc::string_base& out) override {
        out = i == kVerbose ? "Verbose logging" : "Copy diagnostics";
    }
    bool get_description(t_uint32 i, pfc::string_base& out) override {
        out = i == kVerbose ? "Write INFO lines to foo_js_panel.log in the profile folder."
                            : "Copy a redacted report (versions, panels, recent log) to paste into a bug report.";
        return true;
    }
    GUID get_parent() override { return kMenuGroupGuid; }
    bool get_display(t_uint32 i, pfc::string_base& text, t_uint32& flags) override {
        get_name(i, text);
        flags = i == kVerbose && verbose_logging() ? flag_checked : 0;
        return true;
    }
    void execute(t_uint32 i, service_ptr_t<service_base>) override {
        if (i == kVerbose) {
            set_verbose_logging(!verbose_logging());
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

std::string default_script() { return cfg_default_script.get().c_str(); }

void set_default_script(const std::string& path) { cfg_default_script = path.c_str(); }

void set_verbose_logging(bool on) {
    cfg_verbose = on;
    log::Logger::get().set_verbose(on);
    log::note("env", std::string("verbose logging ") + (on ? "on" : "off"));
}

}
