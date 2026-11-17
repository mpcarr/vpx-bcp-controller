#pragma once
#include "Message.h"
#include <functional>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace bcp {
// BCP binary messages are a newline-terminated header followed by exactly
// bytes=N octets. Newlines inside those octets have no framing significance.
class StreamDecoder {
public:
    using Receiver = std::function<void(Message&&)>;
    struct BinaryTarget {
        // Empty storage discards this body; completion owns any target storage
        // until all advertised bytes have arrived. No callback on truncation.
        std::span<uint8_t> bytes;
        std::function<void()> complete;
    };
    using PrepareBinary = std::function<std::optional<BinaryTarget>(const Message&, size_t)>;
    static constexpr size_t Limit = 16 * 1024 * 1024;
    void Feed(std::string_view bytes, const Receiver& receive, const PrepareBinary& prepare = {});
private:
    std::string buffer;
    Message header;
    size_t bodySize = 0, bodyReceived = 0;
    bool readingBody = false;
    std::optional<BinaryTarget> target;
};
}
