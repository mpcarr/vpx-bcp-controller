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
#endif

namespace bcp {
void Launch(const std::string& executable, const std::string& project) {
    if (executable.empty()) return;
    auto path = std::filesystem::absolute(std::filesystem::u8path(executable));
#ifdef _WIN32
    if (!std::filesystem::exists(path) && std::filesystem::exists(path.wstring() + L".lnk")) path += L".lnk";
#endif
    if (!std::filesystem::exists(path)) throw std::runtime_error("Media controller executable does not exist: " + executable);
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
