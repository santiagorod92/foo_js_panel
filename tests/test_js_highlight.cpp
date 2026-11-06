#include "core/js_highlight.h"
#include "test.h"

using namespace jsp;

namespace {

std::string spans(const std::string& src) {
    static const char* const names[] = { "kw", "lit", "str", "num", "com", "re" };
    std::string out;
    for (const Token& t : highlight_js(src)) out += std::string(names[(int)t.kind]) + ":" + std::to_string(t.start) + "+" + std::to_string(t.length) + " ";
    return out;
}

}

TEST("highlight: keywords, literals, numbers and strings; member names stay plain") {
    CHECK_EQ(spans("const x = true; obj.return = 0x1F + 1e-3; 'a\\'b'"),
             std::string("kw:0+5 lit:10+4 num:29+4 num:36+4 str:42+6 "));
}

TEST("highlight: comments, regex after operators but division after values") {
    CHECK_EQ(spans("// hi\nlet r = /a[/]b/g; a / b / c; /* x\ny */"), std::string("com:0+5 kw:6+3 re:14+8 com:35+9 "));
    CHECK_EQ(spans("return /x/.test(s)"), std::string("kw:0+6 re:7+3 "));
}

TEST("highlight: template literals with nested code; offsets are UTF-16 units") {
    CHECK_EQ(spans("`a${ {b: 1}.b }c` + 2"), std::string("str:0+4 num:9+1 str:14+3 num:20+1 "));
    CHECK_EQ(spans("'\xc3\xa9\xf0\x9f\x8e\xb5' + 1"), std::string("str:0+5 num:8+1 "));
}
