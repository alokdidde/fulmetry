// App-wide events. Every module talks to the UI through these, never directly.
#pragma once
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "fulmetry/core/Model.h"

namespace fulmetry {

struct SelectionChanged { std::optional<PartId> part; };
struct HoverChanged { std::optional<PartId> part; };
struct ViewModeChanged { bool exploded = false; bool joints = false; bool xray = false; std::optional<PartId> isolated; };
struct CommentPosted { Comment comment; };
struct CommentUpdated { Comment comment; };
struct RevisionSaved {
    Revision revision;
    std::vector<PartId> changedParts;
    std::string parentSha;             // commit it was saved on top of
    std::string branch;                // branch it was committed on
    std::optional<AgentId> agent;      // the agent whose work it is
    std::optional<CommentId> comment;  // the comment that agent answers
};
struct CheckUpdated { Check check; };
struct JobProgress {
    JobId job;
    std::string title;
    double fraction = 0;
    std::string message;
    bool done = false;
    bool failed = false;           // with done: it ended in an error, message says why
    std::optional<AgentId> agent;  // the agent that started it
};

// The wire (contracts/agent/fulmetry-ext.schema.json, agentUpdated.state) also has "waiting" and "stopped". Here
// they are Working with pendingApproval set and Failed with stopped set, see agentWireState().
enum class AgentState { Starting, Working, Done, Failed };
struct ApprovalRequest {
    std::string requestId;  // passed back to IAgentClient::respondToApproval
    std::string title;      // "Run pytest in the worktree?"
};
struct AgentCard {
    AgentId id;              // the T3 Code thread id
    std::string name;        // named after its task: "Resize wheels"
    AgentState state = AgentState::Starting;
    std::string step;        // current step, or "Done"
    std::string preview;     // latest reply, two lines in the UI
    std::vector<PartId> parts;
    int replies = 0;
    std::optional<CommentId> comment;
    std::optional<ApprovalRequest> pendingApproval;  // set while it waits for the user
    bool stopped = false;                             // with Failed: ended by IAgentClient::stopAgent
    std::string provider;                             // the harness running it (Harness::id)
    std::string model;                                // its model, when the harness reports one
    std::string branch;                               // branch of its worktree
    std::string worktreePath;
    std::optional<int> revision;                      // number of the revision its work was saved as
};
// agentUpdated.state on the wire: starting, working, waiting, stopped, done or failed.
inline const char* agentWireState(const AgentCard& card) {
    switch (card.state) {
        case AgentState::Starting: return "starting";
        case AgentState::Working: return card.pendingApproval ? "waiting" : "working";
        case AgentState::Done: return "done";
        case AgentState::Failed: return card.stopped ? "stopped" : "failed";
    }
    return "failed";
}
struct AgentUpdated { AgentCard card; };
struct ChatMessage { std::string author; std::string text; std::optional<AgentId> agent; };
struct ChatPosted { ChatMessage message; };
struct Banner { std::string text; };

// ---- Harnesses (IAgentClient::harnesses): T3 Code provider instances, as the onboarding screen shows them.
enum class HarnessStatus {
    Ready,         // installed, signed in, enabled
    NeedsInstall,  // not found on this machine; canInstall says whether the app can install it
    NeedsSignIn,
    Checking,      // being probed, installed or signed in (setup.step says which)
    Disabled,      // turned off by the person
    Unavailable,   // cannot run here (unsupported version, OS); message says why
};
struct HarnessModel {
    std::string id;    // "claude-opus-5-5"
    std::string name;  // "Opus 5.5"
};
struct SignInField {
    std::string name;
    std::string label;
    bool secret = false;
};
// An install or sign-in in progress (provider.install.subscribe / provider.auth.subscribe).
struct HarnessSetup {
    enum class Step { Idle, Installing, SigningIn, Verifying, Succeeded, Failed, Cancelled };
    // What the person has to do: open a URL, enter a code there, fill in fields, or use a terminal.
    enum class Prompt { None, Browser, DeviceCode, Credentials, Terminal };
    Step step = Step::Idle;
    double fraction = -1;  // install progress 0..1; below 0 when unknown
    Prompt prompt = Prompt::None;
    std::string url;       // Browser, DeviceCode
    std::string userCode;  // DeviceCode
    std::vector<SignInField> fields;  // Credentials
    std::string output;               // Terminal
    std::string message;              // progress or failure, safe to show
};
struct Harness {
    std::string id;      // T3 Code provider instance id: "raven", "claudeAgent", "codex"...
    std::string driver;  // T3 Code driver kind; several instances can share one
    std::string name;    // "Claude Code"
    std::string maker;   // "Anthropic"
    std::string accent;  // "#rrggbb", empty for the default
    HarnessStatus status = HarnessStatus::NeedsInstall;
    std::string version;
    std::string account;  // who it is signed in as, when it says
    bool canInstall = false, canSignIn = false;
    bool fromRegistry = false;  // added from the ACP registry; removeHarness takes it away
    std::string docsUrl;
    std::string message;  // why it is not ready
    std::vector<HarnessModel> models;  // the first is its default
    HarnessSetup setup;
};
struct HarnessesUpdated { std::vector<Harness> harnesses; };

using Event = std::variant<SelectionChanged, HoverChanged, ViewModeChanged, CommentPosted, CommentUpdated,
                           RevisionSaved, CheckUpdated, JobProgress, AgentUpdated, ChatPosted, Banner, HarnessesUpdated>;

// Producers may publish from any thread; drain() delivers on the caller's (UI) thread, in order.
class EventBus {
public:
    using Handler = std::function<void(const Event&)>;
    int subscribe(Handler h) {
        std::lock_guard lock(mu_);
        handlers_.push_back({++nextId_, std::move(h)});
        return nextId_;
    }
    void unsubscribe(int id) {
        std::lock_guard lock(mu_);
        std::erase_if(handlers_, [id](const auto& p) { return p.first == id; });
    }
    void publish(Event e) {
        std::lock_guard lock(mu_);
        queue_.push_back(std::move(e));
    }
    size_t drain() {
        std::vector<Event> batch;
        std::vector<std::pair<int, Handler>> hs;
        {
            std::lock_guard lock(mu_);
            batch.swap(queue_);
            hs = handlers_;
        }
        for (const auto& e : batch)
            for (const auto& [_, h] : hs) h(e);
        return batch.size();
    }

private:
    std::mutex mu_;
    std::vector<std::pair<int, Handler>> handlers_;
    std::vector<Event> queue_;
    int nextId_ = 0;
};

}  // namespace fulmetry
