#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <vector>

namespace bcp {
struct PixelFrame {
    explicit PixelFrame(size_t pixelCount);
    PixelFrame(const PixelFrame&) = delete;
    PixelFrame& operator=(const PixelFrame&) = delete;
    unsigned int id = 0;
    std::span<const uint8_t> pixels;
private:
    friend class Display;
    std::unique_ptr<uint8_t[]> storage;
};

class Display {
public:
    Display(unsigned int width, unsigned int height);
    const unsigned int width, height;
    void Update(std::string_view pixels);
    struct WriteFrame {
        std::shared_ptr<PixelFrame> frame;
        std::span<uint8_t> bytes;
        uint64_t generation;
    };
    WriteFrame PrepareFrame(size_t byteCount);
    void PublishFrame(WriteFrame frame);
    void Reset();
    std::shared_ptr<const PixelFrame> Snapshot() const;
private:
    mutable std::mutex mutex;
    std::shared_ptr<const PixelFrame> blank, latest;
    unsigned int sequence = 0;
    uint64_t generation = 0;
    struct FramePool;
    std::shared_ptr<FramePool> pool;
};
}
