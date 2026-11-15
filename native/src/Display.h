#pragma once
#include <cstdint>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

namespace bcp {
struct PixelFrame {
    unsigned int id = 0;
    std::vector<uint8_t> pixels;
};

class Display {
public:
    Display(unsigned int width, unsigned int height);
    const unsigned int width, height;
    void Update(std::string_view pixels);
    void Reset();
    std::shared_ptr<const PixelFrame> Snapshot() const;
private:
    mutable std::mutex mutex;
    std::shared_ptr<const PixelFrame> blank, latest;
    unsigned int sequence = 0;
};
}
