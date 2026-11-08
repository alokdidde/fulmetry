#include "fulmetry/net/CliSignIn.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <optional>
#include <utility>

#include <nlohmann/json.hpp>

#if !defined(_WIN32)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fulmetry::net {

namespace cli {

namespace {
// Drops ANSI CSI sequences and OSC sequences (OSC 8 hyperlinks wrap the URL in a terminal).
std::string stripEscapes(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\x1b') {
            if (s[i] != '\r') out += s[i];
            continue;
        }
        if (i + 1 < s.size() && s[i + 1] == '[') {
            i += 2;
            while (i < s.size() && !(s[i] >= '@' && s[i] <= '~')) ++i;
        } else if (i + 1 < s.size() && s[i + 1] == ']') {
            i += 2;
            while (i < s.size() && s[i] != '\x07' && !(s[i] == '\x1b' && i + 1 < s.size() && s[i + 1] == '\\')) ++i;
            if (i < s.size() && s[i] == '\x1b') ++i;
        }
    }
    return out;
}
}  // namespace

std::string firstUrl(const std::string& output) {
    const std::string s = stripEscapes(output);
    const auto at = s.find("https://");
    if (at == std::string::npos) return {};
    auto end = at;
    while (end < s.size() && static_cast<unsigned char>(s[end]) > ' ' && s[end] != '"' && s[end] != '\'') ++end;
    // a URL is complete only once the line it is on has ended
    if (end == s.size()) return {};
    return s.substr(at, end - at);
}

std::string lastLine(const std::string& output) {
    const std::string s = stripEscapes(output);
    size_t end = s.size();
    while (end > 0) {
        size_t start = s.rfind('\n', end - 1);
        start = start == std::string::npos ? 0 : start + 1;
        std::string line = s.substr(start, end - start);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\n' || line.back() == '\t')) line.pop_back();
        if (!line.empty()) return line;
        if (start == 0) break;
        end = start - 1;
    }
    return {};
}

}  // namespace cli

namespace {

namespace fs = std::filesystem;

#if !defined(_WIN32)
// A child whose stdout and stderr come back on one non-blocking pipe, in its own process group.
struct Proc {
    pid_t pid = -1;
    int out = -1, in = -1;
    std::string output;
    bool running = false;
    int exitCode = -1;

    static std::unique_ptr<Proc> start(const std::vector<std::string>& argv, const std::string& path) {
        int o[2], i[2];
        if (pipe(o) != 0) return nullptr;
        if (pipe(i) != 0) {
            close(o[0]);
            close(o[1]);
            return nullptr;
        }
        std::vector<char*> args;
        for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        const pid_t pid = fork();
        if (pid < 0) {
            for (int fd : {o[0], o[1], i[0], i[1]}) close(fd);
            return nullptr;
        }
        if (pid == 0) {
            setsid();
            dup2(i[0], 0);
            dup2(o[1], 1);
            dup2(o[1], 2);
            for (int fd : {o[0], o[1], i[0], i[1]}) close(fd);
            if (!path.empty()) setenv("PATH", path.c_str(), 1);
            execv(args[0], args.data());
            _exit(127);
        }
        close(o[1]);
        close(i[0]);
        fcntl(o[0], F_SETFL, fcntl(o[0], F_GETFL) | O_NONBLOCK);
        fcntl(o[0], F_SETFD, FD_CLOEXEC);
        fcntl(i[1], F_SETFD, FD_CLOEXEC);
        auto p = std::make_unique<Proc>();
        p->pid = pid;
        p->out = o[0];
        p->in = i[1];
        p->running = true;
        return p;
    }

    // Reads what is there and reaps the child once it has exited. True when anything changed.
    bool pump() {
        bool changed = false;
        char buf[4096];
        for (;;) {
            const ssize_t n = read(out, buf, sizeof buf);
            if (n > 0) {
                output.append(buf, static_cast<size_t>(n));
                changed = true;
                continue;
            }
            if (n < 0 && errno == EINTR) continue;
            break;
        }
        if (running) {
            int status = 0;
            if (waitpid(pid, &status, WNOHANG) == pid) {
                // whatever the child wrote before exiting is still in the pipe
                for (ssize_t n; (n = read(out, buf, sizeof buf)) > 0;) output.append(buf, static_cast<size_t>(n));
                running = false;
                exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
                changed = true;
            }
        }
        return changed;
    }

    bool write(const std::string& s) {
        if (in < 0) return false;
        size_t done = 0;
        while (done < s.size()) {
            const ssize_t n = ::write(in, s.data() + done, s.size() - done);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            done += static_cast<size_t>(n);
        }
        return true;
    }

    void kill() {
        if (running) ::kill(-pid, SIGTERM);
    }

    ~Proc() {
        if (running) {
            ::kill(-pid, SIGKILL);
            waitpid(pid, nullptr, 0);
        }
        if (out >= 0) close(out);
        if (in >= 0) close(in);
    }
};
#endif

bool executable(const fs::path& p) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
#if defined(_WIN32)
    return true;
#else
    return access(p.c_str(), X_OK) == 0;
#endif
}

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> dirs;
    size_t start = 0;
    while (start <= path.size()) {
        auto end = path.find(':', start);
        if (end == std::string::npos) end = path.size();
        if (end > start) dirs.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    return dirs;
}

std::string findIn(const std::string& path, const std::string& name) {
    for (const auto& d : splitPath(path))
        if (executable(fs::path(d) / name)) return (fs::path(d) / name).string();
    return {};
}

std::string homeDir() {
    const char* h = std::getenv("HOME");
    return h ? h : "";
}

// The usual install folders, which a Finder-launched app's PATH leaves out.
std::string withInstallDirs(std::string path) {
    const std::string home = homeDir();
    std::vector<std::string> extra = {"/opt/homebrew/bin", "/usr/local/bin"};
    if (!home.empty())
        for (const char* d : {"/.local/bin", "/.claude/local", "/.npm-global/bin", "/.bun/bin", "/.volta/bin"})
            extra.push_back(home + d);
    const auto have = splitPath(path);
    for (const auto& d : extra)
        if (std::find(have.begin(), have.end(), d) == have.end()) path += (path.empty() ? "" : ":") + d;
    return path;
}

struct Cli {
    std::string id, binary, name, docsUrl;
    std::string exe;  // resolved; empty when not installed
    enum class Phase { Locating, Probing, Installing, SigningIn, SigningOut, Idle } phase = Phase::Locating;
    bool signedIn = false;
    bool disabled = false;
    std::string account;
    std::string failure;  // why the last install or sign-in did not work
    std::string url;      // the sign-in page, once the CLI printed it
    bool codeSent = false;
    bool cancelled = false;
    bool signInNext = false;  // asked for while it was still being checked
    std::optional<bool> lastReady;
#if !defined(_WIN32)
    std::unique_ptr<Proc> proc;
#endif
};

}  // namespace

struct CliSignInClient::Impl {
    std::unique_ptr<IAgentClient> inner;
    CliSignInOptions options;
    std::string path;  // PATH the CLIs are found on and run with
    bool pathReady = false;
    std::vector<Cli> clis;
    EventBus innerBus;
    EventBus* out = nullptr;
    bool changed = true;
#if !defined(_WIN32)
    std::unique_ptr<Proc> shell;  // reads the login shell's PATH
#endif

    Cli* find(const std::string& id) {
        for (auto& c : clis)
            if (c.id == id) return &c;
        return nullptr;
    }

    Harness overlay(Harness h, const Cli& c) const {
        h.canSignIn = true;
        h.docsUrl = c.docsUrl;
        h.setup = {};
        h.message.clear();
        h.account = c.signedIn ? c.account : "";
        using S = HarnessSetup::Step;
        switch (c.phase) {
            case Cli::Phase::Locating:
            case Cli::Phase::Probing:
            case Cli::Phase::SigningOut:
                h.status = HarnessStatus::Checking;
                h.setup.step = S::Verifying;
                h.setup.message = c.phase == Cli::Phase::SigningOut ? "Signing out" : "Checking the sign-in";
                break;
            case Cli::Phase::Installing:
                h.status = HarnessStatus::Checking;
                h.setup.step = S::Installing;
                h.setup.message = "Installing " + c.name;
                break;
            case Cli::Phase::SigningIn:
                h.status = HarnessStatus::Checking;
                h.setup.step = S::SigningIn;
                if (!c.url.empty()) {
                    h.setup.prompt = HarnessSetup::Prompt::Browser;
                    h.setup.url = c.url;
                    if (c.id == "claudeAgent") {
                        h.setup.fields = {{"code", "Code from the page, if it shows one", false}};
                        h.setup.message = c.codeSent ? "Checking the code\xE2\x80\xA6"
                                                     : "Your browser opened Claude's sign-in page. Approve it there "
                                                       "and this finishes by itself. If the page shows a code instead, "
                                                       "paste it here.";
                    } else {
                        h.setup.message = "Your browser opened the OpenAI sign-in page. Sign in with your ChatGPT "
                                          "account there and this finishes by itself.";
                    }
                }
                break;
            case Cli::Phase::Idle:
                if (c.exe.empty()) {
                    h.status = HarnessStatus::NeedsInstall;
                    h.canInstall = !installCommand(c).empty();
                    h.message = c.name + " is not installed on this computer.";
                } else {
                    h.status = c.signedIn ? HarnessStatus::Ready : HarnessStatus::NeedsSignIn;
                }
                if (!c.failure.empty()) {
                    h.setup.step = S::Failed;
                    h.setup.message = c.failure;
                }
                break;
        }
        if (c.disabled) h.status = HarnessStatus::Disabled;
        return h;
    }

    std::vector<std::string> installCommand(const Cli& c) const {
        if (c.id == "claudeAgent") return {"/bin/sh", "-c", "curl -fsSL https://claude.ai/install.sh | bash"};
        if (const auto npm = findIn(path, "npm"); !npm.empty()) return {npm, "install", "-g", "@openai/codex"};
        if (const auto brew = findIn(path, "brew"); !brew.empty()) return {brew, "install", "codex"};
        return {};
    }

    std::vector<Harness> merged() const {
        auto list = inner->harnesses();
        for (auto& h : list)
            for (const auto& c : clis)
                if (h.id == c.id) h = overlay(std::move(h), c);
        return list;
    }

#if !defined(_WIN32)
    bool run(Cli& c, Cli::Phase phase, std::vector<std::string> argv) {
        c.proc = Proc::start(argv, path);
        if (!c.proc) {
            c.failure = "Could not start " + c.binary + ".";
            c.phase = Cli::Phase::Idle;
            return false;
        }
        c.phase = phase;
        changed = true;
        return true;
    }

    void probe(Cli& c) {
        c.exe = findIn(path, c.binary);
        if (c.exe.empty()) {
            c.signedIn = false;
            c.signInNext = false;
            c.phase = Cli::Phase::Idle;
            changed = true;
            return;
        }
        if (c.id == "claudeAgent") run(c, Cli::Phase::Probing, {c.exe, "auth", "status", "--json"});
        else run(c, Cli::Phase::Probing, {c.exe, "login", "status"});
    }

    Status startSignIn(Cli& c) {
        c.signInNext = false;
        if (c.exe.empty()) return Error{"not_installed", c.name + " is not installed"};
        c.failure.clear();
        c.url.clear();
        c.codeSent = false;
        // Both open the browser themselves and finish through a callback on localhost.
        std::vector<std::string> argv = c.id == "claudeAgent" ? std::vector<std::string>{c.exe, "auth", "login"}
                                                              : std::vector<std::string>{c.exe, "login"};
        if (!run(c, Cli::Phase::SigningIn, std::move(argv))) return Error{"spawn_failed", c.failure};
        return Ok{};
    }

    void finished(Cli& c) {
        const Proc& p = *c.proc;
        const auto phase = c.phase;
        if (phase == Cli::Phase::Probing) {
            c.signedIn = p.exitCode == 0;
            c.account.clear();
            if (c.id == "claudeAgent") {
                // claude auth status --json: {"loggedIn": bool, "email"?: ..., "authMethod": ...}
                const auto j = nlohmann::json::parse(p.output.substr(std::min(p.output.find('{'), p.output.size())),
                                                     nullptr, false);
                if (j.is_object()) {
                    c.signedIn = j.value("loggedIn", c.signedIn);
                    for (const char* k : {"email", "emailAddress", "account"})
                        if (j.contains(k) && j[k].is_string()) {
                            c.account = j[k].get<std::string>();
                            break;
                        }
                    if (c.account.empty() && j.value("authMethod", "") == "api_key") c.account = "API key";
                }
            } else if (c.signedIn) {
                const auto line = cli::lastLine(p.output);
                c.account = line.find("ChatGPT") != std::string::npos ? "ChatGPT"
                            : line.find("API key") != std::string::npos ? "API key"
                                                                        : "";
            }
            c.phase = Cli::Phase::Idle;
            if (c.signInNext) {
                c.proc.reset();
                startSignIn(c);
                changed = true;
                return;
            }
        } else if (phase == Cli::Phase::Installing || phase == Cli::Phase::SigningIn) {
            if (p.exitCode != 0 && !c.cancelled) {
                const auto last = cli::lastLine(p.output);
                c.failure = (phase == Cli::Phase::Installing ? "Installing " + c.name + " did not work"
                                                             : "Signing in did not finish") +
                            (last.empty() ? "." : ": " + last);
            }
            c.url.clear();
            c.codeSent = false;
            c.cancelled = false;
            c.proc.reset();
            probe(c);
            return;
        } else if (phase == Cli::Phase::SigningOut) {
            c.proc.reset();
            probe(c);
            return;
        }
        c.proc.reset();
        changed = true;
    }

    void step() {
        if (!pathReady) {
            if (!shell) {
                const char* sh = std::getenv("SHELL");
                const std::string shellPath = sh && *sh ? sh : "/bin/zsh";
                if (executable(shellPath))
                    shell = Proc::start({shellPath, "-ilc", "printf '\\n__FULMETRY_PATH__%s__\\n' \"$PATH\""}, "");
                if (!shell) {
                    path = withInstallDirs(std::getenv("PATH") ? std::getenv("PATH") : "");
                    pathReady = true;
                }
            }
            if (shell) {
                shell->pump();
                if (shell->running) return;
                const auto& o = shell->output;
                const auto a = o.rfind("__FULMETRY_PATH__");
                std::string login;
                if (a != std::string::npos) {
                    const auto b = o.find("__", a + 17);
                    if (b != std::string::npos) login = o.substr(a + 17, b - a - 17);
                }
                const char* env = std::getenv("PATH");
                path = withInstallDirs(login + (env && *env ? (login.empty() ? "" : ":") + std::string(env) : ""));
                shell.reset();
                pathReady = true;
            }
            for (auto& c : clis) probe(c);
            return;
        }
        for (auto& c : clis) {
            if (!c.proc) continue;
            if (c.proc->pump()) changed = true;
            if (c.phase == Cli::Phase::SigningIn && c.url.empty()) {
                c.url = cli::firstUrl(c.proc->output);
                if (!c.url.empty()) changed = true;
            }
            if (!c.proc->running) finished(c);
        }
    }
#endif
};

CliSignInClient::CliSignInClient(std::unique_ptr<IAgentClient> inner, CliSignInOptions options)
    : impl_(std::make_unique<Impl>()) {
    impl_->inner = std::move(inner);
    impl_->options = std::move(options);
#if !defined(_WIN32)
    auto add = [&](const char* id, const char* binary, const char* name, const char* docs) {
        Cli c;
        c.id = id;
        c.binary = binary;
        c.name = name;
        c.docsUrl = docs;
        impl_->clis.push_back(std::move(c));
    };
    add("claudeAgent", "claude", "Claude Code", "https://docs.claude.com/en/docs/claude-code/setup");
    add("codex", "codex", "Codex", "https://developers.openai.com/codex/cli");
    if (!impl_->options.path.empty()) {
        impl_->path = impl_->options.path;
        impl_->pathReady = true;
        for (auto& c : impl_->clis) impl_->probe(c);
    }
#endif
    impl_->innerBus.subscribe([impl = impl_.get()](const Event& e) {
        if (!impl->out) return;
        if (std::holds_alternative<HarnessesUpdated>(e)) impl->out->publish(HarnessesUpdated{impl->merged()});
        else impl->out->publish(e);
    });
}

CliSignInClient::~CliSignInClient() = default;

Status CliSignInClient::connect(const std::string& url) { return impl_->inner->connect(url); }
bool CliSignInClient::connected() const { return impl_->inner->connected(); }
Result<AgentStarted> CliSignInClient::startAgentForComment(const Comment& comment, const std::optional<Image>& render,
                                                           const StartAgentOptions& options) {
    return impl_->inner->startAgentForComment(comment, render, options);
}
Result<AgentId> CliSignInClient::sendChat(const ChatRequest& request) { return impl_->inner->sendChat(request); }
Status CliSignInClient::reply(const AgentId& agent, const std::string& text) { return impl_->inner->reply(agent, text); }
Status CliSignInClient::respondToApproval(const AgentId& agent, const std::string& requestId, ApprovalDecision d) {
    return impl_->inner->respondToApproval(agent, requestId, d);
}
Status CliSignInClient::stopAgent(const AgentId& agent) { return impl_->inner->stopAgent(agent); }
std::vector<Harness> CliSignInClient::harnesses() const { return impl_->merged(); }

Status CliSignInClient::installHarness(const std::string& id) {
    Cli* c = impl_->find(id);
    if (!c) return impl_->inner->installHarness(id);
#if defined(_WIN32)
    return Error{"unsupported", "installing " + id + " from the app is not available on Windows yet"};
#else
    if (c->phase != Cli::Phase::Idle) return Error{"busy", c->name + " is busy"};
    if (!c->exe.empty()) return Ok{};
    auto cmd = impl_->installCommand(*c);
    if (cmd.empty()) return Error{"unsupported", "Install Node.js or Homebrew first, then " + c->name + " from its guide"};
    c->failure.clear();
    if (!impl_->run(*c, Cli::Phase::Installing, std::move(cmd))) return Error{"spawn_failed", c->failure};
    return Ok{};
#endif
}

Status CliSignInClient::signInHarness(const std::string& id) {
    Cli* c = impl_->find(id);
    if (!c) return impl_->inner->signInHarness(id);
#if defined(_WIN32)
    return Error{"unsupported", "signing in from the app is not available on Windows yet"};
#else
    if (c->phase == Cli::Phase::SigningIn) return Ok{};
    if (c->phase == Cli::Phase::Locating || c->phase == Cli::Phase::Probing) {
        c->signInNext = true;
        return Ok{};
    }
    if (c->phase != Cli::Phase::Idle) return Error{"busy", c->name + " is busy"};
    return impl_->startSignIn(*c);
#endif
}

Status CliSignInClient::answerSignIn(const std::string& id, const SignInAnswer& answer) {
    Cli* c = impl_->find(id);
    if (!c) return impl_->inner->answerSignIn(id, answer);
#if defined(_WIN32)
    return Error{"unsupported", "signing in from the app is not available on Windows yet"};
#else
    if (c->phase != Cli::Phase::SigningIn || !c->proc) return Error{"not_found", "no sign-in in progress"};
    if (auto it = answer.values.find("code"); it != answer.values.end() && !it->second.empty()) {
        if (!c->proc->write(it->second + "\n")) return Error{"closed", "the sign-in has already ended"};
        c->codeSent = true;
        impl_->changed = true;
    }
    return Ok{};
#endif
}

Status CliSignInClient::cancelHarnessSetup(const std::string& id) {
    Cli* c = impl_->find(id);
    if (!c) return impl_->inner->cancelHarnessSetup(id);
#if !defined(_WIN32)
    if (c->proc && (c->phase == Cli::Phase::SigningIn || c->phase == Cli::Phase::Installing)) {
        c->cancelled = true;
        c->proc->kill();
    }
#endif
    return Ok{};
}

Status CliSignInClient::signOutHarness(const std::string& id) {
    Cli* c = impl_->find(id);
    if (!c) return impl_->inner->signOutHarness(id);
#if defined(_WIN32)
    return Error{"unsupported", "signing out from the app is not available on Windows yet"};
#else
    if (c->phase != Cli::Phase::Idle) return Error{"busy", c->name + " is busy"};
    if (c->exe.empty()) return Error{"not_installed", c->name + " is not installed"};
    c->failure.clear();
    std::vector<std::string> argv = c->id == "claudeAgent" ? std::vector<std::string>{c->exe, "auth", "logout"}
                                                           : std::vector<std::string>{c->exe, "logout"};
    if (!impl_->run(*c, Cli::Phase::SigningOut, std::move(argv))) return Error{"spawn_failed", c->failure};
    return Ok{};
#endif
}

Status CliSignInClient::setHarnessEnabled(const std::string& id, bool enabled) {
    Cli* c = impl_->find(id);
    if (!c) return impl_->inner->setHarnessEnabled(id, enabled);
    c->disabled = !enabled;
    impl_->changed = true;
    return Ok{};
}

Result<std::vector<RegistryHarness>> CliSignInClient::searchHarnessRegistry(const std::string& query) {
    return impl_->inner->searchHarnessRegistry(query);
}
Result<std::string> CliSignInClient::addRegistryHarness(const std::string& registryId) {
    return impl_->inner->addRegistryHarness(registryId);
}
Status CliSignInClient::removeHarness(const std::string& id) { return impl_->inner->removeHarness(id); }

void CliSignInClient::poll(EventBus& bus) {
    impl_->out = &bus;
    impl_->inner->poll(impl_->innerBus);
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"  // GCC 13 misreads the inlined EventBus::drain
#endif
    impl_->innerBus.drain();
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
    impl_->out = nullptr;
#if !defined(_WIN32)
    impl_->step();
#endif
    if (!impl_->changed) return;
    impl_->changed = false;
    auto list = impl_->merged();
    for (auto& c : impl_->clis) {
        bool ready = false;
        for (const auto& h : list)
            if (h.id == c.id) ready = h.status == HarnessStatus::Ready;
        if (ready != c.lastReady && impl_->options.onReadyChanged) impl_->options.onReadyChanged(c.id, ready);
        c.lastReady = ready;
    }
    bus.publish(HarnessesUpdated{std::move(list)});
}

}  // namespace fulmetry::net
