#include "test.h"
#include "core/editor_console.h"

using namespace jsp;

namespace {

std::string echo(const std::string& code) { return "=" + code; }

}

TEST("editor console: an expression is echoed with its value") {
    EditorConsole c;
    const ConsoleReply r = c.submit("  1 + 1  ", echo);
    CHECK_EQ(r.echo, std::string("> 1 + 1\n"));
    CHECK_EQ(r.text, std::string("=1 + 1\n"));
    CHECK(!r.clear);
    CHECK(c.submit("   ", echo).text.empty());
}

TEST("editor console: :watch, :unwatch and :clear") {
    EditorConsole c;
    CHECK(c.watches(echo).find(":watch") != std::string::npos);
    CHECK_EQ(c.submit(":watch fb.Volume", echo).text, std::string("watch #1: fb.Volume\n"));
    c.submit(":watch a\nb", echo);
    CHECK(c.watching());
    CHECK_EQ(c.watches(echo), std::string("#1  fb.Volume = =fb.Volume\n#2  a\nb = =a b"));
    CHECK_EQ(c.submit(":unwatch 3", echo).text, std::string("no watch #3\n"));
    CHECK_EQ(c.submit(":unwatch 1", echo).text, std::string("watch #1 removed\n"));
    CHECK_EQ(c.watches(echo), std::string("#1  a\nb = =a b"));
    c.submit(":unwatch", echo);
    CHECK(!c.watching());
    CHECK(c.submit(":clear", echo).clear);
    CHECK_EQ(c.submit(":watch", echo).text, std::string("usage: :watch EXPRESSION\n"));
    CHECK_EQ(c.submit(":watchx", echo).text, std::string("=:watchx\n"));
}
