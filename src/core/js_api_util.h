#pragma once
#include "gfx/canvas.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace jsp {

inline pui::gfx::Color argb_color(uint32_t argb) {
    return pui::gfx::Color((int)((argb >> 16) & 0xff), (int)((argb >> 8) & 0xff), (int)(argb & 0xff));
}

inline int argb_alpha(uint32_t argb) { return (int)(argb >> 24); }

enum : uint32_t {
    DT_CENTER_ = 0x1,
    DT_RIGHT_ = 0x2,
    DT_VCENTER_ = 0x4,
    DT_BOTTOM_ = 0x8,
    DT_WORDBREAK_ = 0x10,
    DT_SINGLELINE_ = 0x20,
    DT_NOCLIP_ = 0x100,
    DT_CALCRECT_ = 0x400,
    DT_END_ELLIPSIS_ = 0x8000,
    DT_WORD_ELLIPSIS_ = 0x40000,
};

inline unsigned dt_to_text_flags(uint32_t dt) {
    namespace g = pui::gfx;
    unsigned f = 0;
    if (dt & DT_CENTER_) f |= g::kAlignCenter;
    else if (dt & DT_RIGHT_) f |= g::kAlignRight;
    if (dt & DT_VCENTER_) f |= g::kVCenter;
    if (dt & DT_SINGLELINE_) f |= g::kSingleLine;
    if (dt & DT_WORDBREAK_) f |= g::kWordWrap;
    if (dt & DT_NOCLIP_) f |= g::kNoClip;
    if (dt & DT_END_ELLIPSIS_) f |= g::kEndEllipsis;
    if (dt & DT_WORD_ELLIPSIS_) f |= g::kWordEllipsis;
    return f;
}

inline std::string windows_separators(std::string path) {
#ifndef _WIN32
    std::replace(path.begin(), path.end(), '/', '\\');
#endif
    return path;
}

inline pui::gfx::Rect dt_text_rect(pui::gfx::Rect r, uint32_t dt, int text_h) {
    if (dt & DT_CALCRECT_) {
        if (dt & DT_VCENTER_) return { r.x, r.y + (r.h - text_h) / 2, r.w, text_h };
        if (dt & DT_BOTTOM_) return { r.x, r.y + r.h - text_h, r.w, text_h };
        return r;
    }
    if ((dt & DT_BOTTOM_) && (dt & DT_SINGLELINE_) && !(dt & DT_VCENTER_) && text_h < r.h)
        return { r.x, r.y + r.h - text_h, r.w, text_h };
    return r;
}

inline unsigned string_format_to_text_flags(uint32_t sf) {
    namespace g = pui::gfx;
    const uint32_t align = (sf >> 28) & 0xf, line = (sf >> 24) & 0xf, trim = (sf >> 20) & 0xf,
                   flags = sf & 0xffff;
    unsigned f = 0;
    if (align == 1) f |= g::kAlignCenter;
    else if (align == 2) f |= g::kAlignRight;
    if (line == 1) f |= g::kVCenter;
    if (trim == 3 || trim == 5) f |= g::kEndEllipsis;
    else if (trim == 4) f |= g::kWordEllipsis;
    if (flags & 0x1000) f |= g::kSingleLine;
    else f |= g::kWordWrap;
    if (flags & 0x4000) f |= g::kNoClip;
    return f;
}

enum : int {
    VK_BACK_ = 0x08, VK_TAB_ = 0x09, VK_RETURN_ = 0x0D, VK_SHIFT_ = 0x10, VK_CONTROL_ = 0x11, VK_MENU_ = 0x12,
    VK_ESCAPE_ = 0x1B, VK_SPACE_ = 0x20, VK_PRIOR_ = 0x21, VK_NEXT_ = 0x22, VK_END_ = 0x23, VK_HOME_ = 0x24,
    VK_LEFT_ = 0x25, VK_UP_ = 0x26, VK_RIGHT_ = 0x27, VK_DOWN_ = 0x28, VK_DELETE_ = 0x2E, VK_F1_ = 0x70,
};

inline int mac_key_to_vk(int keyCode, char32_t ch) {
    switch (keyCode) {
    case 36: case 76: return VK_RETURN_;
    case 48: return VK_TAB_;
    case 49: return VK_SPACE_;
    case 51: return VK_BACK_;
    case 53: return VK_ESCAPE_;
    case 117: return VK_DELETE_;
    case 115: return VK_HOME_;
    case 119: return VK_END_;
    case 116: return VK_PRIOR_;
    case 121: return VK_NEXT_;
    case 123: return VK_LEFT_;
    case 124: return VK_RIGHT_;
    case 125: return VK_DOWN_;
    case 126: return VK_UP_;
    case 122: return VK_F1_;
    case 120: return VK_F1_ + 1;
    case 99: return VK_F1_ + 2;
    case 118: return VK_F1_ + 3;
    case 96: return VK_F1_ + 4;
    case 97: return VK_F1_ + 5;
    default: break;
    }
    if (ch >= 'a' && ch <= 'z') return (int)(ch - 'a' + 'A');
    if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) return (int)ch;
    return 0;
}

// utils.IsKeyPressed for the modifiers on macOS. ⌘ is VK_LWIN/VK_RWIN (the key in its place on a
// Windows keyboard), so a script can tell ⌘A from ⌃A; it also still counts as VK_CONTROL, so
// scripts written for Windows take ⌘C as Ctrl+C.
struct MacModifiers { bool shift = false, control = false, option = false, command = false; };
inline bool mac_modifier_pressed(int vk, const MacModifiers& m) {
    switch (vk) {
    case 0x10: return m.shift;
    case 0x11: return m.control || m.command;
    case 0x12: return m.option;
    case 0x5B: case 0x5C: return m.command;
    default: return false;
    }
}

enum class PowerAction { None, Shutdown, Restart, Sleep };

inline PowerAction windows_power_command(const std::string& command) {
    std::vector<std::string> words;
    std::string word;
    for (char ch : command + " ") {
        if (ch == ' ' || ch == '"') {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += (char)std::tolower((unsigned char)ch);
        }
    }
    if (words.empty()) return PowerAction::None;
    std::string file = words[0].substr(words[0].find_last_of("/\\") + 1);
    if (file != "shutdown" && file != "shutdown.exe") return PowerAction::None;
    PowerAction action = PowerAction::None;
    for (size_t i = 1; i < words.size(); ++i) {
        const std::string& w = words[i];
        if (w.size() != 2 || (w[0] != '/' && w[0] != '-')) continue;
        if (w[1] == 's') action = PowerAction::Shutdown;
        else if (w[1] == 'r') action = PowerAction::Restart;
        else if (w[1] == 'h') action = PowerAction::Sleep;
    }
    return action;
}

inline std::string menu_label(const std::string& text) {
    std::string out;
    const std::string label = text.substr(0, text.find('\t'));
    for (size_t i = 0; i < label.size(); ++i) {
        if (label[i] != '&') out += label[i];
        else if (i + 1 < label.size() && label[i + 1] == '&') out += label[++i];
    }
    return out;
}

inline unsigned mac_char_to_wm_char(char32_t ch, bool command) {
    if (command || (ch >= 0xF700 && ch <= 0xF8FF)) return 0;
    switch (ch) {
    case 0x7f: return VK_BACK_;
    case 0x03: case '\n': case '\r': return VK_RETURN_;
    case 0x08: case '\t': case 0x1b: return (unsigned)ch;
    default: break;
    }
    return ch < 0x20 ? 0 : (unsigned)ch;
}

inline std::string with_fallback_rating(const std::string& expr) {
    static const std::string field = "%rating%", fallback = "$if2(%rating%,$meta(NAVIDROME_RATING))";
    std::string out;
    out.reserve(expr.size());
    for (size_t i = 0; i < expr.size();) {
        bool hit = expr.size() - i >= field.size();
        for (size_t k = 0; hit && k < field.size(); ++k)
            hit = std::tolower((unsigned char)expr[i + k]) == field[k];
        if (hit) {
            out += fallback;
            i += field.size();
        } else {
            out += expr[i++];
        }
    }
    return out;
}

struct ScriptHeader {
    bool present = false;
    std::vector<std::string> imports, features;
    std::string name, author, version;
};

inline ScriptHeader parse_header(const std::string& src) {
    ScriptHeader h;
    const size_t begin = src.find("==PREPROCESSOR==");
    if (begin == std::string::npos) return h;
    const size_t end = src.find("==/PREPROCESSOR==", begin);
    if (end == std::string::npos) return h;
    h.present = true;
    size_t pos = src.find('\n', begin);
    while (pos != std::string::npos && pos < end) {
        const size_t next = src.find('\n', pos + 1);
        const std::string line = src.substr(pos + 1, (next == std::string::npos ? src.size() : next) - pos - 1);
        for (auto [key, field, list] : { std::tuple{ "@import", (std::string*)nullptr, &h.imports },
                                         std::tuple{ "@feature", (std::string*)nullptr, &h.features },
                                         std::tuple{ "@name", &h.name, (std::vector<std::string>*)nullptr },
                                         std::tuple{ "@author", &h.author, (std::vector<std::string>*)nullptr },
                                         std::tuple{ "@version", &h.version, (std::vector<std::string>*)nullptr } }) {
            const size_t at = line.find(key);
            if (at == std::string::npos) continue;
            const size_t q1 = line.find('"', at), q2 = q1 == std::string::npos ? q1 : line.find('"', q1 + 1);
            if (q2 == std::string::npos) continue;
            const std::string value = line.substr(q1 + 1, q2 - q1 - 1);
            if (field) *field = value;
            else list->push_back(value);
        }
        pos = next;
    }
    return h;
}

inline std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
    return s;
}

inline std::string resolve_import(const std::string& spec, const std::string& scriptDir, const std::string& componentDir,
                                  const std::string& profileDir, const std::string& foobarDir) {
    std::string p = replace_all(replace_all(replace_all(spec, "%fb2k_component_path%", componentDir), "%fb2k_profile_path%", profileDir),
                                "%fb2k_path%", foobarDir);
    const bool absolute = !p.empty() && (p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':'));
    return absolute ? p : scriptDir + p;
}

inline bool is_jsp3_script(const std::string& src, const ScriptHeader& h) {
    for (const auto& i : h.imports)
        if (i == "lodash" || i.find("%fb2k_component_path%") != std::string::npos) return true;
    static const char* const only[] = { ".WriteText(", ".WriteText2(", ".WriteTextSimple(", ".FillRectangle(", ".DrawRectangle(",
                                        ".FillRoundedRectangle(", "utils.ReadUTF8(", "utils.ListFiles(", "window.IsDark" };
    for (const char* n : only)
        if (src.find(n) != std::string::npos) return true;
    return false;
}

inline bool is_wsh_script(const std::string& src, const ScriptHeader& h) {
    if (!h.features.empty()) return true;
    std::string lower(src);
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    static const char* const only[] = { "collectgarbage(", "new vbarray(", "new enumerator(", ".toarray()", "getcolorcui(", "getcolordui(",
                                        "utils.getsyscolor(", "createtimerinterval(", "createtimertimeout(", "watchmetadb(",
                                        "createstyletextrender(", "on_colors_changed", ".boxblur(", "getcolorscheme(", "fb.trace(",
                                        "fb.playbackorder", "fb.playlistcount", "fb.activeplaylist", "fb.playingplaylist",
                                        "fb.getplaylistname(", "pathwildcardmatch(", "getbackgroundimage(" };
    for (const char* n : only)
        if (lower.find(n) != std::string::npos) return true;
    return false;
}

inline std::string resolve_script_path(const std::string& p, const std::string& home, const std::string& profile) {
    auto join = [](std::string dir, const std::string& rest) {
        if (!dir.empty() && dir.back() != '/' && dir.back() != '\\') dir += '/';
        return dir + rest;
    };
    if (p.empty()) return p;
    if (p == "~") return home;
    if (p.rfind("~/", 0) == 0) return join(home, p.substr(2));
    const bool absolute = p[0] == '/' || p[0] == '\\' || (p.size() > 1 && p[1] == ':');
    return absolute || profile.empty() ? p : join(profile, p);
}

inline std::string decode_text(std::string b) {
    const auto u = [&](size_t i) { return (unsigned char)b[i]; };
    if (b.size() >= 3 && u(0) == 0xEF && u(1) == 0xBB && u(2) == 0xBF) return b.erase(0, 3);
    if (b.size() < 2 || !((u(0) == 0xFF && u(1) == 0xFE) || (u(0) == 0xFE && u(1) == 0xFF))) return b;
    const bool le = u(0) == 0xFF;
    std::string out;
    out.reserve(b.size());
    const auto unit = [&](size_t i) { return le ? (uint32_t)(u(i) | (u(i + 1) << 8)) : (uint32_t)((u(i) << 8) | u(i + 1)); };
    for (size_t i = 2; i + 1 < b.size(); i += 2) {
        uint32_t c = unit(i);
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < b.size()) {
            const uint32_t lo = unit(i + 2);
            if (lo >= 0xDC00 && lo < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            }
        }
        if (c < 0x80) out += (char)c;
        else if (c < 0x800) out += {(char)(0xC0 | (c >> 6)), (char)(0x80 | (c & 0x3F))};
        else if (c < 0x10000) out += {(char)(0xE0 | (c >> 12)), (char)(0x80 | ((c >> 6) & 0x3F)), (char)(0x80 | (c & 0x3F))};
        else out += {(char)(0xF0 | (c >> 18)), (char)(0x80 | ((c >> 12) & 0x3F)), (char)(0x80 | ((c >> 6) & 0x3F)), (char)(0x80 | (c & 0x3F))};
    }
    return out;
}

inline int detect_codepage(const std::string& b) {
    if (b.size() >= 2 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xFE) return 1200;
    if (b.size() >= 2 && (unsigned char)b[0] == 0xFE && (unsigned char)b[1] == 0xFF) return 1201;
    if (b.size() >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF) return 65001;
    for (size_t i = 0; i < b.size();) {
        const unsigned char c = (unsigned char)b[i];
        const int n = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 && c >= 0xC2 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 && c <= 0xF4 ? 3 : -1;
        if (n < 0 || (n > 0 && i + n >= b.size())) return 1252;
        for (int k = 1; k <= n; ++k)
            if (((unsigned char)b[i + k] & 0xC0) != 0x80) return 1252;
        i += n + 1;
    }
    return 65001;
}

inline const std::vector<std::string>& playlist_lock_actions() {
    static const std::vector<std::string> names = { "AddItems", "RemoveItems", "ReorderItems", "ReplaceItems",
                                                    "RenamePlaylist", "RemovePlaylist", "ExecuteDefaultAction" };
    return names;
}

inline unsigned lock_mask_of(const std::vector<std::string>& actions, std::string& unknown) {
    const auto& names = playlist_lock_actions();
    unsigned mask = 0;
    for (const auto& a : actions) {
        const auto it = std::find(names.begin(), names.end(), a);
        if (it == names.end()) {
            unknown = a;
            return 0;
        }
        mask |= 1u << (it - names.begin());
    }
    return mask;
}

inline std::vector<std::string> lock_actions_of(unsigned mask) {
    std::vector<std::string> out;
    const auto& names = playlist_lock_actions();
    for (size_t i = 0; i < names.size(); ++i)
        if (mask & (1u << i)) out.push_back(names[i]);
    return out;
}

enum FontStyle : int { kFontBold = 1, kFontItalic = 2, kFontUnderline = 4 };

inline pui::gfx::FontSpec font_spec(const std::string& name, float px, int style) {
    pui::gfx::FontSpec s;
    s.face = name;
    s.size = px;
    s.points = false;
    s.bold = (style & kFontBold) != 0;
    s.italic = (style & kFontItalic) != 0;
    s.underline = (style & kFontUnderline) != 0;
    return s;
}

}
