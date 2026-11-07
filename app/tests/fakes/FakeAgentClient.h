// Fake agent back end. Deterministic: each poll() advances every running agent by one step.
// Lifecycle per the UI spec: comment -> agent named after the task -> edit -> revision -> checks -> resolved.
#pragma once
#include <cctype>
#include <map>
#include <sstream>

#include "fulmetry/core/IAgentClient.h"

namespace fulmetry::fakes {

class FakeAgentClient : public IAgentClient {
public:
    explicit FakeAgentClient(Project project) : project_(std::move(project)), revision_(project_.revision.number) {}

    Status connect(const std::string& url) override {
        if (url.rfind("ws://", 0) != 0 && url.rfind("fake://", 0) != 0) return Error{"bad_url", url};
        connected_ = true;
        return Ok{};
    }
    bool connected() const override { return connected_; }

    Result<AgentId> startAgentForComment(const Comment& c, const std::optional<Image>&) override {
        if (!connected_) return Error{"not_connected", "connect first"};
        Run r;
        r.card.id = AgentId("agent:" + std::to_string(++nextId_));
        r.card.name = nameFor(c.text);
        r.card.parts = {c.anchor.part};
        r.card.comment = c.id;
        r.comment = c;
        runs_.push_back(r);
        return r.card.id;
    }

    Result<AgentId> sendChat(const ChatRequest& req) override {
        if (!connected_) return Error{"not_connected", "connect first"};
        Run r;
        r.card.id = AgentId("agent:" + std::to_string(++nextId_));
        r.card.name = nameFor(req.text);
        if (req.part) r.card.parts = {*req.part};
        r.chat = true;
        runs_.push_back(r);
        pending_.push_back(ChatPosted{{"Claude", "Started an agent for this.", r.card.id}});
        return r.card.id;
    }

    Status reply(const AgentId& id, const std::string&) override {
        for (auto& r : runs_)
            if (r.card.id == id) {
                r.stage = 0;
                r.card.replies += 1;
                return Ok{};
            }
        return Error{"not_found", id.value};
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
    struct Run {
        AgentCard card;
        std::optional<Comment> comment;
        bool chat = false;
        int stage = 0;  // 0 start, 1 read, 2 edit, 3 save, 4 checks, 5 done
    };

    void advance(Run& r, EventBus& bus) {
        static const char* kSteps[] = {"Starting", "Reading the part", "Editing the model", "Saving the revision",
                                       "Re-running checks", "Done"};
        if (r.stage > 5) return;
        r.card.step = kSteps[r.stage];
        r.card.state = r.stage == 0 ? AgentState::Starting : r.stage == 5 ? AgentState::Done : AgentState::Working;
        if (r.stage == 0 && r.comment) {
            Comment c = *r.comment;
            c.status = CommentStatus::Working;
            c.agent = r.card.id;
            bus.publish(CommentUpdated{c});
        }
        if (r.stage == 3) {
            ++revision_;
            Revision rev{RevisionId("rev-" + std::to_string(revision_)), revision_, r.card.name};
            bus.publish(RevisionSaved{rev, r.card.parts});
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
        if (r.stage == 5) {
            r.card.preview = "Done: " + r.card.name + " (r" + std::to_string(revision_) + ").";
            r.card.replies += 1;
            if (r.comment) {
                Comment c = *r.comment;
                c.status = CommentStatus::Resolved;
                c.agent = r.card.id;
                c.reply = r.card.preview;
                bus.publish(CommentUpdated{c});
            }
        }
        bus.publish(AgentUpdated{r.card});
        ++r.stage;
    }

    Project project_;
    int revision_ = 0;
    bool connected_ = false;
    int nextId_ = 0;
    std::vector<Run> runs_;
    std::vector<Event> pending_;
};

}  // namespace fulmetry::fakes
