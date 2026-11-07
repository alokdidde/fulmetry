// The app's view of the agent back end. Real: app/net (S7, Effect RPC over T3 Code's /ws, the fulmetry.* methods in
// contracts/agent/fulmetry-ext.schema.json). Fake: FakeAgentClient (scripted, deterministic).
// Results arrive as events on the EventBus.
#pragma once
#include <cstddef>
#include <map>
#include <optional>
#include <string>

#include "fulmetry/core/Events.h"
#include "fulmetry/core/IViewport.h"

namespace fulmetry {

// Version of the fulmetry.* extension this header speaks (x-fulmetryExtVersion in the schema).
// 2: provider is any T3 Code provider instance id, with an optional model, on comments and chat alike.
inline constexpr int kFulmetryExtVersion = 2;
// A comment's render: at most kMaxRenderSide pixels a side, and at most kMaxRenderPngBytes once encoded as PNG.
// A larger render is refused with invalid_argument (side, or rgba size not width * height * 4) or render_too_large.
inline constexpr int kMaxRenderSide = 2048;
inline constexpr std::size_t kMaxRenderPngBytes = 4u << 20;

enum class ChatScope { Project, Object, Part, Simulation, NewObject };

struct ChatRequest {
    std::string text;
    ChatScope scope = ChatScope::Project;
    std::optional<ObjectId> object;
    std::optional<PartId> part;
    std::string commandId;  // idempotency key; empty: the client makes a fresh one
    std::string provider;   // harness (Harness::id) to run it; empty: the default harness
    std::string model;      // one of that harness's models; empty: its default model
};

struct StartAgentOptions {
    std::optional<ObjectId> object;
    RevisionId baseRevision;  // commit the agent's worktree branches from; empty: the project's current revision
    std::string provider;     // harness (Harness::id) to run it; empty: the default harness
    std::string commandId;    // idempotency key; empty: the client makes a fresh one
    std::string model;        // one of that harness's models; empty: its default model
};

struct AgentStarted {
    AgentId agent;
    std::string thread;  // T3 Code thread id
};

// What a sign-in prompt is answered with (HarnessSetup::prompt): accept or decline a browser sign-in, the
// credential fields' values, or keystrokes for a terminal sign-in.
struct SignInAnswer {
    bool accept = true;
    std::map<std::string, std::string> values;
    std::string terminalInput;
};

// A harness the registry offers (T3 Code's ACP registry), not yet added.
struct RegistryHarness {
    std::string id;  // registry agent id
    std::string name;
    std::string version;
    std::string description;
};

enum class ApprovalDecision { Accept, AcceptForSession, Decline, Cancel };

class IAgentClient {
public:
    virtual ~IAgentClient() = default;
    virtual Status connect(const std::string& url) = 0;
    virtual bool connected() const = 0;
    // A comment starts its own agent immediately (UI spec). The render shows the agent what was pointed at.
    // The same commandId twice returns the first answer and starts nothing.
    virtual Result<AgentStarted> startAgentForComment(const Comment& comment, const std::optional<Image>& render,
                                                      const StartAgentOptions& options = {}) = 0;
    // A chat message also starts an agent; Claude's one-line reply arrives as ChatPosted.
    virtual Result<AgentId> sendChat(const ChatRequest& request) = 0;
    // Replying in an agent's thread restarts it on the same subject.
    virtual Status reply(const AgentId& agent, const std::string& text) = 0;
    // Answers the agent's pendingApproval. not_found for an unknown agent or a request it is not waiting on.
    virtual Status respondToApproval(const AgentId& agent, const std::string& requestId, ApprovalDecision decision) = 0;
    // Interrupts the agent; it ends Failed with stopped set, and its comment is not resolved.
    virtual Status stopAgent(const AgentId& agent) = 0;
    // ---- Harnesses: the agent runtimes T3 Code drives (its provider instances). The real client maps these to
    // T3 Code's own methods: subscribeServerConfig (the list), provider.install.*, provider.auth.*,
    // server.updateSettings (providerInstances) and the ACP registry methods. Changes arrive as HarnessesUpdated.
    virtual std::vector<Harness> harnesses() const = 0;
    // Install, sign in and their prompts. not_found for an unknown harness; unsupported when it cannot do that.
    virtual Status installHarness(const std::string& harness) = 0;
    virtual Status signInHarness(const std::string& harness) = 0;
    virtual Status answerSignIn(const std::string& harness, const SignInAnswer& answer) = 0;
    virtual Status cancelHarnessSetup(const std::string& harness) = 0;
    virtual Status signOutHarness(const std::string& harness) = 0;
    // A disabled harness keeps its install and sign-in but is not offered for new agents.
    virtual Status setHarnessEnabled(const std::string& harness, bool enabled) = 0;
    virtual Result<std::vector<RegistryHarness>> searchHarnessRegistry(const std::string& query) = 0;
    // Adds a registry harness as a new instance and returns its id; removeHarness takes an added one away.
    virtual Result<std::string> addRegistryHarness(const std::string& registryId) = 0;
    virtual Status removeHarness(const std::string& harness) = 0;

    // Pump network I/O; publishes AgentUpdated, ChatPosted, CommentUpdated, RevisionSaved, CheckUpdated,
    // JobProgress, Banner and HarnessesUpdated.
    virtual void poll(EventBus& bus) = 0;
};

}  // namespace fulmetry
