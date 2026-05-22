#include <gtest/gtest.h>

#include "parser/lexer.h"

TEST(LexerTests, TokenizesSqlSubset) {
    db::Lexer lexer("SELECT * FROM users WHERE age >= 18 AND name LIKE \"A.*\";");
    const std::vector<db::Token> tokens = lexer.Tokenize();

    ASSERT_GE(tokens.size(), 13U);
    EXPECT_EQ(tokens[0].lexeme, "SELECT");
    EXPECT_EQ(tokens[1].type, db::TokenType::kStar);
    EXPECT_EQ(tokens[6].type, db::TokenType::kGe);
    EXPECT_EQ(tokens[8].lexeme, "AND");
    EXPECT_EQ(tokens[11].type, db::TokenType::kString);
    EXPECT_EQ(tokens.back().type, db::TokenType::kEof);
}

