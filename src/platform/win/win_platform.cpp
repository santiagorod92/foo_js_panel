#include "win_sdk.h"
#include "platform/win/gdi_canvas.h"
#include "../../core/platform.h"

namespace jsp::ui {

namespace {

std::wstring widen(const std::string& s) { return std::wstring(pfc::stringcvt::string_wide_from_utf8(s.c_str())); }
std::string narrow(const wchar_t* s) { return std::string(pfc::stringcvt::string_utf8_from_wide(s)); }

}

std::string os_version() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion"))))
            if (fn(&v) == 0)
                return std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + "." +
                       std::to_string(v.dwBuildNumber);
    return "unknown";
}

std::string wine_version() {
    using WineVersionFn = const char* (*)();
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"))
        if (auto fn = reinterpret_cast<WineVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "wine_get_version"))))
            if (const char* v = fn()) return v;
    return {};
}

std::string home_dir() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    return n > 0 && n < MAX_PATH ? narrow(buf) : std::string();
}

bool copy_to_clipboard(const std::string& text) {
    const std::wstring w = widen(text);
    if (!OpenClipboard(nullptr)) return false;
    bool ok = false;
    EmptyClipboard();
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t))) {
        if (void* dst = GlobalLock(mem)) {
            memcpy(dst, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            ok = SetClipboardData(CF_UNICODETEXT, mem) != nullptr;
        }
        if (!ok) GlobalFree(mem);
    }
    CloseClipboard();
    return ok;
}

}

namespace jsp {

namespace {

class GdiShutdown : public initquit {
public:
    void on_quit() override {
        pui::gfx::gdi_fonts_shutdown();
        pui::gfx::platform_images_shutdown();
    }
};
FB2K_SERVICE_FACTORY(GdiShutdown);

}

}
