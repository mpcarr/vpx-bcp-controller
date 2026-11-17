#include "Display.h"
#include "StreamDecoder.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace bcp {
PixelFrame::PixelFrame(size_t count) : storage(std::make_unique_for_overwrite<uint8_t[]>(count * 4)) {
    pixels = {storage.get(), count * 3};
}
struct Display::FramePool {
    explicit FramePool(size_t count) : count(count) { available.reserve(3); }
    const size_t count;
    std::mutex mutex;
    std::vector<std::unique_ptr<PixelFrame>> available;
    std::unique_ptr<PixelFrame> Take() {
        {
            std::lock_guard lock(mutex);
            if (!available.empty()) {
                auto frame = std::move(available.back());
                available.pop_back();
                return frame;
            }
        }
        return std::make_unique<PixelFrame>(count);
    }
    void Return(PixelFrame* value) {
        std::unique_ptr<PixelFrame> frame(value);
        std::lock_guard lock(mutex);
        if (available.size() < 3) available.push_back(std::move(frame));
    }
};
Display::Display(unsigned int w, unsigned int h) : width(w), height(h) {
    if (!w || !h || w > 4096 || h > 4096 || static_cast<uint64_t>(w) * h * 4 > StreamDecoder::Limit)
        throw std::invalid_argument("Display dimensions must be 1..4096 and fit within a 16 MiB RGBA frame");
    pool = std::make_shared<FramePool>(static_cast<size_t>(w) * h);
    auto frame = std::make_shared<PixelFrame>(static_cast<size_t>(w) * h);
    std::fill_n(frame->storage.get(), frame->pixels.size(), uint8_t{0});
    blank = latest = std::move(frame);
}
void Display::Update(std::string_view pixels) {
    auto frame = PrepareFrame(pixels.size());
    std::memcpy(frame.bytes.data(), pixels.data(), pixels.size());
    PublishFrame(std::move(frame));
}
Display::WriteFrame Display::PrepareFrame(size_t byteCount) {
    const size_t count = static_cast<size_t>(width) * height;
    if (byteCount != count * 3 && byteCount != count * 4)
        throw std::runtime_error("RGB DMD frame length does not match registered dimensions (expected RGB8 or RGBA8)");
    uint64_t currentGeneration;
    {
        std::lock_guard lock(mutex);
        currentGeneration = generation;
    }
    // Only the final owner returns a frame. The pool's mutex synchronises that
    // release with the next writer; there is no use_count/reader race. The pool
    // state can outlive Display if a consumer still holds a snapshot.
    auto storage = pool->Take();
    auto frame = std::shared_ptr<PixelFrame>(storage.release(), [owner = pool](PixelFrame* value) { owner->Return(value); });
    return {frame, {frame->storage.get(), byteCount}, currentGeneration};
}
void Display::PublishFrame(WriteFrame pending) {
    const size_t count = static_cast<size_t>(width) * height;
    if (pending.bytes.size() == count * 4) {
        // Compact RGBA in place. Each source pixel is consumed before its
        // storage can be overwritten; RGB frames require no conversion/copy.
        auto pixels = pending.bytes.data();
        for (size_t i = 0; i < count; ++i) {
            const uint8_t r = pixels[i * 4], g = pixels[i * 4 + 1], b = pixels[i * 4 + 2];
            pixels[i * 3] = r; pixels[i * 3 + 1] = g; pixels[i * 3 + 2] = b;
        }
    }
    std::lock_guard lock(mutex);
    if (pending.generation != generation) return; // A reset invalidates in-flight frames.
    if (++sequence == 0) ++sequence;
    pending.frame->id = sequence;
    latest = std::move(pending.frame);
}
void Display::Reset() { std::lock_guard lock(mutex); ++generation; latest = blank; }
std::shared_ptr<const PixelFrame> Display::Snapshot() const { std::lock_guard lock(mutex); return latest; }
}
