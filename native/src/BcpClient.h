#pragma once
#include "Message.h"
#include "Display.h"
#include "StreamDecoder.h"
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
    std::shared_ptr<Display> RegisterDisplay(const std::string& name, unsigned int width, unsigned int height);
    void ClearDisplays();
    bool Connected() const { return connected; }
    std::string LastError() const;
private:
    void Run(int port);
    void Error(std::string message);
    void Receive(Message message);
    std::optional<StreamDecoder::BinaryTarget> PrepareBinary(const Message& message, size_t byteCount);
    void ResetDisplays();
    std::atomic<bool> stop{true}, connected{false};
    std::thread worker;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> outgoing;
    std::deque<Message> incoming;
    std::map<std::string, std::shared_ptr<Display>> displays;
    std::string error;
    size_t queuedBytes = 0, receivedBytes = 0;
};
// Launch a program directly, without shell command interpolation. Never terminates it.
std::filesystem::path ResolveExecutable(const std::string& executable, const std::filesystem::path& baseDirectory = {});
void Launch(const std::string& executable, const std::string& project = "", const std::filesystem::path& baseDirectory = {});
}
