#pragma once
#include <memory>
#include <string>
#include "sql_statement.h"

namespace db {

class Parser {
public:
    std::unique_ptr<SqlStatement> Parse(const std::string& sql);
};

}
