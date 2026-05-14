#include "network/tcp_server.h"
#include "network/protocol.h"
#include <winsock2.h> // windows sockets api
#include <ws2tcpip.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <cstring>

namespace db {

namespace {

// глобальный счётчик для id клиентов
std::atomic<int> g_client_counter{0};

// читаем ровно n байт из сокета
bool recv_all(SOCKET sock, char* buf, int n) {
    int total = 0;
    while (total < n) {
        int r = recv(sock, buf + total, n - total, 0); // читаем порциями
        if (r <= 0) return false; // ошибка или disconnect
        total += r;
    }
    return true;
}

// отправляем ровно n байт в сокет
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

struct TcpServer::Impl {
    std::string host; // адрес для прослушивания
    int port = 0; // порт
    SOCKET listen_sock = INVALID_SOCKET; // слушающий сокет
    std::atomic<bool> running{false}; // флаг работы сервера
    std::thread accept_thread; // поток приёма соединений
    std::vector<std::thread> client_threads; // потоки клиентов
    std::mutex clients_mtx; // защита списка потоков
    Handler handler; // обработчик запросов
};

TcpServer::TcpServer(std::string host, int port)
    : impl_(std::make_unique<Impl>()) {
    impl_->host = std::move(host);
    impl_->port = port;
}

TcpServer::~TcpServer() = default; // определение деструктора после impl_

Status TcpServer::Start(Handler handler) {
    impl_->handler = std::move(handler);

    WSADATA wsa_data;
    int err = WSAStartup(MAKEWORD(2, 2), &wsa_data); // инициализация winsock
    if (err != 0) {
        return Status::Error(StatusCode::kNetworkError, "WSAStartup failed");
    }

    impl_->listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // создаём tcp-сокет
    if (impl_->listen_sock == INVALID_SOCKET) {
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "socket creation failed");
    }

    // разрешаем повторное использование адреса
    int opt = 1;
    setsockopt(impl_->listen_sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(impl_->port)); // порт в сетевом порядке
    inet_pton(AF_INET, impl_->host.c_str(), &addr.sin_addr); // преобразуем ip-адрес

    if (bind(impl_->listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(impl_->listen_sock);
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "bind failed");
    }

    if (listen(impl_->listen_sock, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(impl_->listen_sock);
        WSACleanup();
        return Status::Error(StatusCode::kNetworkError, "listen failed");
    }

    // устанавливаем таймаут на accept, чтобы можно было прервать Stop()
    DWORD timeout = 100; // 100 мс
    setsockopt(impl_->listen_sock, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    impl_->running = true;
    // запускаем поток accept
    impl_->accept_thread = std::thread([this]() {
        while (impl_->running.load()) {
            sockaddr_in client_addr{};
            int addr_len = sizeof(client_addr);
            SOCKET client = accept(impl_->listen_sock,
                                   reinterpret_cast<sockaddr*>(&client_addr),
                                   &addr_len); // принимаем соединение
            if (client == INVALID_SOCKET) {
                // если сокет закрыт — выходим
                if (!impl_->running.load()) break;
                continue;
            }

            // таймаут на recv, чтобы потоки не висели вечно
            DWORD client_timeout = 500; // 500 мс
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&client_timeout), sizeof(client_timeout));

            // формируем session для нового клиента
            Session session;
            session.client_id = "client_" + std::to_string(g_client_counter++); // генерируем id
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
            session.remote_addr = ip_str; // запоминаем ip

            // запускаем поток обработки клиента
            std::thread t([this, client, session]() mutable {
                while (impl_->running.load()) {
                    // читаем 4 байта длины сообщения
                    char len_buf[4];
                    if (!recv_all(client, len_buf, 4)) break; // disconnect или ошибка
                    uint32_t msg_len = 0;
                    std::memcpy(&msg_len, len_buf, 4); // длина в little-endian

                    // читаем тело сообщения в полный wire-буфер (с length-prefix)
                    std::string wire(4 + msg_len, '\0');
                    std::memcpy(wire.data(), len_buf, 4); // копируем длину
                    if (!recv_all(client, wire.data() + 4, static_cast<int>(msg_len))) break;

                    // десериализуем request
                    Request req{};
                    if (!Protocol::DeserializeRequest(wire, &req)) {
                        Response resp{};
                        resp.ok = false;
                        resp.error = "protocol deserialize error";
                        std::string out = Protocol::SerializeResponse(resp);
                        send_all(client, out.data(), static_cast<int>(out.size()));
                        continue;
                    }

                    // вызываем обработчик
                    Response resp = impl_->handler(session, req);

                    // сериализуем и отправляем ответ
                    std::string out = Protocol::SerializeResponse(resp);
                    if (!send_all(client, out.data(), static_cast<int>(out.size()))) break;
                }
                closesocket(client); // закрываем сокет клиента
            });

            {
                std::lock_guard<std::mutex> lock(impl_->clients_mtx);
                impl_->client_threads.push_back(std::move(t)); // сохраняем поток
            }
        }
    });

    return Status::Ok();
}

Status TcpServer::Stop() {
    impl_->running = false; // останавливаем цикл
    if (impl_->listen_sock != INVALID_SOCKET) {
        closesocket(impl_->listen_sock); // закрываем слушающий сокет
        impl_->listen_sock = INVALID_SOCKET;
    }
    if (impl_->accept_thread.joinable()) {
        impl_->accept_thread.join(); // ждём поток accept
    }
    // ждём все клиентские потоки
    std::lock_guard<std::mutex> lock(impl_->clients_mtx);
    for (auto& t : impl_->client_threads) {
        if (t.joinable()) t.join();
    }
    impl_->client_threads.clear();
    WSACleanup(); // освобождаем winsock
    return Status::Ok();
}

} // namespace db
