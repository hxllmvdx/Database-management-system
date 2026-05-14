#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <algorithm>
#include <cctype>
#include "network/tcp_client.h"
#include "network/request.h"
#include "network/response.h"
#include "server/query_result.h"
#include "common/config.h"

int main(int argc, char* argv[]) {
    db::Config config; // конфиг по умолчанию для подключения
    std::string current_db; // текущая база данных (для USE)
    db::TcpClient client(config.host, config.port); // подключаемся к серверу

    // функция отправки одного sql-запроса
    auto send_sql = [&](const std::string& sql) {
        db::Request req{};
        req.sql = sql;
        req.database = current_db; // передаём текущую бд
        db::Response resp{};
        auto st = client.Send(req, &resp); // отправляем и ждём ответ
        if (!st.ok()) {
            std::cerr << "network error: " << st.message() << std::endl;
            return;
        }
        if (!resp.ok) {
            std::cout << "{\"error\":\"" << resp.error << "\"}" << std::endl;
            return;
        }
        std::cout << db::QueryResultToJson(resp.result) << std::endl; // выводим результат как json
    };

    // функция выполнения накопленных команд
    auto execute_buffer = [&](std::string& buf) {
        // убираем пробелы с начала и конца
        size_t a = 0;
        while (a < buf.size() && std::isspace(static_cast<unsigned char>(buf[a]))) ++a;
        size_t b = buf.size();
        while (b > a && std::isspace(static_cast<unsigned char>(buf[b-1]))) --b;
        buf = buf.substr(a, b - a);
        if (!buf.empty()) {
            // обрабатываем USE локально
            auto low = buf;
            std::transform(low.begin(), low.end(), low.begin(), ::tolower);
            if (low.rfind("use ", 0) == 0) {
                current_db = buf.substr(4); // запоминаем базу
                current_db.erase(0, current_db.find_first_not_of(" \t")); // trim
                std::cout << "{\"ok\":true}" << std::endl;
            } else {
                send_sql(buf);
            }
        }
        buf.clear();
    };

    if (argc >= 2) {
        // пакетный режим: читаем файл
        std::ifstream file(argv[1]);
        if (!file.is_open()) {
            std::cerr << "cannot open file: " << argv[1] << std::endl;
            return 1;
        }
        std::string line, buf;
        while (std::getline(file, line)) {
            for (char c : line) {
                if (c == ';') {
                    execute_buffer(buf); // выполняем накопленное до ;
                } else {
                    buf += c;
                }
            }
            buf += '\n'; // сохраняем перенос строки
        }
        execute_buffer(buf); // выполняем остаток
    } else {
        // интерактивный режим
        std::cout << "coursedb cli. enter sql commands (end with ;)." << std::endl;
        std::string buf;
        while (true) {
            std::cout << "> ";
            std::string line;
            if (!std::getline(std::cin, line)) break; // eof
            for (char c : line) {
                if (c == ';') {
                    execute_buffer(buf);
                } else {
                    buf += c;
                }
            }
            buf += '\n';
        }
    }
    return 0;
}
