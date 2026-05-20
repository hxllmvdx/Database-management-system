#pragma once
#include <string>
#include <vector>
#include "token.h"

namespace db {

class Lexer {
public:
    explicit Lexer(std::string input);
    std::vector<Token> Tokenize();

private:
    std::string input_;
};

}
