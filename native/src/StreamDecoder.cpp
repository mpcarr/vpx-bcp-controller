#include "StreamDecoder.h"
#include <charconv>
#include <stdexcept>

namespace bcp {
void StreamDecoder::Feed(std::string_view bytes, const Receiver& receive) {
    buffer.append(bytes);
    for (;;) {
        if (readingBody) {
            if (buffer.size() < bodySize) return;
            header.binary.assign(buffer.data(), bodySize);
            buffer.erase(0, bodySize);
            readingBody = false;
            receive(std::move(header));
            continue;
        }
        const auto end = buffer.find('\n');
        if (end == std::string::npos) {
            if (buffer.size() > Limit) throw std::runtime_error("BCP line exceeds 16 MiB");
            return;
        }
        if (end > Limit) throw std::runtime_error("BCP line exceeds 16 MiB");
        header = Message::Parse(buffer.substr(0, end));
        buffer.erase(0, end + 1);
        auto length = header.parameters.find("bytes");
        if (length == header.parameters.end()) {
            receive(std::move(header));
            continue;
        }
        const auto& value = length->second;
        bodySize = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), bodySize);
        if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size() || bodySize > Limit)
            throw std::runtime_error("Invalid BCP binary length (maximum 16 MiB)");
        readingBody = true;
    }
}
}
