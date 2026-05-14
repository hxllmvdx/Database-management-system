// исходный код написан человеком 1, комментарии добавлены человеком 3
#include <iostream> // стандартный ввод-вывод (cout, cerr)
#include <fstream> // файловые потоки для пакетного режима
#include <sstream> // строковые потоки
#include <string> // стандартный строковый тип
#include <algorithm> // std::transform для ToLower
#include <cctype> // std::tolower для обработки регистра
#include "network/tcp_client.h" // клиент для отправки запросов на сервер (код человека 1)
#include "network/request.h" // формат запроса (код человека 1)
#include "network/response.h" // формат ответа (код человека 1)
#include "server/query_result.h" // преобразование результата в json (код человека 1)
#include "common/config.h" // конфигурация подключения (код человека 1)

int main(int argc, char* argv[]) { // точка входа: аргументы командной строки
    db::Config config; // конфиг по умолчанию для подключения (host=127.0.0.1, port=7000)
    std::string current_db; // текущая база данных, выбранная через USE (код человека 1)
    db::TcpClient client(config.host, config.port); // создаём tcp-клиент, подключаемся к серверу (код человека 1)

    // лямбда: отправка одного sql-запроса на сервер и вывод результата (код человека 1)
    auto send_sql = [&](const std::string& sql) { // захват по ссылке: client, current_db
        db::Request req{}; // формируем запрос (код человека 1)
        req.sql = sql; // текст sql-запроса (код человека 1)
        req.database = current_db; // передаём текущую бд из контекста клиента (код человека 1)
        db::Response resp{}; // буфер для ответа сервера (код человека 1)
        auto st = client.Send(req, &resp); // отправляем по tcp и ждём ответ (код человека 1)
        if (!st.ok()) { // сетевая ошибка (код человека 1)
            std::cerr << "network error: " << st.message() << std::endl; // выводим ошибку в stderr (код человека 1)
            return; // прерываем обработку этого запроса (код человека 1)
        } // if network error
        if (!resp.ok) { // сервер вернул логическую ошибку (код человека 1)
            std::cout << "{\"error\":\"" << resp.error << "\"}" << std::endl; // выводим ошибку как json (код человека 1)
            return; // прерываем обработку (код человека 1)
        } // if server error
        std::cout << db::QueryResultToJson(resp.result) << std::endl; // выводим результат запроса как json (код человека 1)
    }; // send_sql

    // лямбда: выполнение накопленного буфера sql-команд (код человека 1)
    auto execute_buffer = [&](std::string& buf) { // захват по ссылке: send_sql, current_db
        // убираем пробелы с начала строки (код человека 1)
        size_t a = 0; // левая граница (код человека 1)
        while (a < buf.size() && std::isspace(static_cast<unsigned char>(buf[a]))) ++a; // пропускаем пробелы слева (код человека 1)
        // убираем пробелы с конца строки (код человека 1)
        size_t b = buf.size(); // правая граница (код человека 1)
        while (b > a && std::isspace(static_cast<unsigned char>(buf[b-1]))) --b; // пропускаем пробелы справа (код человека 1)
        buf = buf.substr(a, b - a); // оставляем только значимую часть (код человека 1)
        if (!buf.empty()) { // если после trim осталось что-то (код человека 1)
            // обрабатываем USE локально в cli, не отправляя на сервер (код человека 1)
            auto low = buf; // копия для проверки в нижнем регистре (код человека 1)
            std::transform(low.begin(), low.end(), low.begin(), ::tolower); // приводим к нижнему регистру (код человека 1)
            if (low.rfind("use ", 0) == 0) { // запрос начинается с "use " (код человека 1)
                current_db = buf.substr(4); // запоминаем имя базы после префикса (код человека 1)
                current_db.erase(0, current_db.find_first_not_of(" \t")); // trim слева (код человека 1)
                std::cout << "{\"ok\":true}" << std::endl; // подтверждаем смену бд локально (код человека 1)
            } else { // не USE — отправляем на сервер (код человека 1)
                send_sql(buf); // вызываем сетевую отправку (код человека 1)
            } // if USE
        } // if not empty
        buf.clear(); // очищаем буфер для следующей команды (код человека 1)
    }; // execute_buffer

    if (argc >= 2) { // пакетный режим: передан файл со скриптом (код человека 1)
        std::ifstream file(argv[1]); // открываем файл для чтения (код человека 1)
        if (!file.is_open()) { // не удалось открыть файл (код человека 1)
            std::cerr << "cannot open file: " << argv[1] << std::endl; // сообщаем об ошибке (код человека 1)
            return 1; // код ошибки (код человека 1)
        } // if open failed
        std::string line, buf; // line — текущая строка файла, buf — накопитель команд (код человека 1)
        while (std::getline(file, line)) { // читаем файл построчно (код человека 1)
            for (char c : line) { // идём по символам строки (код человека 1)
                if (c == ';') { // найден разделитель команд (код человека 1)
                    execute_buffer(buf); // выполняем накопленное до ; (код человека 1)
                } else { // обычный символ (код человека 1)
                    buf += c; // добавляем в буфер (код человека 1)
                } // if separator
            } // for char
            buf += '\n'; // сохраняем перенос строки в буфере (код человека 1)
        } // while getline
        execute_buffer(buf); // выполняем остаток после конца файла (код человека 1)
    } else { // интерактивный режим: ввод с клавиатуры (код человека 1)
        std::cout << "coursedb cli. enter sql commands (end with ;)." << std::endl; // приглашение (код человека 1)
        std::string buf; // буфер накопления команды (код человека 1)
        while (true) { // бесконечный цикл чтения (код человека 1)
            std::cout << "> "; // приглашение ввода (код человека 1)
            std::string line; // текущая строка ввода (код человека 1)
            if (!std::getline(std::cin, line)) break; // eof (ctrl+d/ctrl+z) — выходим (код человека 1)
            for (char c : line) { // идём по символам введённой строки (код человека 1)
                if (c == ';') { // найден разделитель (код человека 1)
                    execute_buffer(buf); // выполняем накопленное (код человека 1)
                } else { // обычный символ (код человека 1)
                    buf += c; // добавляем в буфер (код человека 1)
                } // if separator
            } // for char
            buf += '\n'; // сохраняем перенос строки (код человека 1)
        } // while true
    } // if batch else interactive
    return 0; // успешное завершение (код человека 1)
} // main
