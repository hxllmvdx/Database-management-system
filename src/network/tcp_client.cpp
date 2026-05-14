#include "network/tcp_client.h"
#include "network/protocol.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstring>

namespace db {

namespace {

// читаем ровно n байт
bool recv_all(SOCKET sock, char* buf, int n) {
    int total = 0;
    while (total < n) {
        int r = recv(sock, buf + total, n - total, 0); // читаем порциями
        if (r <= 0) return false; // ошибка или disconnect
        total += r;
    }
    return true;
}

// отправляем ровно n байт
bool send_all(SOCKET sock, const char* buf, int n) {
    int total = 0;
    while (total < n) {
        int s = send(sock, buf + total, n - total, 0); // отправляем порциями
        if (s <= 0) return false; // ошибка
        total += s;
    }
    return true;
}

} // anonymous namespace

TcpClient::TcpClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

Status TcpClient::Send(const Request& req, Response* out) {
    WSADATA wsa_data;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa_data); // инициализация winsock
    if (err != 0) {
        return Status::Error(StatusCode::kNetworkError, "WSAStartup failed");
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // создаём tcp-сокет
    if (sock == INVALID_SOCKET) {
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "socket creation failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port_)); // порт в сетевом порядке
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr); // преобразуем ip

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sock);
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "connect failed");
    }

    // сериализуем и отправляем request
    std::string wire = Protocol::SerializeRequest(req);
    if (!send_all(sock, wire.data(), static_cast<int>(wire.size()))) {
        closesocket(sock);
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "send failed");
    }

    // читаем 4 байта длины ответа
    char len_buf[4];
    if (!recv_all(sock, len_buf, 4)) {
        closesocket(sock);
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "recv length failed");
    }
    uint32_t resp_len = 0;
    std::memcpy(&resp_len, len_buf, 4); // длина в little-endian

    // читаем тело ответа в полный resp_wire-буфер (с length-prefix)
    std::string resp_wire(4 + resp_len, '\0');
    std::memcpy(resp_wire.data(), len_buf, 4); // копируем длину
    if (!recv_all(sock, resp_wire.data() + 4, static_cast<int>(resp_len))) {
        closesocket(sock);
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "recv payload failed");
    }

    closesocket(sock); // закрываем сокет
    WSACleanup(); // освобождаем winsock

    if (!Protocol::DeserializeResponse(resp_wire, out)) {
        return Status::Error(StatusCode::kNetworkError, "deserialize response failed");
    }
    return Status::Ok();
}

} // namespace db
