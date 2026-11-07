// The app's view of the agent back end. Real: app/net (S7, Effect RPC over T3 Code's /ws, the fulmetry.* methods in
// contracts/agent/fulmetry-ext.schema.json). Fake: FakeAgentClient (scripted, deterministic).
// Results arrive as events on the EventBus.
#pragma once
#include <cstddef>
#include <optional>
#include <string>

#include "fulmetry/core/Events.h"
#include "fulmetry/core/IViewport.h"

namespace fulmetry {

// Version of the fulmetry.* extension this header speaks (x-fulmetryExtVersion in the schema).
inline constexpr int kFulmetryExtVersion = 1;
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
};

struct StartAgentOptions {
    std::optional<ObjectId> object;
    RevisionId baseRevision;  // commit the agent's worktree branches from; empty: the project's current revision
    std::string provider;     // "raven" (default when empty) or "claudeAgent"
    std::string commandId;    // idempotency key; empty: the client makes a fresh one
};

struct AgentStarted {
    AgentId agent;
    std::string thread;  // T3 Code thread id
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
    // Pump network I/O; publishes AgentUpdated, ChatPosted, CommentUpdated, RevisionSaved, CheckUpdated,
    // JobProgress and Banner.
    virtual void poll(EventBus& bus) = 0;
};

}  // namespace fulmetry
