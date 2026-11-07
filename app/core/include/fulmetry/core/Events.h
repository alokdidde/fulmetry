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
struct RevisionSaved { Revision revision; std::vector<PartId> changedParts; };
struct CheckUpdated { Check check; };
struct JobProgress { JobId job; std::string title; double fraction = 0; std::string message; bool done = false; };

enum class AgentState { Starting, Working, Done, Failed };
struct AgentCard {
    AgentId id;
    std::string name;        // named after its task: "Resize wheels"
    AgentState state = AgentState::Starting;
    std::string step;        // current step, or "Done"
    std::string preview;     // latest reply, two lines in the UI
    std::vector<PartId> parts;
    int replies = 0;
    std::optional<CommentId> comment;
};
struct AgentUpdated { AgentCard card; };
struct ChatMessage { std::string author; std::string text; std::optional<AgentId> agent; };
struct ChatPosted { ChatMessage message; };
struct Banner { std::string text; };

using Event = std::variant<SelectionChanged, HoverChanged, ViewModeChanged, CommentPosted, CommentUpdated,
                           RevisionSaved, CheckUpdated, JobProgress, AgentUpdated, ChatPosted, Banner>;

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
