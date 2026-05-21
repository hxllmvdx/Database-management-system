#include "network/tcp_client.h"                         // интерфейс tcp-клиента
#include <winsock2.h>                                   // winsock api
#include <ws2tcpip.h>                                   // inet_pton
#include <cstdint>                                      // uintptr_t
#include "network/protocol.h"                           // сериализация
#include "common/status.h"                              // статусы

namespace db {

namespace {                                             // локальные хелперы

bool SendAll(SOCKET s, const char* data, int len) {     // полная отправка
    int sent = 0;                                       // счётчик
    while (sent < len) {                                // пока не всё ушло
        int r = send(s, data + sent, len - sent, 0);    // отправляем остаток
        if (r == SOCKET_ERROR) return false;            // ошибка сети
        sent += r;                                      // накапливаем
    }
    return true;                                        // всё отправлено
}

bool RecvAll(SOCKET s, char* data, int len) {           // полный приём
    int received = 0;                                   // счётчик
    while (received < len) {                            // пока не набрали
        int r = recv(s, data + received, len - received, 0); // читаем остаток
        if (r <= 0) return false;                       // обрыв
        received += r;                                  // накапливаем
    }
    return true;                                        // весь блок получен
}

} // anonymous namespace

TcpClient::TcpClient(std::string host, int port)        // конструктор
    : host_(std::move(host)),                           // адрес сервера
      port_(port) {}                                    // порт сервера

Status TcpClient::Send(const Request& req, Response* out) { // отправка запроса и получение ответа
    WSADATA wsaData;                                    // структура winsock
    int wsa_result = WSAStartup(MAKEWORD(2, 2), &wsaData); // инициализация
    if (wsa_result != 0) {                              // не вышло
        return Status::Error(StatusCode::kNetworkError, "WSAStartup failed");
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // создаём сокет
    if (sock == INVALID_SOCKET) {                       // ошибка
        WSACleanup();                                   // гасим winsock
        return Status::Error(StatusCode::kNetworkError, "socket creation failed");
    }

    sockaddr_in addr{};                                 // адрес сервера
    addr.sin_family = AF_INET;                          // ipv4
    addr.sin_port = htons(static_cast<u_short>(port_)); // порт
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);  // строка -> адрес

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { // соединяемся
        closesocket(sock);                              // закрываем сокет
        WSACleanup();                                   // гасим winsock
        return Status::Error(StatusCode::kNetworkError, "connect failed");
    }

    std::string req_data = Protocol::SerializeRequest(req); // упаковываем запрос
    std::uint32_t req_len = static_cast<std::uint32_t>(req_data.size()); // длина
    if (!SendAll(sock, reinterpret_cast<const char*>(&req_len), sizeof(req_len))) { // заголовок
        closesocket(sock);
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "failed to send request length");
    }
    if (!SendAll(sock, req_data.data(), static_cast<int>(req_data.size()))) { // тело
        closesocket(sock);
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "failed to send request body");
    }

    std::uint32_t resp_len = 0;                         // длина ответа
    if (!RecvAll(sock, reinterpret_cast<char*>(&resp_len), sizeof(resp_len))) { // читаем заголовок
        closesocket(sock);
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "failed to read response length");
    }

    std::string resp_data(resp_len, '\0');              // буфер под ответ
    if (!RecvAll(sock, resp_data.data(), static_cast<int>(resp_len))) { // читаем тело
        closesocket(sock);
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "failed to read response body");
    }

    closesocket(sock);                                  // закрываем соединение
    WSACleanup();                                       // освобождаем winsock

    if (!Protocol::DeserializeResponse(resp_data, out)) { // парсим ответ
        return Status::Error(StatusCode::kNetworkError, "failed to deserialize response");
    }
    return Status::Ok();                                // всё прошло успешно
}

} // namespace db
