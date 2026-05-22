#include <iostream>
#include <string>
#include "common/config.h"
#include "network/tcp_client.h"
#include "network/request.h"
#include "network/response.h"
#include "execution/value.h"

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 7000;
    if (argc > 1) host = argv[1];
    if (argc > 2) port = std::stoi(argv[2]);

    std::string sql;
    if (argc > 3) {
        sql = argv[3];
    } else {
        std::cout << "sql> ";
        std::getline(std::cin, sql);
    }

    db::TcpClient client(host, port);
    db::Request req;
    req.sql = sql;
    req.request_id = "cli";

    db::Response resp;
    db::Status status = client.Send(req, &resp);
    if (!status.ok()) {
        std::cerr << "network error: " << status.message() << "\n";
        return 1;
    }

    if (!resp.ok) {
        std::cerr << "error: " << resp.error << "\n";
        return 1;
    }

    for (const auto& col : resp.result.columns) {
        std::cout << col << "\t";
    }
    if (!resp.result.columns.empty()) std::cout << "\n";

    for (const auto& row : resp.result.rows) {
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
    std::cout << "affected rows: " << resp.result.affected_rows << "\n";
    return 0;
}
