#include "StreamDecoder.h"
#include "Display.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        const std::string pixels("\x0a\x0d\x00\xff\x80\x01", 6);
        const std::string wire = "hello\nrgb_dmd_frame?name=Main&bytes=6\n" + pixels + "trigger?name=after\n";
        // Every possible network split, including inside the size and pixels.
        for (size_t split = 0; split <= wire.size(); ++split) {
            bcp::StreamDecoder decoder;
            std::vector<bcp::Message> messages;
            auto receive = [&](bcp::Message&& message) { messages.push_back(std::move(message)); };
            decoder.Feed(std::string_view(wire).substr(0, split), receive);
            decoder.Feed(std::string_view(wire).substr(split), receive);
            Check(messages.size() == 3, "binary payload must not create spurious messages");
            Check(messages[1].binary == pixels && messages[2].GetValue("name") == "after", "binary framing preserves pixels and following event");
        }
        bcp::StreamDecoder decoder;
        int delivered = 0;
        for (char byte : wire) decoder.Feed(std::string_view(&byte, 1), [&](bcp::Message&&) { ++delivered; });
        Check(delivered == 3, "one-byte fragments");
        decoder.Feed("custom?bytes=0\ntrigger?name=zero\n", [&](bcp::Message&&) { ++delivered; });
        Check(delivered == 5, "zero-byte payload");
        for (const auto length : {"-1", "+1", "", "1x", "16777217", "184467440737095516160"}) {
            bool rejected = false;
            try {
                bcp::StreamDecoder invalid;
                invalid.Feed("rgb_dmd_frame?bytes=" + std::string(length) + "\n", [](bcp::Message&&) {});
            } catch (const std::runtime_error&) { rejected = true; }
            Check(rejected, "invalid binary length rejected");
        }
        bcp::Display display(2, 1);
        Check(display.Snapshot()->pixels == std::vector<uint8_t>(6), "initial black frame");
        display.Update(pixels);
        auto original = display.Snapshot();
        Check(original->pixels == std::vector<uint8_t>({10, 13, 0, 255, 128, 1}), "RGB channel order");
        display.Update(std::string("\x01\x02\x03\x00\x04\x05\x06\xff", 8));
        Check(display.Snapshot()->pixels == std::vector<uint8_t>({1, 2, 3, 4, 5, 6}), "RGBA alpha is removed");
        Check(display.Snapshot()->id != original->id && original->pixels[0] == 10, "published frame snapshots remain immutable");
        bool rejected = false;
        try { display.Update("bad"); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected && display.Snapshot()->pixels[0] == 1, "invalid frame does not replace valid frame");
        display.Reset();
        Check(display.Snapshot()->pixels == std::vector<uint8_t>(6), "disconnect clears stale image");
        for (auto dimensions : {std::pair{0u, 32u}, {128u, 0u}, {4097u, 1u}, {4096u, 4096u}}) {
            rejected = false;
            try { bcp::Display invalid(dimensions.first, dimensions.second); } catch (const std::invalid_argument&) { rejected = true; }
            Check(rejected, "invalid resolution rejected");
        }
        std::atomic<bool> done = false;
        std::thread producer([&] {
            for (int i = 0; i < 1000; ++i) display.Update(std::string(6, static_cast<char>(i)));
            done = true;
        });
        bool coherent = true;
        do {
            auto frame = display.Snapshot();
            for (auto byte : frame->pixels) if (byte != frame->pixels[0]) coherent = false;
        } while (!done);
        producer.join();
        Check(coherent, "concurrent consumers see complete frames");
        std::cout << "Binary framing, RGB/RGBA, immutable snapshots and bounds: OK\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
