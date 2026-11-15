#include "Display.h"
#include "StreamDecoder.h"
#include <stdexcept>

namespace bcp {
Display::Display(unsigned int w, unsigned int h) : width(w), height(h) {
    if (!w || !h || w > 4096 || h > 4096 || static_cast<uint64_t>(w) * h * 4 > StreamDecoder::Limit)
        throw std::invalid_argument("Display dimensions must be 1..4096 and fit within a 16 MiB RGBA frame");
    auto frame = std::make_shared<PixelFrame>();
    frame->pixels.resize(static_cast<size_t>(w) * h * 3);
    blank = latest = std::move(frame);
}
void Display::Update(std::string_view pixels) {
    const size_t count = static_cast<size_t>(width) * height;
    if (pixels.size() != count * 3 && pixels.size() != count * 4)
        throw std::runtime_error("RGB DMD frame length does not match registered dimensions (expected RGB8 or RGBA8)");
    auto frame = std::make_shared<PixelFrame>();
    frame->pixels.resize(count * 3);
    const size_t stride = pixels.size() / count;
    for (size_t i = 0; i < count; ++i)
        for (size_t c = 0; c < 3; ++c) frame->pixels[i * 3 + c] = static_cast<uint8_t>(pixels[i * stride + c]);
    std::lock_guard lock(mutex);
    if (++sequence == 0) ++sequence;
    frame->id = sequence;
    latest = std::move(frame);
}
void Display::Reset() { std::lock_guard lock(mutex); latest = blank; }
std::shared_ptr<const PixelFrame> Display::Snapshot() const { std::lock_guard lock(mutex); return latest; }
}
