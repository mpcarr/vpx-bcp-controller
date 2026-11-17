#include "BcpClient.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
void Check(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class F> void Wait(F&& test, const char* text) {
    const auto end = std::chrono::steady_clock::now() + 10s;
    while (std::chrono::steady_clock::now() < end) {
        if (test()) return;
        std::this_thread::sleep_for(1ms);
    }
    throw std::runtime_error(text);
}
int main(int argc, char** argv) {
    try {
        if (argc != 2) return 2;
        bcp::Client client;
        auto display = client.RegisterDisplay("Main", 1280, 720);
        client.Connect(std::stoi(argv[1]));
        std::shared_ptr<const bcp::PixelFrame> retained;
        int events = 0;
        bool replaced = false;
        Wait([&] {
            bcp::Message message;
            while (client.Pop(message)) {
                if (message.command == "ping") {
                    ++events;
                    client.Send("pong?seq=" + message.GetValue("seq"));
                } else if (message.command == "hold") {
                    retained = display->Snapshot();
                    Check(retained->id == 1 && retained->pixels[0] == 1, "first frame missing");
                    client.Send("held");
                } else if (message.command == "replace") {
                    client.ClearDisplays();
                    display = client.RegisterDisplay("Main", 1280, 720);
                    replaced = true;
                    client.Send("replaced");
                } else if (message.command == "complete") {
                    Check(events == 24 && replaced, "mixed events lost");
                    auto frame = display->Snapshot();
                    Check(frame->id == 1 || frame->id == 2, "unexpected frames on replacement registration");
                    for (auto byte : frame->pixels) Check(byte == 77, "large RGB frame corrupt");
                    for (auto byte : retained->pixels) Check(byte == 1, "retained frame overwritten");
                    client.Send("checked");
                    return true;
                }
            }
            return false;
        }, "large frame/event processing timed out");
        Wait([&] { return !client.Connected(); }, "peer loss was not observed");
        Wait([&] { return display->Snapshot()->id == 0; }, "truncated frame or disconnect left a stale image");
        client.Disconnect();
        client.Connect(std::stoi(argv[1]));
        Wait([&] { return display->Snapshot()->id != 0; }, "reconnect failed to publish");
        for (auto byte : display->Snapshot()->pixels) Check(byte == 88, "RGBA reconnect corrupt");
        client.Send("reconnected");
        std::this_thread::sleep_for(100ms);
        client.Disconnect();
        std::cout << "Large frames, mixed events, registration replacement, retained readers, truncation and reconnect: OK\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
