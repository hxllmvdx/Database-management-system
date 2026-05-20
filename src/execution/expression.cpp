#include "execution/expression.h"

#include <regex>
#include <stdexcept>

namespace {

db::Status Compare(const db::Value& left,
                   const db::Value& right,
                   db::BinaryOp op,
                   db::Value* out) {
    if (left.is_null() || right.is_null()) {
        *out = db::Value::Bool(false);
        return db::Status::Ok();
    }
    if (left.type() != right.type()) {
        return db::Status::Error(db::StatusCode::kExecutionError,
                                 "cannot compare values of different types");
    }
    switch (op) {
        case db::BinaryOp::kEq: *out = db::Value::Bool(left == right); break;
        case db::BinaryOp::kNe: *out = db::Value::Bool(left != right); break;
        case db::BinaryOp::kLt: *out = db::Value::Bool(left < right); break;
        case db::BinaryOp::kGt: *out = db::Value::Bool(left > right); break;
        case db::BinaryOp::kLe: *out = db::Value::Bool(left <= right); break;
        case db::BinaryOp::kGe: *out = db::Value::Bool(left >= right); break;
    }
    return db::Status::Ok();
}

bool Truthy(const db::Value& value) {
    return value.type() == db::ValueType::kBool && value.AsBool();
}

}

db::Status db::ExpressionEvaluator::Evaluate(const Expr& expr,
                                             const TableSchema& schema,
                                             const Tuple& tuple,
                                             Value* out) {
    if (out == nullptr) {
        return Status::Error(StatusCode::kInvalidArgument, "expression output is null");
    }

    switch (expr.kind()) {
        case ExprKind::kColumnRef: {
            const auto& column = static_cast<const ColumnRefExpr&>(expr);
            const int index = schema.FindColumn(column.column_name);
            if (index < 0 || static_cast<std::size_t>(index) >= tuple.values.size()) {
                return Status::Error(StatusCode::kExecutionError,
                                     "column '" + column.column_name + "' is not available");
            }
            *out = tuple.values[static_cast<std::size_t>(index)];
            return Status::Ok();
        }
        case ExprKind::kLiteral:
            *out = static_cast<const LiteralExpr&>(expr).value;
            return Status::Ok();
        case ExprKind::kBinary: {
            const auto& binary = static_cast<const BinaryExpr&>(expr);
            Value left;
            Value right;
            Status status = Evaluate(*binary.left, schema, tuple, &left);
            if (!status.ok()) return status;
            status = Evaluate(*binary.right, schema, tuple, &right);
            if (!status.ok()) return status;
            return Compare(left, right, binary.op, out);
        }
        case ExprKind::kLogical: {
            const auto& logical = static_cast<const LogicalExpr&>(expr);
            Value left;
            Status status = Evaluate(*logical.left, schema, tuple, &left);
            if (!status.ok()) return status;
            if (logical.op == LogicalOp::kAnd && !Truthy(left)) {
                *out = Value::Bool(false);
                return Status::Ok();
            }
            if (logical.op == LogicalOp::kOr && Truthy(left)) {
                *out = Value::Bool(true);
                return Status::Ok();
            }
            Value right;
            status = Evaluate(*logical.right, schema, tuple, &right);
            if (!status.ok()) return status;
            *out = Value::Bool(logical.op == LogicalOp::kAnd ? Truthy(left) && Truthy(right)
                                                             : Truthy(left) || Truthy(right));
            return Status::Ok();
        }
        case ExprKind::kBetween: {
            const auto& between = static_cast<const BetweenExpr&>(expr);
            Value value;
            Value low;
            Value high;
            Status status = Evaluate(*between.value, schema, tuple, &value);
            if (!status.ok()) return status;
            status = Evaluate(*between.low, schema, tuple, &low);
            if (!status.ok()) return status;
            status = Evaluate(*between.high, schema, tuple, &high);
            if (!status.ok()) return status;
            if (value.is_null() || low.is_null() || high.is_null()) {
                *out = Value::Bool(false);
                return Status::Ok();
            }
            if (value.type() != low.type() || value.type() != high.type()) {
                return Status::Error(StatusCode::kExecutionError,
                                     "BETWEEN operands have different types");
            }
            *out = Value::Bool(value >= low && value < high);
            return Status::Ok();
        }
        case ExprKind::kLike: {
            const auto& like = static_cast<const LikeExpr&>(expr);
            Value value;
            Value pattern;
            Status status = Evaluate(*like.value, schema, tuple, &value);
            if (!status.ok()) return status;
            status = Evaluate(*like.pattern, schema, tuple, &pattern);
            if (!status.ok()) return status;
            if (value.is_null() || pattern.is_null()) {
                *out = Value::Bool(false);
                return Status::Ok();
            }
            if (value.type() != ValueType::kString || pattern.type() != ValueType::kString) {
                return Status::Error(StatusCode::kExecutionError,
                                     "LIKE expects string operands");
            }
            try {
                *out = Value::Bool(std::regex_match(value.AsString(), std::regex(pattern.AsString())));
            } catch (const std::regex_error&) {
                return Status::Error(StatusCode::kExecutionError, "invalid LIKE regex");
            }
            return Status::Ok();
        }
    }
    return Status::Error(StatusCode::kExecutionError, "unsupported expression");
}
