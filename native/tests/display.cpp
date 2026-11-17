#include "StreamDecoder.h"
#include "Display.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <algorithm>
#include <cstring>
#include <set>

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
bool Equal(std::span<const uint8_t> a, const std::vector<uint8_t>& b) { return std::ranges::equal(a, b); }
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

            // Exercise the production direct-to-frame route at every split too.
            bcp::StreamDecoder direct;
            bcp::Display output(2, 1);
            int events = 0;
            const bcp::StreamDecoder::PrepareBinary prepare = [&](const bcp::Message&, size_t size) {
                auto frame = output.PrepareFrame(size);
                auto bytes = frame.bytes;
                return bcp::StreamDecoder::BinaryTarget{bytes, [&, frame = std::move(frame)]() mutable { output.PublishFrame(std::move(frame)); }};
            };
            const auto receiveEvent = [&](bcp::Message&&) { ++events; };
            direct.Feed(std::string_view(wire).substr(0, split), receiveEvent, prepare);
            direct.Feed(std::string_view(wire).substr(split), receiveEvent, prepare);
            Check(events == 2 && output.Snapshot()->id == 1 && Equal(output.Snapshot()->pixels, {10, 13, 0, 255, 128, 1}), "direct binary framing");
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
        Check(Equal(display.Snapshot()->pixels, std::vector<uint8_t>(6)), "initial black frame");
        display.Update(pixels);
        auto original = display.Snapshot();
        Check(Equal(original->pixels, {10, 13, 0, 255, 128, 1}), "RGB channel order");
        display.Update(std::string("\x01\x02\x03\x00\x04\x05\x06\xff", 8));
        Check(Equal(display.Snapshot()->pixels, {1, 2, 3, 4, 5, 6}), "RGBA alpha is removed");
        Check(display.Snapshot()->id != original->id && original->pixels[0] == 10, "published frame snapshots remain immutable");
        bool rejected = false;
        try { display.Update("bad"); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected && display.Snapshot()->pixels[0] == 1, "invalid frame does not replace valid frame");
        display.Reset();
        Check(Equal(display.Snapshot()->pixels, std::vector<uint8_t>(6)), "disconnect clears stale image");

        auto stale = display.PrepareFrame(6);
        std::memset(stale.bytes.data(), 99, stale.bytes.size());
        display.Reset();
        display.PublishFrame(std::move(stale));
        Check(display.Snapshot()->id == 0, "reset invalidates in-flight writes");

        {
            bcp::Display pooled(32, 1);
            std::vector<std::shared_ptr<const bcp::PixelFrame>> held;
            for (int i = 1; i <= 8; ++i) {
                pooled.Update(std::string(96, static_cast<char>(i)));
                held.push_back(pooled.Snapshot());
            }
            for (int i = 0; i < 50; ++i) pooled.Update(std::string(96, 99));
            for (size_t i = 0; i < held.size(); ++i) {
                Check(held[i]->id == i + 1, "retained frame ids are immutable");
                for (auto byte : held[i]->pixels) Check(byte == i + 1, "pool never overwrites retained pixels");
            }
            held.clear();
            std::set<const void*> storage;
            for (int i = 0; i < 100; ++i) {
                auto frame = pooled.PrepareFrame(i % 2 ? 96 : 128);
                storage.insert(frame.bytes.data());
                std::fill(frame.bytes.begin(), frame.bytes.end(), uint8_t{17});
                auto address = frame.bytes.data();
                pooled.PublishFrame(std::move(frame));
                Check(pooled.Snapshot()->pixels.data() == address, "publishing keeps original body storage");
                Check(pooled.Snapshot()->pixels.size() == 96, "RGBA capacity is not exposed to consumers");
            }
            Check(storage.size() <= 2, "steady-state RGB/RGBA reuses released buffers");
        }
        std::shared_ptr<const bcp::PixelFrame> survivor;
        {
            bcp::Display temporary(2, 1);
            temporary.Update(pixels);
            survivor = temporary.Snapshot();
        }
        Check(Equal(survivor->pixels, {10, 13, 0, 255, 128, 1}), "snapshot survives display/pool owner destruction");
        survivor.reset();

        {
            auto active = std::make_shared<bcp::Display>(2, 1);
            auto old = active;
            bcp::StreamDecoder changing;
            const bcp::StreamDecoder::PrepareBinary prepare = [&](const bcp::Message&, size_t size) {
                auto owner = active;
                auto pending = owner->PrepareFrame(size);
                auto bytes = pending.bytes;
                return bcp::StreamDecoder::BinaryTarget{bytes, [owner, pending = std::move(pending)]() mutable { owner->PublishFrame(std::move(pending)); }};
            };
            changing.Feed("rgb_dmd_frame?bytes=6\nabc", [](bcp::Message&&) {}, prepare);
            active = std::make_shared<bcp::Display>(2, 1);
            changing.Feed("def", [](bcp::Message&&) {}, prepare);
            Check(old->Snapshot()->id == 1 && active->Snapshot()->id == 0, "pending frame stays with registration chosen at header");
        }

        {
            bcp::StreamDecoder skipped;
            int events = 0;
            const auto skip = [](const bcp::Message& m, size_t) -> std::optional<bcp::StreamDecoder::BinaryTarget> {
                return m.command == "rgb_dmd_frame" ? std::optional{bcp::StreamDecoder::BinaryTarget{}} : std::nullopt;
            };
            const auto receive = [&](bcp::Message&& m) {
                if (m.command == "custom") Check(m.binary == pixels, "other binary messages retain their bodies");
                ++events;
            };
            skipped.Feed("rgb_dmd_frame?name=unused&bytes=16777216\n", receive, skip);
            const std::string chunk(65536, '\n');
            for (int i = 0; i < 256; ++i) skipped.Feed(chunk, receive, skip);
            skipped.Feed("custom?bytes=6\n" + pixels + "trigger?name=after\n", receive, skip);
            Check(events == 2, "discarded bodies preserve framing and following events");
        }
        {
            bcp::Display partial(2, 1);
            int completed = 0;
            {
                bcp::StreamDecoder truncated;
                truncated.Feed("rgb_dmd_frame?bytes=6\nabc", [](bcp::Message&&) {}, [&](const bcp::Message&, size_t size) {
                    auto pending = partial.PrepareFrame(size);
                    auto bytes = pending.bytes;
                    return bcp::StreamDecoder::BinaryTarget{bytes, [&, pending = std::move(pending)]() mutable {
                        ++completed; partial.PublishFrame(std::move(pending));
                    }};
                });
                Check(partial.Snapshot()->id == 0, "partial frame is never published");
            }
            Check(completed == 0, "truncation releases pending storage without publishing");
            partial.Update(pixels);
            Check(partial.Snapshot()->id == 1, "next frame works after abandoned body");
        }
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
