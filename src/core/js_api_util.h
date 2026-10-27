#pragma once
#include "gfx/canvas.h"
#include <cstdint>

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
