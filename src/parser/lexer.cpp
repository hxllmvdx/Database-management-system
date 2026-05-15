#include "parser/lexer.h"
#include "parser/parse_error.h"

#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

bool IsIdentifierStart(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
}

bool IsIdentifierPart(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

}

db::Lexer::Lexer(std::string input) : input_(std::move(input)) {}

std::vector<db::Token> db::Lexer::Tokenize() {
    std::vector<Token> tokens;
    std::size_t i = 0;

    while (i < input_.size()) {
        const char ch = input_[i];

        if (std::isspace(static_cast<unsigned char>(ch))) {
            ++i;
            continue;
        }

        if (IsIdentifierStart(ch)) {
            const std::size_t start = i++;
            while (i < input_.size() && IsIdentifierPart(input_[i])) {
                ++i;
            }
            tokens.push_back({TokenType::kIdentifier, input_.substr(start, i - start)});
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(ch))) {
            const std::size_t start = i++;
            bool timestamp_like = false;
            while (i < input_.size()) {
                const char c = input_[i];
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    ++i;
                    continue;
                }
                if (c == '.' || c == '-' || c == ':') {
                    timestamp_like = true;
                    ++i;
                    continue;
                }
                break;
            }
            if (timestamp_like) {
                tokens.push_back({TokenType::kIdentifier, input_.substr(start, i - start)});
                continue;
            }
            while (i < input_.size() && std::isdigit(static_cast<unsigned char>(input_[i]))) {
                ++i;
            }
            tokens.push_back({TokenType::kNumber, input_.substr(start, i - start)});
            continue;
        }

        if (ch == '"') {
            ++i;
            std::string literal;
            while (i < input_.size() && input_[i] != '"') {
                if (input_[i] == '\\' && i + 1 < input_.size()) {
                    ++i;
                }
                literal.push_back(input_[i++]);
            }
            if (i >= input_.size() || input_[i] != '"') {
                throw ParseError("unterminated string literal");
            }
            ++i;
            tokens.push_back({TokenType::kString, std::move(literal)});
            continue;
        }

        if (ch == '=' && i + 1 < input_.size() && input_[i + 1] == '=') {
            tokens.push_back({TokenType::kEq, "=="});
            i += 2;
            continue;
        }
        if (ch == '!' && i + 1 < input_.size() && input_[i + 1] == '=') {
            tokens.push_back({TokenType::kNe, "!="});
            i += 2;
            continue;
        }
        if (ch == '<' && i + 1 < input_.size() && input_[i + 1] == '=') {
            tokens.push_back({TokenType::kLe, "<="});
            i += 2;
            continue;
        }
        if (ch == '>' && i + 1 < input_.size() && input_[i + 1] == '=') {
            tokens.push_back({TokenType::kGe, ">="});
            i += 2;
            continue;
        }

        switch (ch) {
            case ',': tokens.push_back({TokenType::kComma, ","}); break;
            case ';': tokens.push_back({TokenType::kSemicolon, ";"}); break;
            case '(': tokens.push_back({TokenType::kLParen, "("}); break;
            case ')': tokens.push_back({TokenType::kRParen, ")"}); break;
            case '.': tokens.push_back({TokenType::kDot, "."}); break;
            case '*': tokens.push_back({TokenType::kStar, "*"}); break;
            case '=': tokens.push_back({TokenType::kEq, "="}); break;
            case '<': tokens.push_back({TokenType::kLt, "<"}); break;
            case '>': tokens.push_back({TokenType::kGt, ">"}); break;
            default:
                throw ParseError(std::string("unexpected character: ") + ch);
        }
        ++i;
    }

    tokens.push_back({TokenType::kEof, ""});
    return tokens;
}
