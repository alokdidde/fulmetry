// CliSignInClient against stand-in `claude` and `codex` scripts that speak like the real CLIs: the status probe,
// a browser sign-in that prints its URL (Claude's also takes a pasted code), and sign-out.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <thread>

#include <unistd.h>

#include "fulmetry/net/CliSignIn.h"

using namespace fulmetry;
namespace fs = std::filesystem;

namespace {

class StubClient : public IAgentClient {
public:
    std::vector<Harness> list;
    StubClient() {
        for (const char* id : {"raven", "claudeAgent", "codex"}) {
            Harness h;
            h.id = h.driver = h.name = id;
            h.status = HarnessStatus::NeedsSignIn;
            h.models = {{"m", "M"}};
            list.push_back(h);
        }
    }
    Status connect(const std::string&) override { return Ok{}; }
    bool connected() const override { return true; }
    Result<AgentStarted> startAgentForComment(const Comment&, const std::optional<Image>&,
                                              const StartAgentOptions&) override {
        return Error{"unsupported", ""};
    }
    Result<AgentId> sendChat(const ChatRequest&) override { return Error{"unsupported", ""}; }
    Status reply(const AgentId&, const std::string&) override { return Ok{}; }
    Status respondToApproval(const AgentId&, const std::string&, ApprovalDecision) override { return Ok{}; }
    Status stopAgent(const AgentId&) override { return Ok{}; }
    std::vector<Harness> harnesses() const override { return list; }
    Status installHarness(const std::string&) override { return Ok{}; }
    Status signInHarness(const std::string& id) override {
        signedInInner = id;
        return Ok{};
    }
    Status answerSignIn(const std::string&, const SignInAnswer&) override { return Ok{}; }
    Status cancelHarnessSetup(const std::string&) override { return Ok{}; }
    Status signOutHarness(const std::string&) override { return Ok{}; }
    Status setHarnessEnabled(const std::string&, bool) override { return Ok{}; }
    Result<std::vector<RegistryHarness>> searchHarnessRegistry(const std::string&) override {
        return std::vector<RegistryHarness>{};
    }
    Result<std::string> addRegistryHarness(const std::string&) override { return Error{"unsupported", ""}; }
    Status removeHarness(const std::string&) override { return Ok{}; }
    void poll(EventBus& bus) override { bus.publish(HarnessesUpdated{list}); }
    std::string signedInInner;
};

const char* kClaude = R"(#!/bin/sh
S="$FULMETRY_TEST_CLI_STATE"
case "$1 $2" in
"auth status")
  if [ -f "$S/claude" ]; then echo '{"loggedIn": true, "authMethod": "claude.ai", "email": "maker@example.com"}'; exit 0; fi
  echo '{"loggedIn": false}'; exit 1 ;;
"auth login")
  echo "Opening browser to sign in..."
  printf "If the browser didn't open, visit: \033]8;;https://claude.com/cai/oauth/authorize?client_id=abc&state=1\007https://claude.com/cai/oauth/authorize?client_id=abc&state=1\033]8;;\007\n"
  printf "Paste code here if prompted > "
  read code
  if [ "$code" = "good-code" ]; then touch "$S/claude"; echo "Login successful."; exit 0; fi
  echo "Invalid code. Please make sure the full code was copied."; exit 1 ;;
"auth logout") rm -f "$S/claude"; echo "Successfully logged out"; exit 0 ;;
esac
exit 2
)";

const char* kCodex = R"(#!/bin/sh
S="$FULMETRY_TEST_CLI_STATE"
case "$1" in
login)
  if [ "$2" = "status" ]; then
    if [ -f "$S/codex" ]; then echo "Logged in using ChatGPT"; exit 0; fi
    echo "Not logged in"; exit 1
  fi
  echo "Starting local login server on http://localhost:1455."
  echo "If your browser did not open, navigate to this URL to authenticate:"
  echo ""
  echo "https://auth.openai.com/oauth/authorize?response_type=code&client_id=app_X&redirect_uri=http%3A%2F%2Flocalhost%3A1455%2Fauth%2Fcallback"
  # the browser's callback reaching the local server
  while [ ! -f "$S/approve" ]; do sleep 0.02; done
  touch "$S/codex"; echo "Successfully logged in"; exit 0 ;;
logout) rm -f "$S/codex"; exit 0 ;;
esac
exit 2
)";

struct Fixture {
    fs::path dir = fs::temp_directory_path() / ("fulmetry-cli-" + std::to_string(::getpid()));
    fs::path bin = dir / "bin", state = dir / "state";
    Fixture() {
        fs::remove_all(dir);
        fs::create_directories(bin);
        fs::create_directories(state);
        write("claude", kClaude);
        write("codex", kCodex);
        ::setenv("FULMETRY_TEST_CLI_STATE", state.c_str(), 1);
    }
    ~Fixture() { fs::remove_all(dir); }
    void write(const std::string& name, const char* text) {
        std::ofstream(bin / name) << text;
        fs::permissions(bin / name, fs::perms::owner_all);
    }
    std::string path() const { return bin.string() + ":/usr/bin:/bin"; }
};

const Harness* find(const std::vector<Harness>& list, const std::string& id) {
    for (const auto& h : list)
        if (h.id == id) return &h;
    return nullptr;
}

// Polls until the condition holds; the scripts answer within milliseconds, so the bound is generous.
template <class F>
bool pollUntil(net::CliSignInClient& c, EventBus& bus, F done) {
    for (int i = 0; i < 1000; ++i) {
        c.poll(bus);
        bus.drain();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

}  // namespace

TEST_CASE("cli output: the sign-in URL comes out of a terminal hyperlink, and only once its line is complete") {
    CHECK(net::cli::firstUrl("visit: \x1b]8;;https://a.b/c?d=1\x07https://a.b/c?d=1\x1b]8;;\x07\nPaste") ==
          "https://a.b/c?d=1");
    CHECK(net::cli::firstUrl("visit: https://a.b/c?d=1\r\n") == "https://a.b/c?d=1");
    CHECK(net::cli::firstUrl("visit: https://a.b/c?d") == "");
    CHECK(net::cli::lastLine("one\n\x1b[31mtwo\x1b[0m\n\n") == "two");
}

TEST_CASE("Claude Code: signed out, a browser sign-in finished with a pasted code, then sign-out") {
    Fixture fx;
    auto stub = std::make_unique<StubClient>();
    StubClient* inner = stub.get();
    std::map<std::string, bool> ready;
    net::CliSignInOptions o;
    o.path = fx.path();
    o.onReadyChanged = [&](const std::string& id, bool r) { ready[id] = r; };
    net::CliSignInClient c(std::move(stub), o);
    EventBus bus;
    std::vector<Harness> seen;
    bus.subscribe([&](const Event& e) {
        if (auto u = std::get_if<HarnessesUpdated>(&e)) seen = u->harnesses;
    });

    REQUIRE(pollUntil(c, bus, [&] { return find(c.harnesses(), "claudeAgent")->status == HarnessStatus::NeedsSignIn; }));
    CHECK(ready["claudeAgent"] == false);
    // the inner client's own updates come through with the CLI's status, not its own
    REQUIRE(find(seen, "claudeAgent"));
    CHECK(find(seen, "claudeAgent")->status == HarnessStatus::NeedsSignIn);
    CHECK(find(seen, "raven")->status == HarnessStatus::NeedsSignIn);

    REQUIRE(c.signInHarness("claudeAgent").ok());
    CHECK(inner->signedInInner.empty());
    REQUIRE(pollUntil(c, bus, [&] { return !find(c.harnesses(), "claudeAgent")->setup.url.empty(); }));
    auto h = *find(c.harnesses(), "claudeAgent");
    CHECK(h.setup.step == HarnessSetup::Step::SigningIn);
    CHECK(h.setup.prompt == HarnessSetup::Prompt::Browser);
    CHECK(h.setup.url == "https://claude.com/cai/oauth/authorize?client_id=abc&state=1");
    REQUIRE(h.setup.fields.size() == 1);

    SignInAnswer bad;
    bad.values["code"] = "nope";
    REQUIRE(c.answerSignIn("claudeAgent", bad).ok());
    REQUIRE(pollUntil(c, bus, [&] { return find(c.harnesses(), "claudeAgent")->status == HarnessStatus::NeedsSignIn; }));
    CHECK(find(c.harnesses(), "claudeAgent")->setup.step == HarnessSetup::Step::Failed);
    CHECK(find(c.harnesses(), "claudeAgent")->setup.message.find("Invalid code") != std::string::npos);

    REQUIRE(c.signInHarness("claudeAgent").ok());
    REQUIRE(pollUntil(c, bus, [&] { return !find(c.harnesses(), "claudeAgent")->setup.url.empty(); }));
    SignInAnswer good;
    good.values["code"] = "good-code";
    REQUIRE(c.answerSignIn("claudeAgent", good).ok());
    REQUIRE(pollUntil(c, bus, [&] { return find(c.harnesses(), "claudeAgent")->status == HarnessStatus::Ready; }));
    CHECK(find(c.harnesses(), "claudeAgent")->account == "maker@example.com");
    CHECK(ready["claudeAgent"] == true);

    REQUIRE(c.signOutHarness("claudeAgent").ok());
    REQUIRE(pollUntil(c, bus, [&] { return find(c.harnesses(), "claudeAgent")->status == HarnessStatus::NeedsSignIn; }));
    CHECK(ready["claudeAgent"] == false);
    CHECK(find(c.harnesses(), "claudeAgent")->account.empty());
}

TEST_CASE("Codex: the browser sign-in finishes on its own once the callback arrives; cancel ends it quietly") {
    Fixture fx;
    net::CliSignInOptions o;
    o.path = fx.path();
    net::CliSignInClient c(std::make_unique<StubClient>(), o);
    EventBus bus;
    // asked for while the CLI is still being checked: it starts once the check is done
    REQUIRE(c.signInHarness("codex").ok());
    REQUIRE(pollUntil(c, bus, [&] { return !find(c.harnesses(), "codex")->setup.url.empty(); }));
    CHECK(find(c.harnesses(), "codex")->setup.url.rfind("https://auth.openai.com/oauth/authorize?", 0) == 0);
    CHECK(find(c.harnesses(), "codex")->setup.fields.empty());
    REQUIRE(c.cancelHarnessSetup("codex").ok());
    REQUIRE(pollUntil(c, bus, [&] { return find(c.harnesses(), "codex")->status == HarnessStatus::NeedsSignIn; }));
    CHECK(find(c.harnesses(), "codex")->setup.step != HarnessSetup::Step::Failed);

    REQUIRE(c.signInHarness("codex").ok());
    REQUIRE(pollUntil(c, bus, [&] { return !find(c.harnesses(), "codex")->setup.url.empty(); }));
    std::ofstream(fx.state / "approve") << "";
    REQUIRE(pollUntil(c, bus, [&] { return find(c.harnesses(), "codex")->status == HarnessStatus::Ready; }));
    CHECK(find(c.harnesses(), "codex")->account == "ChatGPT");
}

TEST_CASE("a CLI that is not on the PATH needs installing; other harnesses go to the wrapped client") {
    Fixture fx;
    fs::remove(fx.bin / "codex");
    auto stub = std::make_unique<StubClient>();
    StubClient* inner = stub.get();
    net::CliSignInOptions o;
    o.path = fx.path();
    net::CliSignInClient c(std::move(stub), o);
    EventBus bus;
    REQUIRE(pollUntil(c, bus, [&] { return find(c.harnesses(), "codex")->status == HarnessStatus::NeedsInstall; }));
    CHECK(c.signInHarness("codex").error().code == "not_installed");
    REQUIRE(c.signInHarness("raven").ok());
    CHECK(inner->signedInInner == "raven");
}
