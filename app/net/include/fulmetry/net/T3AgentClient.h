// IAgentClient over `t3 fulmetry`, T3 Code's bridge for this app: newline-delimited JSON on the child's stdio.
// Requests are {id, method, params}, answered by {id, result} or {id, error: {code, message}}; pushes are {event}.
// The events and methods are contracts/agent/fulmetry-ext.schema.json plus the harness.* methods.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "fulmetry/core/IAgentClient.h"

namespace fulmetry::net {

class T3AgentClient : public IAgentClient {
public:
    // command: argv of the bridge, e.g. {"node", ".../apps/server/src/bin.ts", "fulmetry"}.
    T3AgentClient(Project project, std::string projectRoot, std::vector<std::string> command);
    ~T3AgentClient() override;

    // Starts the bridge and subscribes to the project. url is informational ("t3://local").
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

// Wire decoding, exposed for tests.
namespace wire {
AgentCard agentCard(const std::string& eventJson);
Harness harness(const std::string& harnessJson);
}  // namespace wire

}  // namespace fulmetry::net
