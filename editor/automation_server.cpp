#include "automation_server.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <format>
#include <unordered_map>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace vespera::editor {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
int socket_last_error() { return WSAGetLastError(); }
bool socket_would_block(int error) { return error == WSAEWOULDBLOCK; }
void socket_close(SocketHandle socket) { if (socket != kInvalidSocket) closesocket(socket); }
bool socket_set_nonblocking(SocketHandle socket) {
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
}
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
int socket_last_error() { return errno; }
bool socket_would_block(int error) { return error == EAGAIN || error == EWOULDBLOCK; }
void socket_close(SocketHandle socket) { if (socket != kInvalidSocket) ::close(socket); }
bool socket_set_nonblocking(SocketHandle socket) {
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

struct ClientState {
    SocketHandle socket = kInvalidSocket;
    std::string input;
};

} // namespace

struct AutomationServer::Impl {
    SocketHandle listener = kInvalidSocket;
    std::uint16_t port = 0;
    std::uint64_t next_client_id = 1;
    std::unordered_map<std::uint64_t, ClientState> clients;
#if defined(_WIN32)
    bool winsock_ready = false;
#endif
};

AutomationServer::AutomationServer() : impl_(std::make_unique<Impl>()) {}
AutomationServer::~AutomationServer() { stop(); }

bool AutomationServer::start(std::uint16_t port, std::string* error) {
    stop();
#if defined(_WIN32)
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        if (error) *error = "WSAStartup failed";
        return false;
    }
    impl_->winsock_ready = true;
#endif

    impl_->listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (impl_->listener == kInvalidSocket) {
        if (error) *error = std::format("automation socket() failed: {}", socket_last_error());
        stop();
        return false;
    }

    int reuse = 1;
#if defined(_WIN32)
    setsockopt(impl_->listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    setsockopt(impl_->listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Never expose editor automation off-machine.
    if (::bind(impl_->listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        if (error) *error = std::format("automation bind() failed on 127.0.0.1:{}: {}", port, socket_last_error());
        stop();
        return false;
    }
    if (::listen(impl_->listener, 4) != 0 || !socket_set_nonblocking(impl_->listener)) {
        if (error) *error = std::format("automation listen/nonblocking setup failed: {}", socket_last_error());
        stop();
        return false;
    }
    impl_->port = port;
    return true;
}

void AutomationServer::stop() {
    if (!impl_) return;
    for (auto& [id, client] : impl_->clients) {
        (void)id;
        socket_close(client.socket);
    }
    impl_->clients.clear();
    socket_close(impl_->listener);
    impl_->listener = kInvalidSocket;
    impl_->port = 0;
#if defined(_WIN32)
    if (impl_->winsock_ready) {
        WSACleanup();
        impl_->winsock_ready = false;
    }
#endif
}

bool AutomationServer::running() const { return impl_ && impl_->listener != kInvalidSocket; }
std::uint16_t AutomationServer::port() const { return impl_ ? impl_->port : 0; }

std::vector<AutomationIncomingRequest> AutomationServer::poll() {
    std::vector<AutomationIncomingRequest> out;
    if (!running()) return out;

    for (;;) {
        sockaddr_in address{};
#if defined(_WIN32)
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        const SocketHandle socket = ::accept(impl_->listener, reinterpret_cast<sockaddr*>(&address), &length);
        if (socket == kInvalidSocket) {
            const int error = socket_last_error();
            if (!socket_would_block(error)) {
                // Keep existing server alive; a transient accept error should not mutate editor state.
            }
            break;
        }
        if (!socket_set_nonblocking(socket)) {
            socket_close(socket);
            continue;
        }
        impl_->clients.emplace(impl_->next_client_id++, ClientState{socket, {}});
    }

    std::vector<std::uint64_t> disconnected;
    char buffer[4096];
    for (auto& [client_id, client] : impl_->clients) {
        bool close_client = false;
        for (;;) {
#if defined(_WIN32)
            const int received = ::recv(client.socket, buffer, static_cast<int>(sizeof(buffer)), 0);
#else
            const ssize_t received = ::recv(client.socket, buffer, sizeof(buffer), 0);
#endif
            if (received > 0) {
                client.input.append(buffer, static_cast<std::size_t>(received));
                if (client.input.size() > 1024 * 1024) {
                    close_client = true;
                    break;
                }
                continue;
            }
            if (received == 0) close_client = true;
            if (received < 0) {
                const int error = socket_last_error();
                if (!socket_would_block(error)) close_client = true;
            }
            break;
        }

        std::size_t newline = 0;
        while (!close_client && (newline = client.input.find('\n')) != std::string::npos) {
            std::string line = client.input.substr(0, newline);
            client.input.erase(0, newline + 1);
            std::string parse_error;
            if (auto request = parse_automation_request(line, &parse_error)) {
                out.push_back({client_id, std::move(*request)});
            } else {
                AutomationResponse response;
                response.ok = false;
                response.fields["text"] = parse_error;
                respond(client_id, response);
            }
        }
        if (close_client) disconnected.push_back(client_id);
    }

    for (const auto client_id : disconnected) {
        const auto it = impl_->clients.find(client_id);
        if (it != impl_->clients.end()) {
            socket_close(it->second.socket);
            impl_->clients.erase(it);
        }
    }
    return out;
}

bool AutomationServer::respond(std::uint64_t client_id, const AutomationResponse& response) {
    if (!impl_) return false;
    const auto it = impl_->clients.find(client_id);
    if (it == impl_->clients.end()) return false;
    const std::string line = encode_automation_response(response);
    std::size_t sent_total = 0;
    while (sent_total < line.size()) {
#if defined(_WIN32)
        const int sent = ::send(it->second.socket, line.data() + sent_total,
            static_cast<int>(line.size() - sent_total), 0);
#else
        const ssize_t sent = ::send(it->second.socket, line.data() + sent_total, line.size() - sent_total, 0);
#endif
        if (sent <= 0) return false;
        sent_total += static_cast<std::size_t>(sent);
    }
    return true;
}

} // namespace vespera::editor
