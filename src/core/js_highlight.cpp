#include "js_highlight.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace jsp {

namespace {

constexpr std::array<std::string_view, 40> kKeywords = {
    "async",  "await",  "break",   "case",   "catch",      "class",  "const", "continue", "debugger", "default",
    "delete", "do",     "else",    "export", "extends",    "finally", "for",  "from",     "function", "get",
    "if",     "import", "in",      "instanceof", "let",    "new",    "of",    "return",   "set",      "static",
    "super",  "switch", "throw",   "try",    "typeof",     "var",    "void",  "while",    "with",     "yield",
};
constexpr std::array<std::string_view, 7> kLiterals = { "true", "false", "null", "undefined", "this", "NaN", "Infinity" };
constexpr std::array<std::string_view, 9> kBeforeRegex = { "return", "typeof", "case", "do", "else", "in", "of", "delete", "void" };

bool ident_start(unsigned char c) { return std::isalpha(c) || c == '_' || c == '$' || c >= 0x80; }
bool ident_part(unsigned char c) { return ident_start(c) || std::isdigit(c); }

template <std::size_t N>
bool one_of(const std::array<std::string_view, N>& list, std::string_view w) {
    return std::find(list.begin(), list.end(), w) != list.end();
}

struct Scanner {
    const std::string& s;
    std::vector<std::size_t> u16;
    std::vector<Token> out;

    explicit Scanner(const std::string& text) : s(text), u16(text.size() + 1) {
        std::size_t units = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            u16[i] = units;
            const unsigned char c = (unsigned char)s[i];
            if ((c & 0xC0) != 0x80) units += c >= 0xF0 ? 2 : 1;
        }
        u16[s.size()] = units;
    }

    void emit(std::size_t from, std::size_t to, TokenKind kind) {
        if (to > from) out.push_back({ u16[from], u16[to] - u16[from], kind });
    }

    std::size_t quoted(std::size_t i, char q) {
        for (++i; i < s.size() && s[i] != q && s[i] != '\n'; ++i)
            if (s[i] == '\\') ++i;
        return std::min(s.size(), i + 1);
    }

    std::size_t regex(std::size_t i) {
        bool cls = false;
        for (++i; i < s.size() && s[i] != '\n'; ++i) {
            if (s[i] == '\\') ++i;
            else if (s[i] == '[') cls = true;
            else if (s[i] == ']') cls = false;
            else if (s[i] == '/' && !cls) break;
        }
        for (++i; i < s.size() && ident_part((unsigned char)s[i]); ++i) {
        }
        return std::min(s.size(), i);
    }

    std::size_t number(std::size_t i) {
        const bool hex = s[i] == '0' && i + 1 < s.size() && (s[i + 1] == 'x' || s[i + 1] == 'X');
        while (i < s.size()) {
            const char c = s[i];
            if (std::isalnum((unsigned char)c) || c == '.' || c == '_') ++i;
            else if ((c == '+' || c == '-') && !hex && (s[i - 1] == 'e' || s[i - 1] == 'E')) ++i;
            else break;
        }
        return i;
    }

    std::size_t template_string(std::size_t i, std::vector<int>& braces) {
        const std::size_t from = i;
        for (++i; i < s.size(); ++i) {
            if (s[i] == '\\') ++i;
            else if (s[i] == '`') {
                emit(from, i + 1, TokenKind::String);
                return i + 1;
            } else if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '{') {
                emit(from, i + 2, TokenKind::String);
                braces.push_back(0);
                return i + 2;
            }
        }
        emit(from, s.size(), TokenKind::String);
        return s.size();
    }

    void run() {
        std::vector<int> braces;
        bool regexOk = true;
        std::size_t i = 0;
        while (i < s.size()) {
            const char c = s[i];
            const char n = i + 1 < s.size() ? s[i + 1] : '\0';
            if (c == '/' && n == '/') {
                const std::size_t end = std::min(s.size(), s.find('\n', i));
                emit(i, end, TokenKind::Comment);
                i = end;
            } else if (c == '/' && n == '*') {
                const std::size_t close = s.find("*/", i + 2);
                const std::size_t end = close == std::string::npos ? s.size() : close + 2;
                emit(i, end, TokenKind::Comment);
                i = end;
            } else if (c == '"' || c == '\'') {
                const std::size_t end = quoted(i, c);
                emit(i, end, TokenKind::String);
                i = end;
                regexOk = false;
            } else if (c == '`') {
                i = template_string(i, braces);
                regexOk = false;
            } else if (c == '}' && !braces.empty() && braces.back() == 0) {
                braces.pop_back();
                i = template_string(i, braces);
                regexOk = false;
            } else if (c == '/' && regexOk) {
                const std::size_t end = regex(i);
                emit(i, end, TokenKind::Regex);
                i = end;
                regexOk = false;
            } else if (std::isdigit((unsigned char)c) || (c == '.' && std::isdigit((unsigned char)n))) {
                const std::size_t end = number(i);
                emit(i, end, TokenKind::Number);
                i = end;
                regexOk = false;
            } else if (ident_start((unsigned char)c)) {
                std::size_t end = i;
                while (end < s.size() && ident_part((unsigned char)s[end])) ++end;
                const std::string_view w(s.data() + i, end - i);
                const bool member = i > 0 && s[i - 1] == '.';
                if (!member && one_of(kKeywords, w)) emit(i, end, TokenKind::Keyword);
                else if (!member && one_of(kLiterals, w)) emit(i, end, TokenKind::Literal);
                regexOk = !member && one_of(kBeforeRegex, w);
                i = end;
            } else {
                if (c == '{' && !braces.empty()) ++braces.back();
                else if (c == '}' && !braces.empty()) --braces.back();
                if (!std::isspace((unsigned char)c)) regexOk = c != ')' && c != ']' && c != '}';
                ++i;
            }
        }
    }
};

}

std::vector<Token> highlight_js(const std::string& utf8) {
    Scanner sc(utf8);
    sc.run();
    return std::move(sc.out);
}

}
