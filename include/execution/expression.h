#pragma once
#include "../common/status.h"
#include "../parser/ast.h"
#include "../catalog/schema.h"
#include "tuple.h"

namespace db {

class ExpressionEvaluator {
public:
    Status Evaluate(const Expr& expr,
                    const TableSchema& schema,
                    const Tuple& tuple,
                    Value* out);
};

}
