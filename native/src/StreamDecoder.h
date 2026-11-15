#pragma once
#include "Message.h"
#include <functional>
#include <string_view>

namespace bcp {
// BCP binary messages are a newline-terminated header followed by exactly
// bytes=N octets. Newlines inside those octets have no framing significance.
class StreamDecoder {
public:
    using Receiver = std::function<void(Message&&)>;
    static constexpr size_t Limit = 16 * 1024 * 1024;
    void Feed(std::string_view bytes, const Receiver& receive);
private:
    std::string buffer;
    Message header;
    size_t bodySize = 0;
    bool readingBody = false;
};
}
