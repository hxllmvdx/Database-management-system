#pragma once
#include <string>

namespace db {

enum class TokenType {
    kEof,
    kIdentifier,
    kNumber,
    kString,
    kComma,
    kSemicolon,
    kLParen,
    kRParen,
    kEq,
    kNe,
    kLt,
    kGt,
    kLe,
    kGe,
    kKeyword,
};

struct Token {
    TokenType type = TokenType::kEof;
    std::string lexeme;
};

}
