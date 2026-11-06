#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace jsp {

enum class TokenKind { Keyword, Literal, String, Number, Comment, Regex };

struct Token {
    std::size_t start = 0, length = 0;
    TokenKind kind = TokenKind::Keyword;
};

std::vector<Token> highlight_js(const std::string& utf8);

}
