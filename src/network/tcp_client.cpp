#include "network/tcp_client.h"

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

TcpClient::TcpClient(std::string host, int port)
    : host_(std::move(host)), port_(port) {}

Status TcpClient::Send(const Request& req, Response* out) {
    if (!PlatformInit()) {
        return Status::Error(StatusCode::kNetworkError, "socket platform init failed");
    }

    socket_t sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == kInvalidSocket) {
        PlatformCleanup();
        return Status::Error(StatusCode::kNetworkError, "socket creation failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(static_cast<uint16_t>(port_));
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) {
        CLOSE_SOCKET(sock);
        PlatformCleanup();
        return Status::Error(StatusCode::kNetworkError, "invalid server address: " + host_);
    }

    if (::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCK_ERR) {
        CLOSE_SOCKET(sock);
        PlatformCleanup();
        return Status::Error(StatusCode::kNetworkError,
                             "connect failed to " + host_ + ":" + std::to_string(port_));
    }

    std::string   req_data = Protocol::SerializeRequest(req);
    std::uint32_t req_len  = static_cast<std::uint32_t>(req_data.size());

    if (!SendAll(sock, reinterpret_cast<const char*>(&req_len), sizeof(req_len))) {
        CLOSE_SOCKET(sock);
        PlatformCleanup();
        return Status::Error(StatusCode::kNetworkError, "failed to send request length");
    }
    if (!SendAll(sock, req_data.data(), static_cast<int>(req_data.size()))) {
        CLOSE_SOCKET(sock);
        PlatformCleanup();
        return Status::Error(StatusCode::kNetworkError, "failed to send request body");
    }

    std::uint32_t resp_len = 0;
    if (!RecvAll(sock, reinterpret_cast<char*>(&resp_len), sizeof(resp_len))) {
        CLOSE_SOCKET(sock);
        PlatformCleanup();
        return Status::Error(StatusCode::kNetworkError, "failed to read response length");
    }

    std::string resp_data(resp_len, '\0');
    if (!RecvAll(sock, resp_data.data(), static_cast<int>(resp_len))) {
        CLOSE_SOCKET(sock);
        PlatformCleanup();
        return Status::Error(StatusCode::kNetworkError, "failed to read response body");
    }

    CLOSE_SOCKET(sock);
    PlatformCleanup();

    if (!Protocol::DeserializeResponse(resp_data, out)) {
        return Status::Error(StatusCode::kNetworkError, "failed to deserialize response");
    }
    return Status::Ok();
}

}  
