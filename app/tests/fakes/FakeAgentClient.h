// Fake agent back end. Deterministic: each poll() advances every running agent by one step.
// Lifecycle per the UI spec: comment -> agent named after the task -> edit -> revision -> checks -> resolved.
// With askApproval, every agent stops before editing and waits (pendingApproval) until respondToApproval.
#pragma once
#include <cctype>
#include <map>
#include <sstream>

#include "fulmetry/core/IAgentClient.h"

namespace fulmetry::fakes {

class FakeAgentClient : public IAgentClient {
public:
    explicit FakeAgentClient(Project project, bool askApproval = false)
        : project_(std::move(project)), revision_(project_.revision.number), askApproval_(askApproval) {
        head_ = project_.revision.id.empty() ? "rev-" + std::to_string(revision_) : project_.revision.id.value;
    }

    Status connect(const std::string& url) override {
        if (url.rfind("ws://", 0) != 0 && url.rfind("fake://", 0) != 0) return Error{"bad_url", url};
        connected_ = true;
        return Ok{};
    }
    bool connected() const override { return connected_; }

    Result<AgentStarted> startAgentForComment(const Comment& c, const std::optional<Image>& render,
                                              const StartAgentOptions& options = {}) override {
        if (!connected_) return Error{"not_connected", "connect first"};
        if (auto it = started_.find(options.commandId); !options.commandId.empty() && it != started_.end())
            return it->second;
        if (render) {
            if (render->width <= 0 || render->height <= 0 || render->width > kMaxRenderSide ||
                render->height > kMaxRenderSide || render->rgba.size() != size_t(render->width) * render->height * 4)
                return Error{"invalid_argument", "render must be 1.." + std::to_string(kMaxRenderSide) +
                                                     " px a side with width * height * 4 bytes"};
        }
        Run& r = newRun(c.text, {c.anchor.part}, options.provider);
        r.card.comment = c.id;
        r.card.branch = "fulmetry/" + slug(c.id.value);
        r.comment = c;
        AgentStarted s{r.card.id, r.card.id.value};
        if (!options.commandId.empty()) started_[options.commandId] = s;
        return s;
    }

    Result<AgentId> sendChat(const ChatRequest& req) override {
        if (!connected_) return Error{"not_connected", "connect first"};
        if (auto it = chats_.find(req.commandId); !req.commandId.empty() && it != chats_.end()) return it->second;
        Run& r = newRun(req.text, req.part ? std::vector<PartId>{*req.part} : std::vector<PartId>{}, "");
        r.chat = true;
        pending_.push_back(ChatPosted{{"Claude", "Started an agent for this.", r.card.id}});
        if (!req.commandId.empty()) chats_[req.commandId] = r.card.id;
        return r.card.id;
    }

    Status reply(const AgentId& id, const std::string&) override {
        Run* r = find(id);
        if (!r) return Error{"not_found", id.value};
        r->stage = 0;
        r->approved = false;
        r->card.stopped = false;
        r->card.pendingApproval.reset();
        r->card.replies += 1;
        return Ok{};
    }

    Status respondToApproval(const AgentId& id, const std::string& requestId, ApprovalDecision decision) override {
        Run* r = find(id);
        if (!r) return Error{"not_found", id.value};
        if (!r->card.pendingApproval || r->card.pendingApproval->requestId != requestId)
            return Error{"not_found", "no pending approval " + requestId};
        r->card.pendingApproval.reset();
        if (decision == ApprovalDecision::Cancel) return stopAgent(id);
        r->approved = true;  // a declined command is skipped and the agent carries on, as Raven does
        return Ok{};
    }

    Status stopAgent(const AgentId& id) override {
        Run* r = find(id);
        if (!r) return Error{"not_found", id.value};
        if (r->stage <= kDone) r->stopping = true;  // stopping a finished agent changes nothing
        return Ok{};
    }

    void poll(EventBus& bus) override {
        for (auto& e : pending_) bus.publish(e);
        pending_.clear();
        for (auto& r : runs_) advance(r, bus);
    }

    // Names an agent after its task in a few words: "make the wheels bigger" -> "Make the wheels bigger".
    static std::string nameFor(const std::string& text) {
        std::string s = text;
        auto cut = s.find("…");
        if (cut != std::string::npos && cut + 3 < s.size()) s = s.substr(cut + 3);
        std::istringstream in(s);
        std::string w, out;
        for (int i = 0; i < 4 && in >> w; ++i) out += (out.empty() ? "" : " ") + w;
        while (!out.empty() && std::ispunct(static_cast<unsigned char>(out.back()))) out.pop_back();
        if (!out.empty()) out[0] = char(std::toupper(static_cast<unsigned char>(out[0])));
        return out.empty() ? "Agent" : out;
    }

private:
    static constexpr int kEdit = 2, kSave = 3, kDone = 5;
    struct Run {
        AgentCard card;
        std::optional<Comment> comment;
        bool chat = false;
        bool approved = false;
        bool stopping = false;
        int stage = 0;  // 0 start, 1 read, 2 edit, 3 save, 4 checks, 5 done, 6 finished
    };

    static std::string slug(std::string s) {
        for (auto& ch : s) if (!std::isalnum(static_cast<unsigned char>(ch))) ch = '-';
        return s;
    }
    Run* find(const AgentId& id) {
        for (auto& r : runs_) if (r.card.id == id) return &r;
        return nullptr;
    }
    Run& newRun(const std::string& text, std::vector<PartId> parts, const std::string& provider) {
        Run r;
        r.card.id = AgentId("agent:" + std::to_string(++nextId_));
        r.card.name = nameFor(text);
        r.card.parts = std::move(parts);
        r.card.provider = provider.empty() ? "raven" : provider;
        r.card.branch = "fulmetry/agent-" + std::to_string(nextId_);
        r.card.worktreePath = "/fake/worktrees/agent-" + std::to_string(nextId_);
        runs_.push_back(std::move(r));
        return runs_.back();
    }
    void publishComment(const Run& r, CommentStatus status, const std::string& reply, EventBus& bus) {
        if (!r.comment) return;
        Comment c = *r.comment;
        c.status = status;
        c.agent = r.card.id;
        c.reply = reply;
        bus.publish(CommentUpdated{c});
    }

    void advance(Run& r, EventBus& bus) {
        static const char* kSteps[] = {"Starting", "Reading the part", "Editing the model", "Saving the revision",
                                       "Re-running checks", "Done"};
        if (r.stage > kDone) return;
        if (r.stopping) {
            r.stopping = false;
            r.stage = kDone + 1;
            r.card.state = AgentState::Failed;
            r.card.stopped = true;
            r.card.step = "Stopped";
            r.card.pendingApproval.reset();
            publishComment(r, CommentStatus::Open, "", bus);
            bus.publish(AgentUpdated{r.card});
            return;
        }
        if (r.stage == kEdit && askApproval_ && !r.approved) {
            if (r.card.pendingApproval) return;  // still waiting; nothing new to say
            r.card.step = "Waiting for approval";
            r.card.pendingApproval = ApprovalRequest{"req:" + std::to_string(++nextRequest_), "Edit the CAD source?"};
            bus.publish(AgentUpdated{r.card});
            return;
        }
        r.card.step = kSteps[r.stage];
        r.card.state = r.stage == 0 ? AgentState::Starting : r.stage == kDone ? AgentState::Done : AgentState::Working;
        if (r.stage == 0) publishComment(r, CommentStatus::Working, "", bus);
        if (r.stage == kSave) {
            ++revision_;
            std::string sha = "rev-" + std::to_string(revision_);
            RevisionSaved saved{{RevisionId(sha), revision_, r.card.name}, r.card.parts, head_, r.card.branch,
                                r.card.id, r.card.comment};
            head_ = sha;
            r.card.revision = revision_;
            bus.publish(saved);
            for (auto& ch : project_.checks)
                for (const auto& in : ch.inputs)
                    for (const auto& p : r.card.parts)
                        if (in == p) {
                            Check u = ch;
                            u.verdict = Verdict::Unresolved;
                            u.reason = "Changed in r" + std::to_string(revision_) + "; re-testing";
                            bus.publish(CheckUpdated{u});
                        }
        }
        if (r.stage == kDone) {
            r.card.preview = "Done: " + r.card.name + " (r" + std::to_string(revision_) + ").";
            r.card.replies += 1;
            publishComment(r, CommentStatus::Resolved, r.card.preview, bus);
        }
        bus.publish(AgentUpdated{r.card});
        ++r.stage;
    }

    Project project_;
    int revision_ = 0;
    bool askApproval_ = false;
    std::string head_;
    bool connected_ = false;
    int nextId_ = 0;
    int nextRequest_ = 0;
    std::vector<Run> runs_;
    // ChatPosted, not Event: moving a temporary Event into a std::vector<Event> made GCC 13 at -O3 report a false
    // -Wmaybe-uninitialized on ViewModeChanged (Events.h) in Release builds.
    std::vector<ChatPosted> pending_;
    std::map<std::string, AgentStarted> started_;
    std::map<std::string, AgentId> chats_;
};

}  // namespace fulmetry::fakes
