#pragma once
#include "Message.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <filesystem>

namespace bcp {
class Client {
public:
    ~Client();
    void Connect(int port);
    void Disconnect();
    void Send(std::string message);
    std::vector<Message> Drain();
    bool Pop(Message& message);
    bool Connected() const { return connected; }
    std::string LastError() const;
private:
    void Run(int port);
    void Error(std::string message);
    std::atomic<bool> stop{true}, connected{false};
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> outgoing;
    std::deque<Message> incoming;
    std::string error;
    size_t queuedBytes = 0, receivedBytes = 0;
};
// Launch a program directly, without shell command interpolation. Never terminates it.
std::filesystem::path ResolveExecutable(const std::string& executable);
void Launch(const std::string& executable, const std::string& project = "");
}
