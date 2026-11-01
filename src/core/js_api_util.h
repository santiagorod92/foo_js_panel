#pragma once
#include "gfx/canvas.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
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
