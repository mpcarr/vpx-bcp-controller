#include "BcpClient.h"
#include "StreamDecoder.h"
#include <chrono>
#include <stdexcept>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace bcp {
namespace {
using namespace std::chrono_literals;
constexpr size_t Limit = 16 * 1024 * 1024;
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket Invalid = INVALID_SOCKET;
void Close(Socket s) { closesocket(s); }
bool Pending() { int e = WSAGetLastError(); return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
struct Network { Network() { WSADATA data; if (WSAStartup(MAKEWORD(2,2), &data)) throw std::runtime_error("WSAStartup failed"); } ~Network() { WSACleanup(); } };
#else
using Socket = int;
constexpr Socket Invalid = -1;
void Close(Socket s) { close(s); }
bool Pending() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS || errno == EINTR; }
struct Network {};
#endif
struct Handle { Socket value = Invalid; ~Handle() { if (value != Invalid) Close(value); } };
bool Wait(Socket s, bool writing) {
    fd_set set; FD_ZERO(&set); FD_SET(s, &set);
    timeval timeout{0, 20000};
    return select(static_cast<int>(s + 1), writing ? nullptr : &set, writing ? &set : nullptr, nullptr, &timeout) > 0;
}
int Write(Socket s, const char* data, int length) {
#ifdef MSG_NOSIGNAL
    return static_cast<int>(send(s, data, length, MSG_NOSIGNAL));
#else
    return static_cast<int>(send(s, data, length, 0));
#endif
}
}
Client::~Client() { Disconnect(); }
void Client::Error(std::string message) { std::lock_guard lock(mutex); error = std::move(message); }
std::string Client::LastError() const { std::lock_guard lock(mutex); return error; }
void Client::Connect(int port) {
    if (port < 1 || port > 65535) throw std::invalid_argument("Port must be between 1 and 65535");
    Disconnect();
    { std::lock_guard lock(mutex); error.clear(); }
    stop = false;
    try {
        worker = std::thread([this, port] { try { Run(port); } catch (const std::exception& e) { Error(e.what()); } connected = false; stop = true; ResetDisplays(); });
    } catch (...) { stop = true; throw; }
}
void Client::Disconnect() {
    stop = true; wake.notify_all();
    if (worker.joinable()) worker.join();
    connected = false;
    ResetDisplays();
    std::lock_guard lock(mutex);
    outgoing.clear(); incoming.clear(); queuedBytes = receivedBytes = 0;
}
void Client::Send(std::string message) {
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n')) message.pop_back();
    if (message.find_first_of("\r\n") != std::string::npos) throw std::invalid_argument("Send expects one BCP line");
    message += '\n';
    std::lock_guard lock(mutex);
    if (stop) throw std::runtime_error("BCP connection is not active");
    if (outgoing.size() >= 65536 || queuedBytes + message.size() > Limit) throw std::runtime_error("BCP outgoing queue is full");
    queuedBytes += message.size(); outgoing.push_back(std::move(message));
}
std::vector<Message> Client::Drain() {
    std::lock_guard lock(mutex);
    std::vector<Message> result;
    result.reserve(incoming.size());
    while (!incoming.empty()) { result.push_back(std::move(incoming.front())); incoming.pop_front(); }
    receivedBytes = 0;
    return result;
}
bool Client::Pop(Message& message) {
    std::lock_guard lock(mutex);
    if (incoming.empty()) return false;
    receivedBytes -= incoming.front().raw.size() + incoming.front().binary.size();
    message = std::move(incoming.front()); incoming.pop_front(); return true;
}
std::shared_ptr<Display> Client::RegisterDisplay(const std::string& name, unsigned int width, unsigned int height) {
    if (name.empty() || name.size() > 256) throw std::invalid_argument("Display name must contain 1..256 bytes");
    auto display = std::make_shared<Display>(width, height);
    std::lock_guard lock(mutex);
    if (displays.contains(name)) throw std::invalid_argument("Display name is already registered on this connection");
    if (displays.size() >= 16) throw std::invalid_argument("At most 16 displays are supported per connection");
    displays.emplace(name, display);
    return display;
}
void Client::ClearDisplays() { std::lock_guard lock(mutex); displays.clear(); }
void Client::ResetDisplays() {
    std::lock_guard lock(mutex);
    for (auto& [name, display] : displays) display->Reset();
}
void Client::Receive(Message message) {
    if (message.command == "rgb_dmd_frame") {
        std::shared_ptr<Display> display;
        {
            std::lock_guard lock(mutex);
            auto item = displays.find(message.GetValue("name"));
            if (item != displays.end()) display = item->second;
        }
        // Display frames never enter the script's event queue. Keep only the
        // latest complete frame, and discard outputs the table did not register.
        if (display) display->Update(message.binary);
        return;
    }
    const size_t size = message.raw.size() + message.binary.size();
    std::lock_guard lock(mutex);
    if (incoming.size() >= 65536 || size > Limit - receivedBytes) throw std::runtime_error("BCP incoming queue is full");
    receivedBytes += size;
    incoming.push_back(std::move(message));
}
void Client::Run(int port) {
    Network network;
    Handle socket;
    for (int attempt = 0; attempt < 3 && !stop; ++attempt) {
        if (socket.value != Invalid) Close(socket.value);
        socket.value = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket.value == Invalid) throw std::runtime_error("Cannot create BCP socket");
#ifdef _WIN32
        SetHandleInformation(reinterpret_cast<HANDLE>(socket.value), HANDLE_FLAG_INHERIT, 0);
        u_long enabled = 1;
        if (ioctlsocket(socket.value, FIONBIO, &enabled) != 0) throw std::runtime_error("Cannot make BCP socket nonblocking");
#else
        if (fcntl(socket.value, F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(socket.value, F_SETFL, fcntl(socket.value, F_GETFL) | O_NONBLOCK) < 0)
            throw std::runtime_error("Cannot configure BCP socket");
#ifdef SO_NOSIGPIPE
        int enabled = 1; setsockopt(socket.value, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
#endif
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(static_cast<unsigned short>(port));
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int result = ::connect(socket.value, reinterpret_cast<sockaddr*>(&address), sizeof(address));
        if (result == 0) connected = true;
        else if (Pending()) {
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (!stop && std::chrono::steady_clock::now() < deadline) {
                if (!Wait(socket.value, true)) continue;
                int status = 0;
#ifdef _WIN32
                int size = sizeof(status);
#else
                socklen_t size = sizeof(status);
#endif
                if (getsockopt(socket.value, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&status), &size) == 0 && status == 0) connected = true;
                break;
            }
        }
        if (connected) break;
        if (attempt < 2) { std::unique_lock lock(mutex); wake.wait_for(lock, 5s, [this] { return stop.load(); }); }
    }
    if (!connected) { if (!stop) Error("Cannot connect to localhost:" + std::to_string(port)); return; }
    // Preserve legacy handshake value; all protocol lines now include their terminator.
    std::string pending = "hello?version=21&controller_name=VPX&controller_version=0.1.0\n";
    StreamDecoder decoder;
    size_t offset = 0;
    while (!stop) {
        if (pending.empty()) {
            std::lock_guard lock(mutex);
            if (!outgoing.empty()) { pending = std::move(outgoing.front()); outgoing.pop_front(); queuedBytes -= pending.size(); offset = 0; }
        }
        if (!pending.empty()) {
            int n = Write(socket.value, pending.data() + offset, static_cast<int>(pending.size() - offset));
            if (n > 0) { offset += n; if (offset == pending.size()) pending.clear(); }
            else if (n == 0 || !Pending()) throw std::runtime_error("BCP send failed");
        }
        // Read without blocking when writes remain; otherwise sleep in select, avoiding a busy loop.
        bool readable = !pending.empty();
        if (!readable) {
            bool queued;
            { std::lock_guard lock(mutex); queued = !outgoing.empty(); }
            readable = queued || Wait(socket.value, false);
        }
        if (readable) {
            char bytes[8192];
            int n = static_cast<int>(recv(socket.value, bytes, sizeof(bytes), 0));
            if (n == 0) throw std::runtime_error("BCP peer disconnected");
            if (n < 0) { if (!Pending()) throw std::runtime_error("BCP receive failed"); if (!pending.empty()) Wait(socket.value, true); continue; }
            decoder.Feed(std::string_view(bytes, n), [this](Message&& message) { Receive(std::move(message)); });
        }
    }
    // Do not append a goodbye to a partially transmitted application line.
    if (pending.empty()) Write(socket.value, "goodbye\n", 8);
}
}
