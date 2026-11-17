#include "StreamDecoder.h"
#include <charconv>
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace bcp {
void StreamDecoder::Feed(std::string_view bytes, const Receiver& receive, const PrepareBinary& prepare) {
    while (!bytes.empty() || (readingBody && bodyReceived == bodySize)) {
        if (readingBody) {
            const auto count = std::min(bytes.size(), bodySize - bodyReceived);
            if (count) {
                if (!target) std::memcpy(header.binary.data() + bodyReceived, bytes.data(), count);
                else if (!target->bytes.empty()) std::memcpy(target->bytes.data() + bodyReceived, bytes.data(), count);
                bytes.remove_prefix(count);
                bodyReceived += count;
            }
            if (bodyReceived < bodySize) return;
            readingBody = false;
            if (target) {
                auto completed = std::move(*target);
                target.reset();
                if (completed.complete) completed.complete();
                header = {};
            } else receive(std::move(header));
            continue;
        }
        const auto end = bytes.find('\n');
        const auto count = end == std::string_view::npos ? bytes.size() : end;
        if (count > Limit - buffer.size()) throw std::runtime_error("BCP line exceeds 16 MiB");
        buffer.append(bytes.data(), count);
        bytes.remove_prefix(count);
        if (end == std::string_view::npos) return;
        bytes.remove_prefix(1);
        header = Message::Parse(std::move(buffer));
        buffer.clear();
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
        target = prepare ? prepare(header, bodySize) : std::nullopt;
        if (target && !target->bytes.empty() && target->bytes.size() != bodySize)
            throw std::runtime_error("Binary target size does not match payload");
        if (!target) header.binary.resize(bodySize);
        bodyReceived = 0;
        readingBody = true;
    }
}
}
