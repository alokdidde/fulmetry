// The app's view of the agent back end. Real: app/net (S7, WebSocket to the T3 Code server).
// Fake: FakeAgentClient (scripted, deterministic). Results arrive as events on the EventBus.
#pragma once
#include <optional>
#include <string>

#include "fulmetry/core/Events.h"
#include "fulmetry/core/IViewport.h"

namespace fulmetry {

enum class ChatScope { Project, Object, Part, Simulation, NewObject };

struct ChatRequest {
    std::string text;
    ChatScope scope = ChatScope::Project;
    std::optional<ObjectId> object;
    std::optional<PartId> part;
};

class IAgentClient {
public:
    virtual ~IAgentClient() = default;
    virtual Status connect(const std::string& url) = 0;
    virtual bool connected() const = 0;
    // A comment starts its own agent immediately (UI spec). The render shows the agent what was pointed at.
    virtual Result<AgentId> startAgentForComment(const Comment& comment, const std::optional<Image>& render) = 0;
    // A chat message also starts an agent; Claude's one-line reply arrives as ChatPosted.
    virtual Result<AgentId> sendChat(const ChatRequest& request) = 0;
    // Replying in an agent's thread restarts it on the same subject.
    virtual Status reply(const AgentId& agent, const std::string& text) = 0;
    // Pump network I/O; publishes AgentUpdated, ChatPosted, CommentUpdated, RevisionSaved, CheckUpdated.
    virtual void poll(EventBus& bus) = 0;
};

}  // namespace fulmetry
