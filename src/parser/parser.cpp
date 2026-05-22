#include "parser/parser.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "parser/lexer.h"
#include "parser/parse_error.h"

namespace {

std::string Upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

bool IsSingleCaseWord(const std::string& word) {
    bool has_lower = false;
    bool has_upper = false;
    for (char ch : word) {
        if (std::islower(static_cast<unsigned char>(ch))) {
            has_lower = true;
        }
        if (std::isupper(static_cast<unsigned char>(ch))) {
            has_upper = true;
        }
    }
    return !(has_lower && has_upper);
}

std::optional<std::int64_t> ParseRevertTimestampMs(const std::string& raw) {
    if (raw.size() < 23) {
        return std::nullopt;
    }

    const auto IsDigitAt = [&](std::size_t pos) {
        return pos < raw.size() && std::isdigit(static_cast<unsigned char>(raw[pos]));
    };
    const auto Require = [&](std::size_t pos, char expected) {
        return pos < raw.size() && raw[pos] == expected;
    };
    const auto Number = [&](std::size_t pos, std::size_t count) -> std::optional<int> {
        int value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            if (!IsDigitAt(pos + i)) {
                return std::nullopt;
            }
            value = value * 10 + (raw[pos + i] - '0');
        }
        return value;
    };

    if (!Require(4, '.') || !Require(7, '.') || !Require(10, '-') ||
        !Require(13, ':') || !Require(16, ':') || !Require(19, '.')) {
        return std::nullopt;
    }

    const std::optional<int> year = Number(0, 4);
    const std::optional<int> month = Number(5, 2);
    const std::optional<int> day = Number(8, 2);
    const std::optional<int> hour = Number(11, 2);
    const std::optional<int> minute = Number(14, 2);
    const std::optional<int> second = Number(17, 2);
    const std::optional<int> millis = Number(20, 3);
    if (!year || !month || !day || !hour || !minute || !second || !millis) {
        return std::nullopt;
    }

    if (*month < 1 || *month > 12 || *day < 1 || *day > 31 ||
        *hour < 0 || *hour > 23 || *minute < 0 || *minute > 59 ||
        *second < 0 || *second > 59) {
        return std::nullopt;
    }

    for (std::size_t i = 23; i < raw.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(raw[i]))) {
            return std::nullopt;
        }
    }

    std::tm tm{};
    tm.tm_year = *year - 1900;
    tm.tm_mon = *month - 1;
    tm.tm_mday = *day;
    tm.tm_hour = *hour;
    tm.tm_min = *minute;
    tm.tm_sec = *second;
    tm.tm_isdst = -1;

    const std::time_t seconds_since_epoch = std::mktime(&tm);
    if (seconds_since_epoch == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }

    return static_cast<std::int64_t>(seconds_since_epoch) * 1000 + *millis;
}

std::unique_ptr<db::ColumnRefExpr> Column(std::string name) {
    auto expr = std::make_unique<db::ColumnRefExpr>();
    expr->column_name = std::move(name);
    return expr;
}

std::unique_ptr<db::LiteralExpr> Literal(db::Value value) {
    auto expr = std::make_unique<db::LiteralExpr>();
    expr->value = std::move(value);
    return expr;
}

class ParserImpl {
public:
    explicit ParserImpl(std::vector<db::Token> tokens) : tokens_(std::move(tokens)) {}

    std::unique_ptr<db::SqlStatement> ParseStatement() {
        if (MatchKeyword("CREATE")) {
            if (MatchKeyword("DATABASE")) {
                auto stmt = std::make_unique<db::CreateDatabaseStatement>();
                stmt->db_name = ParseIdentifier();
                Finish();
                return stmt;
            }
            if (MatchKeyword("TABLE")) {
                return ParseCreateTable();
            }
            if (MatchKeyword("USER")) {
                return ParseCreateUser();
            }
            if (MatchKeyword("GROUP")) {
                return ParseCreateGroup();
            }
            throw db::ParseError("expected DATABASE, TABLE, USER or GROUP after CREATE");
        }
        if (MatchKeyword("DROP")) {
            if (MatchKeyword("DATABASE")) {
                auto stmt = std::make_unique<db::DropDatabaseStatement>();
                stmt->db_name = ParseIdentifier();
                Finish();
                return stmt;
            }
            if (MatchKeyword("TABLE")) {
                auto stmt = std::make_unique<db::DropTableStatement>();
                ParseQualifiedName(&stmt->database_name, &stmt->table_name);
                Finish();
                return stmt;
            }
            if (MatchKeyword("USER")) {
                auto stmt = std::make_unique<db::DropUserStatement>();
                stmt->user_id = ParseIdentifier();
                Finish();
                return stmt;
            }
            if (MatchKeyword("GROUP")) {
                auto stmt = std::make_unique<db::DropGroupStatement>();
                stmt->group_name = ParseIdentifier();
                Finish();
                return stmt;
            }
            throw db::ParseError("expected DATABASE, TABLE, USER or GROUP after DROP");
        }
        if (MatchKeyword("USE")) {
            auto stmt = std::make_unique<db::UseDatabaseStatement>();
            stmt->db_name = ParseIdentifier();
            Finish();
            return stmt;
        }
        if (MatchKeyword("INSERT")) {
            return ParseInsert();
        }
        if (MatchKeyword("SELECT")) {
            return ParseSelect();
        }
        if (MatchKeyword("UPDATE")) {
            return ParseUpdate();
        }
        if (MatchKeyword("DELETE")) {
            return ParseDelete();
        }
        if (MatchKeyword("REVERT")) {
            return ParseRevert();
        }
        if (MatchKeyword("LOGIN")) {
            return ParseLogin();
        }
        if (MatchKeyword("GRANT")) {
            return ParseGrant();
        }
        if (MatchKeyword("REVOKE")) {
            return ParseRevoke();
        }
        if (MatchKeyword("ADD")) {
            return ParseAddUserToGroup();
        }
        if (MatchKeyword("SUBMIT")) {
            return ParseSubmitQuery();
        }
        if (MatchKeyword("GET")) {
            return ParseGetTask();
        }
        if (MatchKeyword("CANCEL")) {
            return ParseCancelTask();
        }
        if (MatchKeyword("SHOW")) {
            if (MatchKeyword("METRICS")) {
                Finish();
                return std::make_unique<db::ShowMetricsStatement>();
            }
            throw db::ParseError("expected METRICS after SHOW");
        }
        throw db::ParseError("unknown SQL command");
    }

private:
    
    
    bool At(db::TokenType type) const { return Peek().type == type; }

    const db::Token& Peek() const { return tokens_[pos_]; }

    const db::Token& Consume() { return tokens_[pos_++]; }

    bool Match(db::TokenType type) {
        if (!At(type)) {
            return false;
        }
        ++pos_;
        return true;
    }

    bool MatchKeyword(const std::string& keyword) {
        if (Peek().type != db::TokenType::kIdentifier) {
            return false;
        }
        if (Upper(Peek().lexeme) != keyword) {
            return false;
        }
        if (!IsSingleCaseWord(Peek().lexeme)) {
            throw db::ParseError("mixed-case keyword is not allowed: " + Peek().lexeme);
        }
        ++pos_;
        return true;
    }

    void Expect(db::TokenType type, const std::string& what) {
        if (!Match(type)) {
            throw db::ParseError("expected " + what);
        }
    }

    void ExpectKeyword(const std::string& keyword) {
        if (!MatchKeyword(keyword)) {
            throw db::ParseError("expected " + keyword);
        }
    }

    std::string ParseIdentifier() {
        if (!At(db::TokenType::kIdentifier)) {
            throw db::ParseError("expected identifier");
        }
        return Consume().lexeme;
    }

    void ParseQualifiedName(std::string* db_name, std::string* table_name) {
        std::string first = ParseIdentifier();
        if (Match(db::TokenType::kDot)) {
            *db_name = std::move(first);
            *table_name = ParseIdentifier();
            return;
        }
        db_name->clear();
        *table_name = std::move(first);
    }

    db::Value ParseLiteralValue() {
        if (At(db::TokenType::kNumber)) {
            const std::string text = Consume().lexeme;
            return db::Value::Int(std::strtoll(text.c_str(), nullptr, 10));
        }
        if (At(db::TokenType::kString)) {
            return db::Value::String(Consume().lexeme);
        }
        if (MatchKeyword("NULL")) {
            return db::Value::Null();
        }
        throw db::ParseError("expected literal value");
    }

    std::unique_ptr<db::Expr> ParseOperand() {
        if (At(db::TokenType::kNumber) || At(db::TokenType::kString)) {
            return Literal(ParseLiteralValue());
        }
        if (MatchKeyword("NULL")) {
            return Literal(db::Value::Null());
        }
        if (At(db::TokenType::kIdentifier)) {
            return Column(ParseIdentifier());
        }
        throw db::ParseError("expected expression operand");
    }

    db::BinaryOp ParseBinaryOp() {
        if (Match(db::TokenType::kEq)) return db::BinaryOp::kEq;
        if (Match(db::TokenType::kNe)) return db::BinaryOp::kNe;
        if (Match(db::TokenType::kLt)) return db::BinaryOp::kLt;
        if (Match(db::TokenType::kGt)) return db::BinaryOp::kGt;
        if (Match(db::TokenType::kLe)) return db::BinaryOp::kLe;
        if (Match(db::TokenType::kGe)) return db::BinaryOp::kGe;
        throw db::ParseError("expected comparison operator");
    }

    std::unique_ptr<db::Expr> ParsePredicate() {
        if (Match(db::TokenType::kLParen)) {
            auto expr = ParseOr();
            Expect(db::TokenType::kRParen, ")");
            return expr;
        }

        auto left = ParseOperand();
        if (MatchKeyword("BETWEEN")) {
            auto expr = std::make_unique<db::BetweenExpr>();
            expr->value = std::move(left);
            expr->low = ParseOperand();
            ExpectKeyword("AND");
            expr->high = ParseOperand();
            return expr;
        }
        if (MatchKeyword("LIKE")) {
            auto expr = std::make_unique<db::LikeExpr>();
            expr->value = std::move(left);
            expr->pattern = ParseOperand();
            return expr;
        }

        auto expr = std::make_unique<db::BinaryExpr>();
        expr->op = ParseBinaryOp();
        expr->left = std::move(left);
        expr->right = ParseOperand();
        return expr;
    }

    std::unique_ptr<db::Expr> ParseAnd() {
        auto expr = ParsePredicate();
        while (MatchKeyword("AND")) {
            auto logical = std::make_unique<db::LogicalExpr>();
            logical->op = db::LogicalOp::kAnd;
            logical->left = std::move(expr);
            logical->right = ParsePredicate();
            expr = std::move(logical);
        }
        return expr;
    }

    std::unique_ptr<db::Expr> ParseOr() {
        auto expr = ParseAnd();
        while (MatchKeyword("OR")) {
            auto logical = std::make_unique<db::LogicalExpr>();
            logical->op = db::LogicalOp::kOr;
            logical->left = std::move(expr);
            logical->right = ParseAnd();
            expr = std::move(logical);
        }
        return expr;
    }

    std::unique_ptr<db::Expr> ParseWhereIfPresent() {
        if (!MatchKeyword("WHERE")) {
            return nullptr;
        }
        return ParseOr();
    }

    std::unique_ptr<db::SqlStatement> ParseCreateTable() {
        auto stmt = std::make_unique<db::CreateTableStatement>();
        ParseQualifiedName(&stmt->database_name, &stmt->table_name);
        Expect(db::TokenType::kLParen, "(");
        do {
            db::ColumnSchema column;
            column.name = ParseIdentifier();
            const std::string type = Upper(ParseIdentifier());
            if (type == "INT") {
                column.type = db::ColumnType::kInt;
            } else if (type == "STRING") {
                column.type = db::ColumnType::kString;
            } else {
                throw db::ParseError("expected INT or STRING column type");
            }
            while (At(db::TokenType::kIdentifier)) {
                if (MatchKeyword("NOT_NULL")) {
                    column.not_null = true;
                } else if (MatchKeyword("INDEXED")) {
                    column.indexed = true;
                    column.not_null = true;
                } else {
                    break;
                }
            }
            stmt->schema.columns.push_back(std::move(column));
        } while (Match(db::TokenType::kComma));
        Expect(db::TokenType::kRParen, ")");
        Finish();
        return stmt;
    }

    std::unique_ptr<db::SqlStatement> ParseInsert() {
        ExpectKeyword("INTO");
        auto stmt = std::make_unique<db::InsertStatement>();
        ParseQualifiedName(&stmt->database_name, &stmt->table_name);
        Expect(db::TokenType::kLParen, "(");
        do {
            stmt->columns.push_back(ParseIdentifier());
        } while (Match(db::TokenType::kComma));
        Expect(db::TokenType::kRParen, ")");
        ExpectKeyword("VALUE");
        do {
            Expect(db::TokenType::kLParen, "(");
            std::vector<db::Value> row;
            do {
                row.push_back(ParseLiteralValue());
            } while (Match(db::TokenType::kComma));
            Expect(db::TokenType::kRParen, ")");
            stmt->rows.push_back(std::move(row));
        } while (Match(db::TokenType::kComma));
        Finish();
        return stmt;
    }

    std::unique_ptr<db::SqlStatement> ParseSelect() {
        auto stmt = std::make_unique<db::SelectStatement>();
        if (Match(db::TokenType::kStar)) {
            stmt->select_all = true;
        } else {
            Expect(db::TokenType::kLParen, "(");
            do {
                db::SelectItem item;
                item.column = ParseIdentifier();
                if (MatchKeyword("AS")) {
                    item.alias = ParseIdentifier();
                }
                stmt->columns.push_back(std::move(item));
            } while (Match(db::TokenType::kComma));
            Expect(db::TokenType::kRParen, ")");
        }
        ExpectKeyword("FROM");
        ParseQualifiedName(&stmt->database_name, &stmt->table_name);
        stmt->where = ParseWhereIfPresent();
        Finish();
        return stmt;
    }

    std::unique_ptr<db::SqlStatement> ParseUpdate() {
        auto stmt = std::make_unique<db::UpdateStatement>();
        ParseQualifiedName(&stmt->database_name, &stmt->table_name);
        ExpectKeyword("SET");
        do {
            std::string column = ParseIdentifier();
            Expect(db::TokenType::kEq, "=");
            stmt->assignments.push_back({std::move(column), ParseLiteralValue()});
        } while (Match(db::TokenType::kComma));
        ExpectKeyword("WHERE");
        stmt->where = ParseOr();
        Finish();
        return stmt;
    }

    std::unique_ptr<db::SqlStatement> ParseDelete() {
        ExpectKeyword("FROM");
        auto stmt = std::make_unique<db::DeleteStatement>();
        ParseQualifiedName(&stmt->database_name, &stmt->table_name);
        ExpectKeyword("WHERE");
        stmt->where = ParseOr();
        Finish();
        return stmt;
    }

    std::unique_ptr<db::SqlStatement> ParseRevert() {
        auto stmt = std::make_unique<db::RevertStatement>();
        ParseQualifiedName(&stmt->database_name, &stmt->table_name);
        if (!At(db::TokenType::kIdentifier) && !At(db::TokenType::kNumber)) {
            throw db::ParseError("expected revert timestamp");
        }
        const std::string raw = Consume().lexeme;
        std::optional<std::int64_t> timestamp_ms = ParseRevertTimestampMs(raw);
        if (!timestamp_ms.has_value()) {
            throw db::ParseError("expected timestamp in yyyy.mm.dd-hh:mm:ss.mmm format");
        }
        stmt->timestamp_ms = *timestamp_ms;
        Finish();
        return stmt;
    }

    

    std::unique_ptr<db::SqlStatement> ParseCreateUser() {
        auto stmt = std::make_unique<db::CreateUserStatement>();
        stmt->user_id = ParseIdentifier();
        ExpectKeyword("PASSWORD");
        if (!At(db::TokenType::kString)) {
            throw db::ParseError("expected quoted password string");
        }
        stmt->password = Consume().lexeme;
        Finish();
        return stmt;
    }

    std::unique_ptr<db::SqlStatement> ParseCreateGroup() {
        auto stmt = std::make_unique<db::CreateGroupStatement>();
        stmt->group_name = ParseIdentifier();
        Finish();
        return stmt;
    }

    std::unique_ptr<db::SqlStatement> ParseLogin() {
        auto stmt = std::make_unique<db::LoginStatement>();
        stmt->user_id = ParseIdentifier();
        ExpectKeyword("PASSWORD");
        if (!At(db::TokenType::kString)) {
            throw db::ParseError("expected quoted password string");
        }
        stmt->password = Consume().lexeme;
        Finish();
        return stmt;
    }

    
    std::unique_ptr<db::SqlStatement> ParseGrant() {
        auto stmt = std::make_unique<db::GrantStatement>();
        stmt->permission = Upper(ParseIdentifier());
        
        while (At(db::TokenType::kIdentifier) &&
               Upper(Peek().lexeme) != "TO") {
            stmt->permission += "_" + Upper(Consume().lexeme);
        }
        ExpectKeyword("TO");
        if (MatchKeyword("USER")) {
            stmt->target_type = "USER";
        } else if (MatchKeyword("GROUP")) {
            stmt->target_type = "GROUP";
        } else {
            throw db::ParseError("expected USER or GROUP after TO in GRANT");
        }
        stmt->target_name = ParseIdentifier();
        Finish();
        return stmt;
    }

    std::unique_ptr<db::SqlStatement> ParseRevoke() {
        auto stmt = std::make_unique<db::RevokeStatement>();
        stmt->permission = Upper(ParseIdentifier());
        while (At(db::TokenType::kIdentifier) &&
               Upper(Peek().lexeme) != "FROM") {
            stmt->permission += "_" + Upper(Consume().lexeme);
        }
        ExpectKeyword("FROM");
        if (MatchKeyword("USER")) {
            stmt->target_type = "USER";
        } else if (MatchKeyword("GROUP")) {
            stmt->target_type = "GROUP";
        } else {
            throw db::ParseError("expected USER or GROUP after FROM in REVOKE");
        }
        stmt->target_name = ParseIdentifier();
        Finish();
        return stmt;
    }

    
    std::unique_ptr<db::SqlStatement> ParseAddUserToGroup() {
        ExpectKeyword("USER");
        auto stmt = std::make_unique<db::AddUserToGroupStatement>();
        stmt->user_id = ParseIdentifier();
        ExpectKeyword("TO");
        ExpectKeyword("GROUP");
        stmt->group_name = ParseIdentifier();
        Finish();
        return stmt;
    }

    

    
    std::unique_ptr<db::SqlStatement> ParseSubmitQuery() {
        ExpectKeyword("QUERY");
        auto stmt = std::make_unique<db::SubmitQueryStatement>();
        if (!At(db::TokenType::kString)) {
            throw db::ParseError("expected quoted SQL string after SUBMIT QUERY");
        }
        stmt->inner_sql = Consume().lexeme;
        Finish();
        return stmt;
    }

    
    std::unique_ptr<db::SqlStatement> ParseGetTask() {
        ExpectKeyword("TASK");
        auto stmt = std::make_unique<db::GetTaskStatement>();
        stmt->request_id = ParseIdentifier();
        Finish();
        return stmt;
    }

    
    std::unique_ptr<db::SqlStatement> ParseCancelTask() {
        ExpectKeyword("TASK");
        auto stmt = std::make_unique<db::CancelTaskStatement>();
        stmt->request_id = ParseIdentifier();
        Finish();
        return stmt;
    }

    void Finish() {
        Match(db::TokenType::kSemicolon);
        if (!At(db::TokenType::kEof)) {
            throw db::ParseError("unexpected tokens after statement");
        }
    }

    std::vector<db::Token> tokens_;
    std::size_t pos_ = 0;
};

}

std::unique_ptr<db::SqlStatement> db::Parser::Parse(const std::string& sql) {
    Lexer lexer(sql);
    ParserImpl impl(lexer.Tokenize());
    return impl.ParseStatement();
}
