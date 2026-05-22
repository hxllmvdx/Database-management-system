#include "network/tcp_server.h"

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
   using socket_t = SOCKET;
   static constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#  define CLOSE_SOCKET(s) closesocket(s)
#  define SOCK_ERR        SOCKET_ERROR
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
   using socket_t = int;
   static constexpr socket_t kInvalidSocket = -1;
#  define CLOSE_SOCKET(s) ::close(s)
#  define SOCK_ERR        (-1)
#endif

#include <cstdint>
#include <cstring>
#include <thread>

#include "network/protocol.h"
#include "common/status.h"

namespace db {

namespace {

bool PlatformInit() {
#ifdef _WIN32
    WSADATA wsa;
    return WSAStartup(MAKEWORD(2, 2), &wsa) == 0;
#else
    return true;
#endif
}

void PlatformCleanup() {
#ifdef _WIN32
    WSACleanup();
#endif
}

bool SendAll(socket_t s, const char* data, int len) {
    int sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int r = send(s, data + sent, len - sent, 0);
        if (r == SOCK_ERR) return false;
#else
        ssize_t r = ::send(s, data + sent, static_cast<std::size_t>(len - sent), 0);
        if (r <= 0) return false;
#endif
        sent += static_cast<int>(r);
    }
    return true;
}

bool RecvAll(socket_t s, char* data, int len) {
    int received = 0;
    while (received < len) {
#ifdef _WIN32
        int r = recv(s, data + received, len - received, 0);
        if (r <= 0) return false;
#else
        ssize_t r = ::recv(s, data + received, static_cast<std::size_t>(len - received), 0);
        if (r <= 0) return false;
#endif
        received += static_cast<int>(r);
    }
    return true;
}

}  

TcpServer::TcpServer(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

Status TcpServer::Start(Handler handler) {
    if (!PlatformInit()) {
        return Status::Error(StatusCode::kNetworkError, "socket platform init failed");
    }

    socket_t listen_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd == kInvalidSocket) {
        PlatformCleanup();
        return Status::Error(StatusCode::kNetworkError, "socket creation failed");
    }

    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port_));
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        CLOSE_SOCKET(listen_fd);
        PlatformCleanup();
        return Status::Error(StatusCode::kNetworkError, "invalid bind address: " + host_);
    }

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCK_ERR) {
        CLOSE_SOCKET(listen_fd);
        PlatformCleanup();
        return Status::Error(StatusCode::kNetworkError,
                             "bind failed on " + host_ + ":" + std::to_string(port_));
    }

    if (::listen(listen_fd, SOMAXCONN) == SOCK_ERR) {
        CLOSE_SOCKET(listen_fd);
        PlatformCleanup();
        return Status::Error(StatusCode::kNetworkError, "listen failed");
    }

    listen_socket_ = reinterpret_cast<void*>(static_cast<uintptr_t>(listen_fd));
    running_       = true;

    thread_ = std::thread([this, handler]() {
        const socket_t lfd =
            static_cast<socket_t>(reinterpret_cast<uintptr_t>(listen_socket_));

        while (running_.load(std::memory_order_relaxed)) {
            sockaddr_in client_addr{};
            socklen_t   client_len = sizeof(client_addr);
            socket_t    client_fd  = ::accept(
                lfd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

            if (client_fd == kInvalidSocket) {
                break;
            }

            
            std::thread([client_fd, handler, client_addr]() {
                char remote_buf[INET_ADDRSTRLEN] = {};
                ::inet_ntop(AF_INET, &client_addr.sin_addr,
                            remote_buf, sizeof(remote_buf));

                std::uint32_t req_len = 0;
                if (!RecvAll(client_fd,
                             reinterpret_cast<char*>(&req_len),
                             sizeof(req_len))) {
                    CLOSE_SOCKET(client_fd);
                    return;
                }

                std::string req_data(req_len, '\0');
                if (!RecvAll(client_fd, req_data.data(),
                             static_cast<int>(req_len))) {
                    CLOSE_SOCKET(client_fd);
                    return;
                }

                Request  req;
                Response resp;
                if (!Protocol::DeserializeRequest(req_data, &req)) {
                    resp.ok    = false;
                    resp.error = "failed to deserialize request";
                } else {
                    Session session;
                    session.client_id   = req.request_id;
                    session.remote_addr = std::string(remote_buf);
                    resp = handler(session, req);
                }

                std::string   resp_data = Protocol::SerializeResponse(resp);
                std::uint32_t resp_len =
                    static_cast<std::uint32_t>(resp_data.size());
                if (!SendAll(client_fd,
                             reinterpret_cast<const char*>(&resp_len),
                             sizeof(resp_len))) {
                    CLOSE_SOCKET(client_fd);
                    return;
                }
                SendAll(client_fd, resp_data.data(),
                        static_cast<int>(resp_data.size()));
                CLOSE_SOCKET(client_fd);
            }).detach();
        }
    });

    return Status::Ok();
}

Status TcpServer::Stop() {
    running_ = false;
    if (listen_socket_ != nullptr) {
        socket_t lfd =
            static_cast<socket_t>(reinterpret_cast<uintptr_t>(listen_socket_));
        CLOSE_SOCKET(lfd);
        listen_socket_ = nullptr;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    PlatformCleanup();
    return Status::Ok();
}

}  
