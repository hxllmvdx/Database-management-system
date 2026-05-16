#include "network/tcp_server.h"                         // интерфейс tcp-сервера
#include <winsock2.h>                                   // winsock api
#include <ws2tcpip.h>                                   // расширения winsock (inet_pton)
#include <cstdint>                                      // uintptr_t
#include "network/protocol.h"                           // сериализация сообщений
#include "common/status.h"                              // статусы ошибок

namespace db {

namespace {                                             // анонимный namespace для хелперов

bool SendAll(void* socket, const char* data, int len) { // отправка всех байт
    SOCKET s = static_cast<SOCKET>(reinterpret_cast<uintptr_t>(socket)); // приводим void* к сокету
    int sent = 0;                                       // сколько уже отправили
    while (sent < len) {                                // пока не ушло всё
        int r = send(s, data + sent, len - sent, 0);    // пытаемся отправить остаток
        if (r == SOCKET_ERROR) return false;            // разрыв или ошибка
        sent += r;                                      // сдвигаемся
    }
    return true;                                        // успешно отправлено
}

bool RecvAll(void* socket, char* data, int len) {       // приём всех байт
    SOCKET s = static_cast<SOCKET>(reinterpret_cast<uintptr_t>(socket));
    int received = 0;                                   // сколько получили
    while (received < len) {                            // пока не набрали нужное количество
        int r = recv(s, data + received, len - received, 0); // читаем остаток
        if (r <= 0) return false;                       // соединение закрыто или ошибка
        received += r;                                  // накапливаем
    }
    return true;                                        // весь блок прочитан
}

} // anonymous namespace

TcpServer::TcpServer(std::string host, int port)        // конструктор
    : host_(std::move(host)),                           // адрес биндинга
      port_(port) {}                                    // порт прослушивания

Status TcpServer::Start(Handler handler) {              // запуск сервера
    WSADATA wsaData;                                    // структура инициализации
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData); // стартуем winsock
    if (wsa_result != 0) {                              // не удалось
        return Status::Error(StatusCode::kNetworkError, "WSAStartup failed"); // сообщаем
    }

    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // создаём сокет
    if (listen_socket == INVALID_SOCKET) {              // ошибка создания
        WSACleanup();                                   // гасим winsock
        return Status::Error(StatusCode::kNetworkError, "socket creation failed");
    }

    sockaddr_in addr{};                                 // структура адреса
    addr.sin_family = AF_INET;                          // ipv4
    addr.sin_port = htons(static_cast<u_short>(port_)); // порт в сетевом порядке
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);  // преобразуем строку в адрес

    if (bind(listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { // биндинг
        closesocket(listen_socket);                     // освобождаем сокет
        WSACleanup();                                   // и winsock
        return Status::Error(StatusCode::kNetworkError, "bind failed");
    }

    if (listen(listen_socket, SOMAXCONN) == SOCKET_ERROR) { // начинаем слушать
        closesocket(listen_socket);
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "listen failed");
    }

    listen_socket_ = reinterpret_cast<void*>(listen_socket); // сохраняем как void*
    running_ = true;                                    // флаг работы

    thread_ = std::thread([this, handler]() {           // поток обработки соединений
        while (running_.load()) {                       // пока не попросили остановиться
            SOCKET client_socket = accept(static_cast<SOCKET>(reinterpret_cast<uintptr_t>(listen_socket_)), nullptr, nullptr); // ждём клиента
            if (client_socket == INVALID_SOCKET) {      // accept прервали или ошибка
                continue;                               // идём на следующую итерацию
            }

            std::uint32_t req_len = 0;                  // длина входящего сообщения
            if (!RecvAll(reinterpret_cast<void*>(client_socket), reinterpret_cast<char*>(&req_len), sizeof(req_len))) { // читаем 4 байта
                closesocket(client_socket);             // не смогли — закрываем клиента
                continue;
            }

            std::string req_data(req_len, '\0');        // буфер под тело запроса
            if (!RecvAll(reinterpret_cast<void*>(client_socket), req_data.data(), static_cast<int>(req_len))) { // читаем тело
                closesocket(client_socket);
                continue;
            }

            Request req;                                // структура запроса
            Response resp;                              // структура ответа
            if (!Protocol::DeserializeRequest(req_data, &req)) { // не распарсилось
                resp.ok = false;                        // ошибка десериализации
                resp.error = "failed to deserialize request";
            } else {
                Session session;                        // транспортная сессия
                session.client_id = req.request_id;     // используем request_id как client_id
                session.remote_addr = host_;            // адрес сервера (упрощённо)
                resp = handler(session, req);           // вызываем бизнес-логику
            }

            std::string resp_data = Protocol::SerializeResponse(resp); // упаковываем ответ
            std::uint32_t resp_len = static_cast<std::uint32_t>(resp_data.size()); // длина ответа
            if (!SendAll(reinterpret_cast<void*>(client_socket), reinterpret_cast<const char*>(&resp_len), sizeof(resp_len))) { // заголовок
                closesocket(client_socket);
                continue;
            }
            SendAll(reinterpret_cast<void*>(client_socket), resp_data.data(), static_cast<int>(resp_data.size())); // тело
            closesocket(client_socket);                 // закрываем соединение с клиентом
        }
    });

    return Status::Ok();                                // сервер запущен
}

Status TcpServer::Stop() {                              // остановка сервера
    running_ = false;                                   // сигнал потоку
    if (listen_socket_ != nullptr) {                    // если сокет жив
        closesocket(static_cast<SOCKET>(reinterpret_cast<uintptr_t>(listen_socket_))); // закрываем — accept выйдет
        listen_socket_ = nullptr;                       // обнуляем
    }
    if (thread_.joinable()) {                           // если поток ещё бежит
        thread_.join();                                 // дожидаемся завершения
    }
    WSACleanup();                                       // освобождаем winsock
    return Status::Ok();                                // успешно остановлено
}

} // namespace db
