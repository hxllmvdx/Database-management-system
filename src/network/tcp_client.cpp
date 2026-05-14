#include "network/tcp_client.h" // публичный интерфейс клиента
#include "network/protocol.h"   // сериализация запросов/ответов
#include <winsock2.h> // windows sockets api 2
#include <ws2tcpip.h> // расширенные функции winsock (inet_pton и др.)
#include <cstring>    // std::memcpy для копирования байтов

namespace db { // пространство имён базы данных

namespace { // внутренние хелперы клиента

// читает ровно n байт из сокета, false при ошибке или disconnect
bool recv_all(SOCKET sock, char* buf, int n) {
    int total = 0; // сколько уже прочитано
    while (total < n) { // пока не набрали нужное количество
        int r = recv(sock, buf + total, n - total, 0); // читаем остаток
        if (r <= 0) return false; // ошибка сети или сервер закрыл соединение
        total += r; // накапливаем прочитанное
    } // while
    return true; // успешно прочитали все n байт
} // recv_all

// отправляет ровно n байт в сокет, false при ошибке
bool send_all(SOCKET sock, const char* buf, int n) {
    int total = 0; // сколько уже отправлено
    while (total < n) { // пока не отправили всё
        int s = send(sock, buf + total, n - total, 0); // отправляем остаток
        if (s <= 0) return false; // ошибка отправки
        total += s; // накапливаем отправленное
    } // while
    return true; // успешно отправили все n байт
} // send_all

} // anonymous namespace

TcpClient::TcpClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {} // сохраняем параметры подключения

Status TcpClient::Send(const Request& req, Response* out) {
    WSADATA wsa_data; // структура для инициализации winsock
    int err = WSAStartup(MAKEWORD(2, 2), &wsa_data); // запускаем winsock 2.2
    if (err != 0) { // инициализация не удалась
        return Status::Error(StatusCode::kNetworkError, "WSAStartup failed"); // ошибка сети
    } // if

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // создаём tcp-сокет
    if (sock == INVALID_SOCKET) { // сокет не создался
        WSACleanup(); // освобождаем winsock
        return Status::Error(StatusCode::kNetworkError, "socket creation failed"); // ошибка
    } // if

    sockaddr_in addr{}; // структура адреса сервера
    addr.sin_family = AF_INET; // ipv4
    addr.sin_port = htons(static_cast<u_short>(port_)); // порт в сетевом порядке байт
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr); // преобразуем строку ip в бинарный вид

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { // подключаемся
        closesocket(sock); // закрываем сокет
        WSACleanup(); // освобождаем winsock
        return Status::Error(StatusCode::kNetworkError, "connect failed"); // не удалось подключиться
    } // if

    std::string wire = Protocol::SerializeRequest(req); // сериализуем запрос в wire-формат
    if (!send_all(sock, wire.data(), static_cast<int>(wire.size()))) { // отправляем на сервер
        closesocket(sock); // закрываем сокет
        WSACleanup(); // освобождаем winsock
        return Status::Error(StatusCode::kNetworkError, "send failed"); // ошибка отправки
    } // if

    char len_buf[4]; // буфер для 4 байт длины ответа
    if (!recv_all(sock, len_buf, 4)) { // читаем длину ответа
        closesocket(sock); // закрываем сокет
        WSACleanup(); // освобождаем winsock
        return Status::Error(StatusCode::kNetworkError, "recv length failed"); // ошибка чтения
    } // if
    uint32_t resp_len = 0; // длина payload ответа
    std::memcpy(&resp_len, len_buf, 4); // копируем 4 байта в uint32 (little-endian)

    std::string resp_wire(4 + resp_len, '\0'); // буфер для полного ответа с length-prefix
    std::memcpy(resp_wire.data(), len_buf, 4); // копируем прочитанную длину
    if (!recv_all(sock, resp_wire.data() + 4, static_cast<int>(resp_len))) { // читаем тело ответа
        closesocket(sock); // закрываем сокет
        WSACleanup(); // освобождаем winsock
        return Status::Error(StatusCode::kNetworkError, "recv payload failed"); // ошибка чтения
    } // if

    closesocket(sock); // закрываем соединение
    WSACleanup(); // освобождаем winsock

    if (!Protocol::DeserializeResponse(resp_wire, out)) { // парсим ответ
        return Status::Error(StatusCode::kNetworkError, "deserialize response failed"); // битый ответ
    } // if
    return Status::Ok(); // успешно отправили и получили ответ
} // Send

} // namespace db
