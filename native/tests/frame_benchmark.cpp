#include "Display.h"
#include "StreamDecoder.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <stdexcept>

// Measures only decoder + frame conversion/publication, not sockets or rendering.
// BCP_BENCHMARK_BASELINE allows the same workload to build with pre-optimisation sources.
int main(int argc, char** argv) {
    try {
        const unsigned w = argc > 1 ? std::stoul(argv[1]) : 1280;
        const unsigned h = argc > 2 ? std::stoul(argv[2]) : 720;
        const unsigned stride = argc > 3 ? std::stoul(argv[3]) : 4;
        const size_t chunk = argc > 4 ? std::stoul(argv[4]) : 65536;
        const int frames = argc > 5 ? std::stoi(argv[5]) : 300;
        if ((stride != 3 && stride != 4) || !chunk || frames < 1) return 2;
        bcp::Display display(w, h);
        bcp::StreamDecoder decoder;
        std::string pixels(static_cast<size_t>(w) * h * stride, '\0');
        for (size_t i = 0; i < pixels.size(); ++i) pixels[i] = static_cast<char>(i % 251);
        const std::string wire = "rgb_dmd_frame?name=Main&bytes=" + std::to_string(pixels.size()) + "\n" + pixels;
        const auto receive = [&](bcp::Message&& message) { display.Update(message.binary); };
#ifndef BCP_BENCHMARK_BASELINE
        const bcp::StreamDecoder::PrepareBinary prepare = [&](const bcp::Message&, size_t size) {
            auto pending = display.PrepareFrame(size);
            auto bytes = pending.bytes;
            return bcp::StreamDecoder::BinaryTarget{bytes, [&, pending = std::move(pending)]() mutable { display.PublishFrame(std::move(pending)); }};
        };
#endif
        // Retain an older consumer snapshot just as VPX does between frame calls.
        std::shared_ptr<const bcp::PixelFrame> reader;
        auto feed = [&] {
            for (size_t offset = 0; offset < wire.size(); offset += chunk) {
                const auto bytes = std::string_view(wire).substr(offset, std::min(chunk, wire.size() - offset));
#ifdef BCP_BENCHMARK_BASELINE
                decoder.Feed(bytes, receive);
#else
                decoder.Feed(bytes, receive, prepare);
#endif
            }
            reader = display.Snapshot();
        };
        for (int i = 0; i < 30; ++i) feed();
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < frames; ++i) feed();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
        if (reader->id != static_cast<unsigned>(frames + 30)) throw std::runtime_error("Missing published frames");
        for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i)
            for (size_t c = 0; c < 3; ++c)
                if (reader->pixels[i * 3 + c] != static_cast<uint8_t>(pixels[i * stride + c])) throw std::runtime_error("Corrupt pixels");
        std::cout << w << 'x' << h << " RGB" << (stride == 4 ? "A" : "") << " chunk=" << chunk
            << " frames=" << frames << " ms/frame=" << std::fixed << std::setprecision(3) << ms << '\n';
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
