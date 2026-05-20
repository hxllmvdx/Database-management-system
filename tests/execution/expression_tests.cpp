#include <gtest/gtest.h>

#include <optional>

#include "execution/expression.h"

TEST(ExpressionTests, EvaluatesLogicalBetweenAndLike) {
    db::TableSchema schema;
    schema.columns.push_back({"age", db::ColumnType::kInt, false, false, std::nullopt});
    schema.columns.push_back({"name", db::ColumnType::kString, false, false, std::nullopt});

    db::Tuple tuple;
    tuple.values.push_back(db::Value::Int(20));
    tuple.values.push_back(db::Value::String("Anna"));

    auto between = std::make_unique<db::BetweenExpr>();
    auto age = std::make_unique<db::ColumnRefExpr>();
    age->column_name = "age";
    between->value = std::move(age);
    auto low = std::make_unique<db::LiteralExpr>();
    low->value = db::Value::Int(18);
    between->low = std::move(low);
    auto high = std::make_unique<db::LiteralExpr>();
    high->value = db::Value::Int(30);
    between->high = std::move(high);

    auto like = std::make_unique<db::LikeExpr>();
    auto name = std::make_unique<db::ColumnRefExpr>();
    name->column_name = "name";
    like->value = std::move(name);
    auto pattern = std::make_unique<db::LiteralExpr>();
    pattern->value = db::Value::String("A.*");
    like->pattern = std::move(pattern);

    db::LogicalExpr root;
    root.op = db::LogicalOp::kAnd;
    root.left = std::move(between);
    root.right = std::move(like);

    db::Value out;
    db::ExpressionEvaluator evaluator;
    const db::Status status = evaluator.Evaluate(root, schema, tuple, &out);

    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_EQ(out.type(), db::ValueType::kBool);
    EXPECT_TRUE(out.AsBool());
}
