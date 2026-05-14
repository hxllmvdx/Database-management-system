#include "network/tcp_server.h" // публичный интерфейс сервера
#include "network/protocol.h"   // сериализация запросов/ответов
#include <winsock2.h> // windows sockets api
#include <ws2tcpip.h> // расширенные функции winsock
#include <thread>     // потоки для accept и клиентов
#include <atomic>     // атомарный флаг running
#include <mutex>      // защита списка клиентских потоков
#include <vector>     // динамический массив потоков
#include <cstring>    // std::memcpy

namespace db { // пространство имён базы данных

namespace { // внутренние хелперы сервера

std::atomic<int> g_client_counter{0}; // глобальный счётчик для генерации client_id

// читает ровно n байт из сокета, false при ошибке или disconnect
bool recv_all(SOCKET sock, char* buf, int n) {
    int total = 0; // сколько уже прочитано
    while (total < n) { // пока не набрали нужное количество
        int r = recv(sock, buf + total, n - total, 0); // читаем остаток
        if (r <= 0) return false; // ошибка или клиент закрыл соединение
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

struct TcpServer::Impl { // скрытая реализация (pimpl)
    std::string host; // адрес для прослушивания (например "127.0.0.1")
    int port = 0; // порт для прослушивания (например 7000)
    SOCKET listen_sock = INVALID_SOCKET; // слушающий сокет
    std::atomic<bool> running{false}; // флаг работы сервера
    std::thread accept_thread; // поток, который принимает новые соединения
    std::vector<std::thread> client_threads; // потоки обработки клиентов
    std::mutex clients_mtx; // мьютекс для защиты client_threads
    Handler handler; // обработчик запросов (выставляется из Start)
}; // Impl

TcpServer::TcpServer(std::string host, int port)
    : impl_(std::make_unique<Impl>()) { // создаём скрытую реализацию
    impl_->host = std::move(host); // сохраняем адрес
    impl_->port = port; // сохраняем порт
} // конструктор

TcpServer::~TcpServer() = default; // деструктор после определения Impl (нужен для unique_ptr)

Status TcpServer::Start(Handler handler) {
    impl_->handler = std::move(handler); // запоминаем обработчик запросов

    WSADATA wsa_data; // структура инициализации winsock
    int err = WSAStartup(MAKEWORD(2, 2), &wsa_data); // стартуем winsock 2.2
    if (err != 0) { // инициализация не удалась
        return Status::Error(StatusCode::kNetworkError, "WSAStartup failed"); // ошибка сети
    } // if

    impl_->listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // создаём tcp-сокет
    if (impl_->listen_sock == INVALID_SOCKET) { // сокет не создался
        WSACleanup(); // освобождаем winsock
        return Status::Error(StatusCode::kNetworkError, "socket creation failed"); // ошибка
    } // if

    int opt = 1; // флаг для setsockopt
    setsockopt(impl_->listen_sock, SOL_SOCKET, SO_REUSEADDR, // разрешаем повторное использование адреса
               reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{}; // структура адреса привязки
    addr.sin_family = AF_INET; // ipv4
    addr.sin_port = htons(static_cast<u_short>(impl_->port)); // порт в сетевом порядке
    inet_pton(AF_INET, impl_->host.c_str(), &addr.sin_addr); // строка ip → бинарный вид

    if (bind(impl_->listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) { // привязываемся к адресу
        closesocket(impl_->listen_sock); // закрываем сокет
        WSACleanup(); // освобождаем winsock
        return Status::Error(StatusCode::kNetworkError, "bind failed"); // ошибка привязки
    } // if

    if (listen(impl_->listen_sock, SOMAXCONN) == SOCKET_ERROR) { // начинаем слушать
        closesocket(impl_->listen_sock); // закрываем сокет
        WSACleanup(); // освобождаем winsock
        return Status::Error(StatusCode::kNetworkError, "listen failed"); // ошибка listen
    } // if

    DWORD timeout = 100; // таймаут accept в миллисекундах (чтобы можно было прервать Stop)
    setsockopt(impl_->listen_sock, SOL_SOCKET, SO_RCVTIMEO, // устанавливаем таймаут на приём
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    impl_->running = true; // сервер работает
    impl_->accept_thread = std::thread([this]() { // запускаем поток accept
        while (impl_->running.load()) { // пока не попросили остановиться
            sockaddr_in client_addr{}; // адрес подключившегося клиента
            int addr_len = sizeof(client_addr); // размер структуры
            SOCKET client = accept(impl_->listen_sock, // принимаем соединение
                                   reinterpret_cast<sockaddr*>(&client_addr),
                                   &addr_len);
            if (client == INVALID_SOCKET) { // accept не удался (возможно таймаут или закрытие)
                if (!impl_->running.load()) break; // если останавливаемся — выходим
                continue; // иначе это таймаут, пробуем снова
            } // if

            DWORD client_timeout = 500; // таймаут recv для клиентского сокета (чтобы не висеть вечно)
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, // устанавливаем таймаут чтения
                       reinterpret_cast<const char*>(&client_timeout), sizeof(client_timeout));

            Session session; // создаём сессию для нового клиента
            session.client_id = "client_" + std::to_string(g_client_counter++); // генерируем уникальный id
            char ip_str[INET_ADDRSTRLEN]; // буфер для строки ip
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str)); // бинарный ip → строка
            session.remote_addr = ip_str; // сохраняем адрес клиента

            std::thread t([this, client, session]() mutable { // поток обработки одного клиента
                while (impl_->running.load()) { // пока сервер работает
                    char len_buf[4]; // буфер для длины сообщения
                    if (!recv_all(client, len_buf, 4)) break; // disconnect или таймаут — выходим
                    uint32_t msg_len = 0; // длина payload
                    std::memcpy(&msg_len, len_buf, 4); // читаем little-endian длину

                    std::string wire(4 + msg_len, '\0'); // буфер для полного сообщения с length-prefix
                    std::memcpy(wire.data(), len_buf, 4); // копируем длину
                    if (!recv_all(client, wire.data() + 4, static_cast<int>(msg_len))) break; // ошибка чтения

                    Request req{}; // десериализуем запрос
                    if (!Protocol::DeserializeRequest(wire, &req)) { // битый запрос
                        Response resp{}; // формируем ошибку протокола
                        resp.ok = false; // флаг ошибки
                        resp.error = "protocol deserialize error"; // описание
                        std::string out = Protocol::SerializeResponse(resp); // сериализуем
                        send_all(client, out.data(), static_cast<int>(out.size())); // отправляем ошибку
                        continue; // ждём следующий запрос
                    } // if deserialize error

                    Response resp = impl_->handler(session, req); // вызываем обработчик (StorageService)

                    std::string out = Protocol::SerializeResponse(resp); // сериализуем ответ
                    if (!send_all(client, out.data(), static_cast<int>(out.size()))) break; // ошибка отправки — выходим
                } // while
                closesocket(client); // закрываем сокет клиента
            }); // lambda для клиентского потока

            {
                std::lock_guard<std::mutex> lock(impl_->clients_mtx); // защищаем список потоков
                impl_->client_threads.push_back(std::move(t)); // сохраняем поток
            } // lock
        } // while running
    }); // accept_thread

    return Status::Ok(); // сервер успешно запущен
} // Start

Status TcpServer::Stop() {
    impl_->running = false; // сигнализируем потокам о завершении
    if (impl_->listen_sock != INVALID_SOCKET) { // если сокет ещё открыт
        closesocket(impl_->listen_sock); // закрываем слушающий сокет (прерывает accept)
        impl_->listen_sock = INVALID_SOCKET; // помечаем как закрытый
    } // if
    if (impl_->accept_thread.joinable()) { // если поток accept жив
        impl_->accept_thread.join(); // ждём завершения потока accept
    } // if
    { // ждём все клиентские потоки
        std::lock_guard<std::mutex> lock(impl_->clients_mtx); // защищаем список
        for (auto& t : impl_->client_threads) { // идём по всем клиентским потокам
            if (t.joinable()) t.join(); // ждём завершения каждого
        } // for
        impl_->client_threads.clear(); // очищаем список
    } // lock
    WSACleanup(); // освобождаем winsock
    return Status::Ok(); // сервер остановлен
} // Stop

} // namespace db
