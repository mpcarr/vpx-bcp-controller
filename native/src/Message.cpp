#include "Message.h"
#include <algorithm>
#include <cctype>

namespace bcp {
static std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
static int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static std::string Decode(const std::string& s) {
    std::string result;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && Hex(s[i+1]) >= 0 && Hex(s[i+2]) >= 0) {
            result += static_cast<char>(Hex(s[i+1]) * 16 + Hex(s[i+2])); i += 2;
        } else result += s[i] == '+' ? ' ' : s[i];
    }
    return result;
}
Message Message::Parse(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    Message m;
    m.raw = line;
    const auto query = line.find('?');
    m.command = Lower(line.substr(0, query));
    if (query == std::string::npos) return m;
    const auto payload = line.substr(query + 1);
    // JSON is a complete payload, not a query string. Preserve literal +, &, = and %.
    if (Lower(payload.substr(0, 5)) == "json=") {
        auto json = payload.substr(5);
        const auto first = json.find_first_not_of(" \t");
        m.parameters["json"] = first != std::string::npos && (json[first] == '{' || json[first] == '[') ? json : Decode(json);
        return m;
    }
    size_t pos = 0;
    while (pos < payload.size()) {
        auto end = payload.find('&', pos);
        auto pair = payload.substr(pos, end == std::string::npos ? end : end - pos);
        auto eq = pair.find('=');
        m.parameters[Lower(Decode(pair.substr(0, eq)))] = eq == std::string::npos ? "" : Decode(pair.substr(eq + 1));
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return m;
}
std::string Message::GetValue(std::string key) const {
    key = Lower(key);
    if (key == "rawmessage") return raw;
    auto it = parameters.find(key);
    return it == parameters.end() ? "" : it->second;
}
}
