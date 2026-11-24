#include "editor_console.h"

#include <cstdlib>

namespace jsp {
namespace {

std::string trim(const std::string& s) {
    const size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

bool command(const std::string& line, const char* name, std::string& rest) {
    const std::string n(name);
    if (line.compare(0, n.size(), n) != 0) return false;
    if (line.size() > n.size() && line[n.size()] != ' ') return false;
    rest = trim(line.substr(n.size()));
    return true;
}

std::string one_line(std::string s) {
    for (char& c : s)
        if (c == '\n' || c == '\r') c = ' ';
    if (s.size() > 300) s = s.substr(0, 300) + "…";
    return s;
}

}

ConsoleReply EditorConsole::submit(const std::string& input, const Eval& eval) {
    const std::string line = trim(input);
    if (line.empty()) return {};
    std::string rest;
    if (line == ":clear") return { {}, true };
    if (line == ":help")
        return { "Type an expression to evaluate it in the panel's global scope (window, fb, plman, your own variables).\n"
                 ":watch EXPR  keep EXPR's value on show above   :unwatch N  drop watch N (:unwatch alone drops all)\n"
                 ":clear  empty this console\n" };
    if (command(line, ":watch", rest)) {
        if (rest.empty()) return { "usage: :watch EXPRESSION\n" };
        m_watches.push_back(rest);
        return { "watch #" + std::to_string(m_watches.size()) + ": " + rest + "\n" };
    }
    if (command(line, ":unwatch", rest)) {
        if (rest.empty()) {
            m_watches.clear();
            return { "all watches removed\n" };
        }
        const long n = std::strtol(rest.c_str(), nullptr, 10);
        if (n < 1 || n > (long)m_watches.size()) return { "no watch #" + rest + "\n" };
        m_watches.erase(m_watches.begin() + (n - 1));
        return { "watch #" + std::to_string(n) + " removed\n" };
    }
    const std::string echo = "> " + line + "\n";
    return { eval(line) + "\n", false, echo };
}

std::string EditorConsole::watches(const Eval& eval) const {
    if (m_watches.empty()) return "No watches: type :watch EXPRESSION in the console below.";
    std::string out;
    for (size_t i = 0; i < m_watches.size(); ++i)
        out += (i ? "\n" : "") + std::string("#") + std::to_string(i + 1) + "  " + m_watches[i] + " = " + one_line(eval(m_watches[i]));
    return out;
}

}
