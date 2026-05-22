#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../execution/value.h"
#include "../catalog/schema.h"

namespace db {

enum class ExprKind {
    kColumnRef,
    kLiteral,
    kBinary,
    kLogical,
    kBetween,
    kLike,
};

enum class BinaryOp {
    kEq, kNe, kLt, kGt, kLe, kGe
};

enum class LogicalOp {
    kAnd, kOr
};

struct Expr {
    virtual ~Expr() = default;
    virtual ExprKind kind() const = 0;
};

struct ColumnRefExpr : Expr {
    std::string column_name;
    ExprKind kind() const override { return ExprKind::kColumnRef; }
};

struct LiteralExpr : Expr {
    Value value;
    ExprKind kind() const override { return ExprKind::kLiteral; }
};

struct BinaryExpr : Expr {
    BinaryOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    ExprKind kind() const override { return ExprKind::kBinary; }
};

struct LogicalExpr : Expr {
    LogicalOp op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    ExprKind kind() const override { return ExprKind::kLogical; }
};

struct BetweenExpr : Expr {
    std::unique_ptr<Expr> value;
    std::unique_ptr<Expr> low;
    std::unique_ptr<Expr> high;
    ExprKind kind() const override { return ExprKind::kBetween; }
};

struct LikeExpr : Expr {
    std::unique_ptr<Expr> value;
    std::unique_ptr<Expr> pattern;
    ExprKind kind() const override { return ExprKind::kLike; }
};

}
