#pragma once
#include <map>
#include <string>

namespace bcp {
struct Message {
    std::string command, raw;
    std::map<std::string, std::string> parameters;
    static Message Parse(std::string line);
    std::string GetValue(std::string key) const;
};
}
