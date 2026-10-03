#include "BcpClient.h"
#include <filesystem>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <limits.h>
#endif
#endif

namespace bcp {
std::filesystem::path ResolveExecutable(const std::string& executable, const std::filesystem::path& baseDirectory) {
    if (executable.empty()) throw std::invalid_argument("Executable name is empty");
    const auto requested = std::filesystem::u8path(executable);
    const auto base = std::filesystem::absolute(requested.is_absolute() ? requested : baseDirectory / requested);
    std::vector<std::filesystem::path> candidates;
    auto append = [&](const char* suffix) { auto path = base; path += suffix; candidates.push_back(path); };
    // A basename selects a native export. Explicit filenames retain their meaning.
#ifdef _WIN32
    if (!base.has_extension()) append(".exe");
    candidates.push_back(base);
    append(".lnk");
    if (!base.has_extension()) append(".exe.lnk");
#elif defined(__APPLE__)
    if (!base.has_extension()) append(".app");
    candidates.push_back(base);
#else
    candidates.push_back(base);
    if (!base.has_extension()) {
#if defined(__aarch64__)
        append(".arm64"); append(".aarch64");
#elif defined(__x86_64__)
        append(".x86_64");
#endif
    }
#endif
    std::string tried;
    for (auto path : candidates) {
        if (!tried.empty()) tried += ", ";
        const auto utf8 = path.u8string(); tried.append(reinterpret_cast<const char*>(utf8.data()), utf8.size());
#ifdef __APPLE__
        if (path.extension() == ".app" && std::filesystem::is_directory(path)) {
            const auto name = path.string();
            CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                reinterpret_cast<const UInt8*>(name.data()), name.size(), true);
            CFBundleRef bundle = url ? CFBundleCreate(kCFAllocatorDefault, url) : nullptr;
            CFURLRef target = bundle ? CFBundleCopyExecutableURL(bundle) : nullptr;
            UInt8 filename[PATH_MAX];
            const bool found = target && CFURLGetFileSystemRepresentation(target, true, filename, sizeof(filename));
            if (target) CFRelease(target);
            if (bundle) CFRelease(bundle);
            if (url) CFRelease(url);
            if (!found) throw std::runtime_error("App bundle has no executable; check Contents/Info.plist: " + name);
            path = std::filesystem::path(reinterpret_cast<const char*>(filename));
        }
#endif
        if (!std::filesystem::is_regular_file(path)) continue;
#ifndef _WIN32
        if (access(path.c_str(), X_OK) != 0) throw std::runtime_error("Media controller is not executable (check chmod +x): " + path.string());
#endif
        return path;
    }
    throw std::runtime_error("Media controller executable not found. Tried: " + tried);
}
void Launch(const std::string& executable, const std::string& project, const std::filesystem::path& baseDirectory) {
    if (executable.empty()) return;
    const auto path = ResolveExecutable(executable, baseDirectory);
#ifdef _WIN32
    // Windows command-line quoting, including trailing backslashes before a quote.
    auto quote = [](const std::wstring& value) {
        std::wstring out = L"\""; size_t slashes = 0;
        for (auto c : value) {
            if (c == L'\\') { ++slashes; continue; }
            out.append(slashes * (c == L'"' ? 2 : 1), L'\\'); slashes = 0;
            if (c == L'"') out += L'\\';
            out += c;
        }
        out.append(slashes * 2, L'\\'); return out + L'"';
    };
    const auto args = project.empty() ? L"" : L"--path " + quote(std::filesystem::u8path(project).wstring());
    SHELLEXECUTEINFOW info{}; info.cbSize = sizeof(info); info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpFile = path.c_str(); info.lpParameters = args.c_str(); info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) throw std::runtime_error("Cannot launch media controller, Windows error " + std::to_string(GetLastError()));
    if (info.hProcess) CloseHandle(info.hProcess);
#else
    // Double-fork to avoid zombies or a reaper thread that outlives the plugin.
    // Only async-signal-safe calls are made in children before exec.
    const auto program = path.string();
    std::vector<char*> args{const_cast<char*>(program.c_str())};
    if (!project.empty()) { args.push_back(const_cast<char*>("--path")); args.push_back(const_cast<char*>(project.c_str())); }
    args.push_back(nullptr);
    int pipes[2];
    if (pipe(pipes) != 0) throw std::runtime_error("Cannot create launch pipe");
    fcntl(pipes[0], F_SETFD, FD_CLOEXEC); fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
    pid_t child = fork();
    if (child == 0) {
        close(pipes[0]);
        pid_t detached = fork();
        if (detached > 0) _exit(0);
        if (detached == 0) { setsid(); execv(program.c_str(), args.data()); }
        int failure = errno; (void)write(pipes[1], &failure, sizeof(failure)); _exit(127);
    }
    close(pipes[1]);
    if (child < 0) { close(pipes[0]); throw std::runtime_error("Cannot fork media controller"); }
    int status; while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    int failure = 0; ssize_t n;
    do { n = read(pipes[0], &failure, sizeof(failure)); } while (n < 0 && errno == EINTR);
    close(pipes[0]);
    if (n > 0) throw std::runtime_error(std::string("Cannot launch media controller: ") + strerror(failure));
#endif
}
}
