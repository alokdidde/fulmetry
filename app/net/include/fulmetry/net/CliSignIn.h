// Claude Code and Codex accounts through their own CLIs, the way T3 Code expects them: its Claude and Codex
// providers have no sign-in of their own and run on whatever `claude auth login` and `codex login` stored on this
// machine. CliSignInClient wraps any agent client and takes over install, sign-in, sign-out and status for those
// two harnesses; everything else goes to the wrapped client.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "fulmetry/core/IAgentClient.h"

namespace fulmetry::net {

struct CliSignInOptions {
    // Where to look for the CLIs (and the PATH they run with). Empty: the login shell's PATH plus the usual install
    // folders, since an app opened from the Finder starts with a bare PATH.
    std::string path;
    // Told when a CLI harness becomes ready or stops being ready, so a wrapped fake can follow.
    std::function<void(const std::string& harness, bool ready)> onReadyChanged;
};

class CliSignInClient : public IAgentClient {
public:
    CliSignInClient(std::unique_ptr<IAgentClient> inner, CliSignInOptions options = {});
    ~CliSignInClient() override;

    Status connect(const std::string& url) override;
    bool connected() const override;
    Result<AgentStarted> startAgentForComment(const Comment& comment, const std::optional<Image>& render,
                                              const StartAgentOptions& options = {}) override;
    Result<AgentId> sendChat(const ChatRequest& request) override;
    Status reply(const AgentId& agent, const std::string& text) override;
    Status respondToApproval(const AgentId& agent, const std::string& requestId, ApprovalDecision decision) override;
    Status stopAgent(const AgentId& agent) override;
    std::vector<Harness> harnesses() const override;
    Status installHarness(const std::string& harness) override;
    Status signInHarness(const std::string& harness) override;
    Status answerSignIn(const std::string& harness, const SignInAnswer& answer) override;
    Status cancelHarnessSetup(const std::string& harness) override;
    Status signOutHarness(const std::string& harness) override;
    Status setHarnessEnabled(const std::string& harness, bool enabled) override;
    Result<std::vector<RegistryHarness>> searchHarnessRegistry(const std::string& query) override;
    Result<std::string> addRegistryHarness(const std::string& registryId) override;
    Status removeHarness(const std::string& harness) override;
    void poll(EventBus& bus) override;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

// Output parsing, exposed for tests.
namespace cli {
std::string firstUrl(const std::string& output);  // the first https:// URL, terminal escapes stripped
std::string lastLine(const std::string& output);  // the last non-empty line, escapes stripped
}  // namespace cli

}  // namespace fulmetry::net
