#include <iostream>
#include <string>
#include "common/config.h"
#include "server/database.h"
#include "server/query_processor.h"
#include "server/session_context.h"
#include "execution/value.h"

int main(int argc, char* argv[]) {
    db::Config config;
    if (argc > 1) config.data_dir = argv[1];

    db::Database db(config);
    db::Status status = db.Start();
    if (!status.ok()) {
        std::cerr << "failed to start storage node: " << status.message() << "\n";
        return 1;
    }

    db::QueryProcessor qp(&db);

    std::cout << "storage node started. enter sql (empty line to exit):\n";
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) break;

        db::SessionContext ctx;
        ctx.client_id = "local";
        db::QueryResult result = qp.Execute(line, &ctx);
        if (!result.ok) {
            std::cerr << "error: " << result.error << "\n";
            continue;
        }

        for (const auto& col : result.columns) {
            std::cout << col << "\t";
        }
        if (!result.columns.empty()) std::cout << "\n";

        for (const auto& row : result.rows) {
            for (const auto& val : row.values) {
                switch (val.type()) {
                    case db::ValueType::kInt:    std::cout << val.AsInt();    break;
                    case db::ValueType::kString: std::cout << val.AsString(); break;
                    case db::ValueType::kBool:   std::cout << (val.AsBool() ? "true" : "false"); break;
                    case db::ValueType::kNull:   std::cout << "null";         break;
                }
                std::cout << "\t";
            }
            std::cout << "\n";
        }
        std::cout << "affected rows: " << result.affected_rows << "\n";
    }

    db.Stop();
    return 0;
}
