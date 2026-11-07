#include "fulmetry/net/AgentClient.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>

#include "fakes/FakeAgentClient.h"
#include "fulmetry/net/T3AgentClient.h"

#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fulmetry::net {

namespace fs = std::filesystem;

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

fs::path executablePath() {
#ifdef __APPLE__
    char buf[4096];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) return fs::weakly_canonical(buf);
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH)) return fs::path(buf);
#else
    std::error_code ec;
    if (auto p = fs::read_symlink("/proc/self/exe", ec); !ec) return p;
#endif
    return {};
}

// Node, looked up the way a terminal would find it. An app opened from the Finder gets a short PATH, so the
// usual install places are tried too.
std::string findNode() {
    if (auto n = env("FULMETRY_NODE"); !n.empty()) return n;
#ifdef _WIN32
    const char sep = ';';
    const char* exe = "node.exe";
#else
    const char sep = ':';
    const char* exe = "node";
#endif
    std::vector<fs::path> dirs;
    std::stringstream path(env("PATH"));
    for (std::string d; std::getline(path, d, sep);) if (!d.empty()) dirs.emplace_back(d);
    for (const char* d : {"/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"}) dirs.emplace_back(d);
    if (auto home = env("HOME"); !home.empty()) {
        dirs.emplace_back(fs::path(home) / ".volta/bin");
        std::error_code ec;
        // nvm keeps one folder per version; take the newest by name.
        fs::path newest;
        for (const auto& v : fs::directory_iterator(fs::path(home) / ".nvm/versions/node", ec))
            if (newest.empty() || v.path().filename() > newest.filename()) newest = v.path();
        if (!newest.empty()) dirs.push_back(newest / "bin");
    }
    for (const auto& d : dirs) {
        std::error_code ec;
        if (fs::is_regular_file(d / exe, ec)) return (d / exe).string();
    }
    return "";
}

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream in(s);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

// Says once, on the first poll, that the agents are simulated and why.
class FallbackAgentClient : public fakes::FakeAgentClient {
public:
    FallbackAgentClient(Project project, Harnesses start, std::string why)
        : FakeAgentClient(std::move(project), false, start), why_(std::move(why)) {}
    void poll(EventBus& bus) override {
        if (!why_.empty()) bus.publish(Banner{"Agents are simulated: " + why_});
        why_.clear();
        FakeAgentClient::poll(bus);
    }

private:
    std::string why_;
};

}  // namespace

std::vector<std::string> findBridgeCommand() {
    // FULMETRY_T3 is the command that runs T3 Code's CLI, e.g. "node /src/t3code/apps/server/src/bin.ts".
    if (auto custom = split(env("FULMETRY_T3")); !custom.empty()) {
        custom.push_back("fulmetry");
        return custom;
    }
    const auto node = findNode();
    if (node.empty()) return {};
    std::vector<fs::path> candidates;
    if (auto exe = executablePath(); !exe.empty()) {
        candidates.push_back(exe.parent_path() / "../Resources/t3/bin.mjs");  // Fulmetry.app/Contents/MacOS/..
        candidates.push_back(exe.parent_path() / "t3/bin.mjs");
    }
#ifdef FULMETRY_SOURCE_DIR
    candidates.push_back(fs::path(FULMETRY_SOURCE_DIR) / "agent/t3code/apps/server/src/bin.ts");
#endif
    for (const auto& c : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(c, ec)) return {node, fs::weakly_canonical(c).string(), "fulmetry"};
    }
    return {};
}

std::unique_ptr<IAgentClient> makeAgentClient(const Project& project, const AgentClientOptions& options) {
    using FH = AgentClientOptions::FakeHarnesses;
    const auto start = options.fakeHarnesses == FH::Fresh ? fakes::FakeAgentClient::Harnesses::Fresh
                       : options.fakeHarnesses == FH::All ? fakes::FakeAgentClient::Harnesses::All
                                                          : fakes::FakeAgentClient::Harnesses::Some;
    auto fallback = [&](std::string why) -> std::unique_ptr<IAgentClient> {
        auto fake = std::make_unique<FallbackAgentClient>(project, start, std::move(why));
        fake->connect("fake://workbench");
        return fake;
    };
    if (options.forceFake) {
        auto fake = std::make_unique<fakes::FakeAgentClient>(project, false, start);
        fake->connect("fake://workbench");
        return fake;
    }
    auto command = options.bridgeCommand.empty() ? findBridgeCommand() : options.bridgeCommand;
    if (command.empty())
        return fallback("T3 Code was not found. Install Node.js, or set FULMETRY_T3 to the command that runs it.");
    if (!options.t3BaseDir.empty()) {
        command.push_back("--base-dir");
        command.push_back(options.t3BaseDir);
    }
    auto real = std::make_unique<T3AgentClient>(project, options.projectRoot, std::move(command));
    if (auto st = real->connect("t3://local"); !st.ok()) return fallback("T3 Code did not start (" + st.error().message + ").");
    return real;
}

}  // namespace fulmetry::net
