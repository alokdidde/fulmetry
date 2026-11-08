#include "Subprocess.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
extern char** environ;
#endif

namespace fulmetry::net {

#ifdef _WIN32

namespace {
std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
// One argument quoted the way CommandLineToArgvW reads it back.
std::wstring quote(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') out.append(slashes * 2 + 1, L'\\');
        else out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(c);
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}
}  // namespace

Result<std::unique_ptr<Subprocess>> Subprocess::start(const std::vector<std::string>& argv) {
    if (argv.empty()) return Error{"invalid_argument", "empty command"};
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE inRead, inWrite, outRead, outWrite;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0)) return Error{"spawn_failed", "CreatePipe"};
    if (!CreatePipe(&outRead, &outWrite, &sa, 0)) {
        CloseHandle(inRead);
        CloseHandle(inWrite);
        return Error{"spawn_failed", "CreatePipe"};
    }
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    std::wstring cmd;
    for (const auto& a : argv) cmd += (cmd.empty() ? L"" : L" ") + quote(widen(a));
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (!ok) {
        CloseHandle(inWrite);
        CloseHandle(outRead);
        return Error{"spawn_failed", "could not start " + argv[0]};
    }
    CloseHandle(pi.hThread);
    std::unique_ptr<Subprocess> p(new Subprocess());
    p->process_ = pi.hProcess;
    p->in_ = inWrite;
    p->out_ = outRead;
    return p;
}

Subprocess::~Subprocess() {
    closeInput();
    if (process_) {
        if (WaitForSingleObject(process_, 5000) == WAIT_TIMEOUT) TerminateProcess(process_, 1);
        CloseHandle(process_);
    }
    if (out_) CloseHandle(out_);
}

bool Subprocess::writeLine(const std::string& line) {
    std::lock_guard lock(writeMu_);
    if (!in_) return false;
    const std::string data = line + "\n";
    DWORD written = 0;
    return WriteFile(in_, data.data(), DWORD(data.size()), &written, nullptr) && written == data.size();
}

bool Subprocess::readLine(std::string& line) {
    for (;;) {
        if (auto nl = buffer_.find('\n'); nl != std::string::npos) {
            line = buffer_.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            buffer_.erase(0, nl + 1);
            return true;
        }
        char chunk[4096];
        DWORD n = 0;
        if (!ReadFile(out_, chunk, sizeof chunk, &n, nullptr) || n == 0) return false;
        buffer_.append(chunk, n);
    }
}

void Subprocess::closeInput() {
    std::lock_guard lock(writeMu_);
    if (in_) CloseHandle(in_);
    in_ = nullptr;
}

#else

Result<std::unique_ptr<Subprocess>> Subprocess::start(const std::vector<std::string>& argv) {
    if (argv.empty()) return Error{"invalid_argument", "empty command"};
    int in[2], out[2];
    if (pipe(in) != 0) return Error{"spawn_failed", std::strerror(errno)};
    if (pipe(out) != 0) {
        close(in[0]);
        close(in[1]);
        return Error{"spawn_failed", std::strerror(errno)};
    }
    fcntl(in[1], F_SETFD, FD_CLOEXEC);
    fcntl(out[0], F_SETFD, FD_CLOEXEC);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    posix_spawn_file_actions_addclose(&actions, in[0]);
    posix_spawn_file_actions_addclose(&actions, out[1]);
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    pid_t pid = -1;
    const int rc = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(in[0]);
    close(out[1]);
    if (rc != 0) {
        close(in[1]);
        close(out[0]);
        return Error{"spawn_failed", "could not start " + argv[0] + ": " + std::strerror(rc)};
    }
    // A bridge that dies must not kill the app with SIGPIPE on the next write.
    std::signal(SIGPIPE, SIG_IGN);
    std::unique_ptr<Subprocess> p(new Subprocess());
    p->pid_ = pid;
    p->in_ = in[1];
    p->out_ = out[0];
    return p;
}

Subprocess::~Subprocess() {
    closeInput();
    if (pid_ > 0) {
        // The bridge exits at end of input; give it a moment to stop the server it started.
        for (int i = 0; i < 50; ++i) {
            if (waitpid(pid_, nullptr, WNOHANG) == pid_) { pid_ = -1; break; }
            usleep(100 * 1000);
        }
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            waitpid(pid_, nullptr, 0);
        }
    }
    if (out_ >= 0) close(out_);
}

bool Subprocess::writeLine(const std::string& line) {
    std::lock_guard lock(writeMu_);
    if (in_ < 0) return false;
    const std::string data = line + "\n";
    size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = write(in_, data.data() + off, data.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        off += size_t(n);
    }
    return true;
}

bool Subprocess::readLine(std::string& line) {
    for (;;) {
        if (auto nl = buffer_.find('\n'); nl != std::string::npos) {
            line = buffer_.substr(0, nl);
            buffer_.erase(0, nl + 1);
            return true;
        }
        char chunk[4096];
        const ssize_t n = read(out_, chunk, sizeof chunk);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        buffer_.append(chunk, size_t(n));
    }
}

void Subprocess::closeInput() {
    std::lock_guard lock(writeMu_);
    if (in_ >= 0) close(in_);
    in_ = -1;
}

#endif

}  // namespace fulmetry::net
