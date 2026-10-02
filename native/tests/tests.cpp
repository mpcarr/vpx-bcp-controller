#include "BcpClient.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <filesystem>
#include <fstream>
using namespace std::chrono_literals;
void Check(bool condition, const char* text) { if (!condition) throw std::runtime_error(text); }
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--path") {
            std::ofstream(argv[2]) << "launched";
        } else if (argc == 2 && std::string(argv[1]) == "--launch-test") {
            auto marker = std::filesystem::current_path() / "launch test & literal.txt";
            std::filesystem::remove(marker);
            auto launchPath = std::filesystem::absolute(argv[0]);
#ifdef _WIN32
            launchPath.replace_extension(); // Exercise basename lookup through the real launcher.
#endif
            bcp::Launch(launchPath.string(), marker.string());
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            while (std::chrono::steady_clock::now() < deadline && !std::filesystem::exists(marker)) std::this_thread::sleep_for(10ms);
            std::ifstream stream(marker); std::string result; stream >> result; stream.close();
            Check(result == "launched", "launch argument boundaries");
            std::filesystem::remove(marker);
            bool failed = false;
            try { bcp::Launch("nonexistent-bcp-program-for-test"); } catch (...) { failed = true; }
            Check(failed, "launch failure reporting");
        } else if (argc == 2 && std::string(argv[1]) == "--resolve-test") {
            const auto root = std::filesystem::current_path() / "resolver-test-fixtures";
            Check(std::filesystem::create_directory(root), "fixture directory must not already exist");
            struct Cleanup { std::filesystem::path root; ~Cleanup() { std::error_code ec; std::filesystem::remove_all(root, ec); } } cleanup{root};
            auto copy = [&](const std::filesystem::path& target) {
                std::filesystem::copy_file(std::filesystem::absolute(argv[0]), target);
                std::filesystem::permissions(target, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add);
            };
            auto base = root / "Dark Chaos_gmc";
            std::filesystem::path expected;
#ifdef _WIN32
            expected = base; expected += ".exe";
            copy(expected); copy(base);
            Check(bcp::ResolveExecutable(base.string()) == expected, "prefer Windows export over extensionless file");
            auto link = root / "Shortcut.lnk"; std::ofstream(link) << "fixture";
            Check(bcp::ResolveExecutable((root / "Shortcut").string()) == link, "shortcut fallback");
#elif defined(__APPLE__)
            auto bundle = base; bundle += ".app";
            std::filesystem::create_directories(bundle / "Contents/MacOS");
            std::ofstream(bundle / "Contents/Info.plist") <<
                "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
                "<key>CFBundleExecutable</key><string>Different Binary Name</string>"
                "<key>CFBundlePackageType</key><string>APPL</string></dict></plist>";
            expected = bundle / "Contents/MacOS/Different Binary Name";
            copy(expected);
            Check(bcp::ResolveExecutable(base.string()) == expected, "resolve macOS bundle metadata");
            Check(bcp::ResolveExecutable(bundle.string()) == expected, "explicit macOS bundle");
#else
            expected = base; copy(expected);
            Check(bcp::ResolveExecutable(base.string()) == expected, "Linux extensionless binary");
#if defined(__aarch64__) || defined(__x86_64__)
            auto suffixed = root / "Architecture_gmc";
            auto binary = suffixed;
#if defined(__aarch64__)
            binary += ".arm64";
#else
            binary += ".x86_64";
#endif
            copy(binary);
            Check(bcp::ResolveExecutable(suffixed.string()) == binary, "Linux architecture suffix");
#endif
#endif
            Check(bcp::ResolveExecutable(expected.string()) == expected, "explicit executable path");
            bool failed = false;
            try { bcp::ResolveExecutable((root / "Missing").string()); } catch (const std::exception& e) { failed = std::string(e.what()).find("Tried:") != std::string::npos; }
            Check(failed, "missing export diagnostics");
        } else if (argc == 3) {
            bcp::Client a, b;
            a.Connect(std::stoi(argv[1])); b.Connect(std::stoi(argv[2]));
            a.Send("trigger?name=first"); a.Send("trigger?name=second"); b.Send("trigger?name=monitor");
            const auto deadline = std::chrono::steady_clock::now() + 5s;
            std::vector<bcp::Message> aa, bb;
            while (std::chrono::steady_clock::now() < deadline && (aa.size() < 3 || bb.size() < 2)) {
                for (auto& m : a.Drain()) aa.push_back(std::move(m));
                for (auto& m : b.Drain()) bb.push_back(std::move(m));
                std::this_thread::sleep_for(5ms);
            }
            Check(aa.size() == 3 && bb.size() == 2, "independent receive queues");
            Check(aa[0].command == "hello" && aa[1].GetValue("name") == "caf\xc3\xa9", "fragmented UTF-8");
            Check(aa[2].GetValue("json").size() > 100000, "large JSON frame");
            Check(bb[1].GetValue("debug") == "bool:true", "literal bool value");
            auto start = std::chrono::steady_clock::now(); a.Disconnect();
            Check(std::chrono::steady_clock::now() - start < 1s, "bounded disconnect");
            Check(b.Connected(), "disconnect must not affect other instance");
            b.Send("trigger?name=still_connected");
            std::this_thread::sleep_for(100ms); b.Disconnect();
            a.Connect(std::stoi(argv[1])); // cancelling a pending connection/retry must also be bounded
            start = std::chrono::steady_clock::now(); a.Disconnect();
            Check(std::chrono::steady_clock::now() - start < 1s, "cancel connect");
        } else {
            auto m = bcp::Message::Parse("TRIGGER?name=a%26b%3Dc&debug=bool:true&x=a=b&dup=1&dup=2\r\n");
            Check(m.command == "trigger", "command case");
            Check(m.GetValue("NAME") == "a&b=c", "decode after splitting");
            Check(m.GetValue("x") == "a=b" && m.GetValue("dup") == "2", "equals and duplicates");
            Check(m.GetValue("missing").empty(), "missing key");
            Check(m.GetValue("RawMessage") == m.raw, "raw message");
            m = bcp::Message::Parse("trigger?json={\"text\":\"a+b&c=d%20\"}");
            Check(m.GetValue("json") == "{\"text\":\"a+b&c=d%20\"}", "literal JSON");
            m = bcp::Message::Parse("trigger?json=%7B%22x%22%3A1%7D");
            Check(m.GetValue("json") == "{\"x\":1}", "encoded JSON");
            bcp::Client client;
            bool failed = false; try { client.Connect(0); } catch (...) { failed = true; }
            Check(failed, "invalid port");
            failed = false; try { client.Send("hello"); } catch (...) { failed = true; }
            Check(failed, "disconnected send");
        }
        std::cout << "OK\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
