// Every IAgentClient (fake, and the real one against a fake-provider T3 Code server) must pass this.
// agentApprovalConformance needs a client whose agents ask for approval before editing (supervised mode).
#pragma once
#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <set>
#include <string>

#include "fulmetry/core/IAgentClient.h"

namespace fulmetry::conformance {

namespace detail {
struct Recorder {
    EventBus bus;
    std::vector<Event> seen;
    Recorder() { bus.subscribe([this](const Event& e) { seen.push_back(e); }); }
    const AgentCard* last(const AgentId& id) const {
        const AgentCard* card = nullptr;
        for (const auto& e : seen)
            if (auto* a = std::get_if<AgentUpdated>(&e); a && a->card.id == id) card = &a->card;
        return card;
    }
    // Polls until pred(last card of id) holds; false after maxPolls.
    bool pollUntil(IAgentClient& c, const AgentId& id, int maxPolls, const std::function<bool(const AgentCard&)>& pred) {
        for (int i = 0; i < maxPolls; ++i) {
            c.poll(bus);
            bus.drain();
            if (auto* card = last(id); card && pred(*card)) return true;
        }
        return false;
    }
};
inline bool isDone(const AgentCard& c) { return c.state == AgentState::Done; }
}  // namespace detail

inline void agentClientConformance(const std::function<std::unique_ptr<IAgentClient>()>& make,
                                   const std::string& url, const Project& project, int maxPolls = 50) {
    auto c = make();
    REQUIRE(c->connect(url).ok());
    REQUIRE(!project.comments.empty());
    const Comment& comment = project.comments.front();
    detail::Recorder rec;

    SUBCASE("a comment's agent edits, saves a revision and resolves the comment") {
        StartAgentOptions opts;
        opts.commandId = "cmd:conformance-comment";
        auto started = c->startAgentForComment(comment, std::nullopt, opts);
        REQUIRE(started.ok());
        const AgentId id = started->agent;
        CHECK(!started->thread.empty());

        auto again = c->startAgentForComment(comment, std::nullopt, opts);  // same commandId: same agent
        REQUIRE(again.ok());
        CHECK(again->agent == id);

        REQUIRE(rec.pollUntil(*c, id, maxPolls, detail::isDone));

        bool working = false, resolved = false, revision = false, named = false;
        std::set<std::string> agents;
        for (const auto& e : rec.seen) {
            if (auto* u = std::get_if<CommentUpdated>(&e)) {
                CHECK(u->comment.id == comment.id);
                working |= u->comment.status == CommentStatus::Working;
                resolved |= u->comment.status == CommentStatus::Resolved;
            }
            if (auto* r = std::get_if<RevisionSaved>(&e)) {
                revision |= r->revision.number > project.revision.number;
                CHECK(r->agent == std::optional<AgentId>(id));
                CHECK(r->comment == std::optional<CommentId>(comment.id));
                CHECK(!r->branch.empty());
                CHECK(!r->parentSha.empty());
            }
            if (auto* a = std::get_if<AgentUpdated>(&e)) {
                named |= !a->card.name.empty() && a->card.name != "Agent";
                agents.insert(a->card.id.value);
            }
        }
        CHECK(working);
        CHECK(resolved);
        CHECK(revision);
        CHECK(named);
        CHECK(agents.size() == 1);
        const AgentCard* done = rec.last(id);
        REQUIRE(done);
        CHECK(std::string(agentWireState(*done)) == "done");
        REQUIRE(done->revision.has_value());
        CHECK(*done->revision > project.revision.number);
        CHECK(!done->provider.empty());
        CHECK(!done->branch.empty());
        CHECK(done->comment == std::optional<CommentId>(comment.id));
    }

    SUBCASE("a chat message starts an agent and gets a reply") {
        ChatRequest req{"add a small LED", ChatScope::Project, std::nullopt, std::nullopt, "cmd:conformance-chat"};
        auto id = c->sendChat(req);
        REQUIRE(id.ok());
        auto again = c->sendChat(req);
        REQUIRE(again.ok());
        CHECK(again.value() == id.value());
        REQUIRE(rec.pollUntil(*c, id.value(), maxPolls, detail::isDone));
        bool posted = false;
        for (const auto& e : rec.seen)
            if (auto* p = std::get_if<ChatPosted>(&e)) posted |= p->message.agent == std::optional<AgentId>(id.value());
        CHECK(posted);
    }

    SUBCASE("stopAgent ends the agent stopped and leaves the comment unresolved") {
        auto started = c->startAgentForComment(comment, std::nullopt);
        REQUIRE(started.ok());
        const AgentId id = started->agent;
        c->poll(rec.bus);
        rec.bus.drain();
        REQUIRE(c->stopAgent(id).ok());
        REQUIRE(rec.pollUntil(*c, id, maxPolls, [](const AgentCard& a) { return a.state == AgentState::Failed; }));
        const AgentCard* card = rec.last(id);
        CHECK(card->stopped);
        CHECK(std::string(agentWireState(*card)) == "stopped");
        for (const auto& e : rec.seen)
            if (auto* u = std::get_if<CommentUpdated>(&e)) CHECK(u->comment.status != CommentStatus::Resolved);
    }

    SUBCASE("bad requests are refused") {
        const AgentId nobody("agent:nobody");
        CHECK(!c->reply(nobody, "hello").ok());
        CHECK(!c->stopAgent(nobody).ok());
        CHECK(!c->respondToApproval(nobody, "req:1", ApprovalDecision::Accept).ok());

        Image huge{kMaxRenderSide + 1, 1, std::vector<uint8_t>(size_t(kMaxRenderSide + 1) * 4, 255)};
        CHECK(!c->startAgentForComment(comment, huge).ok());
        Image torn{4, 4, std::vector<uint8_t>(10, 255)};
        CHECK(!c->startAgentForComment(comment, torn).ok());

        auto started = c->startAgentForComment(comment, std::nullopt);
        REQUIRE(started.ok());
        CHECK(!c->respondToApproval(started->agent, "req:not-asked", ApprovalDecision::Accept).ok());
    }
}

inline void agentApprovalConformance(const std::function<std::unique_ptr<IAgentClient>()>& makeSupervised,
                                     const std::string& url, const Project& project, int maxPolls = 50) {
    auto c = makeSupervised();
    REQUIRE(c->connect(url).ok());
    REQUIRE(!project.comments.empty());
    detail::Recorder rec;

    auto started = c->startAgentForComment(project.comments.front(), std::nullopt);
    REQUIRE(started.ok());
    const AgentId id = started->agent;
    REQUIRE(rec.pollUntil(*c, id, maxPolls, [](const AgentCard& a) { return a.pendingApproval.has_value(); }));
    const ApprovalRequest request = *rec.last(id)->pendingApproval;
    CHECK(!request.requestId.empty());
    CHECK(rec.last(id)->state == AgentState::Working);
    CHECK(std::string(agentWireState(*rec.last(id))) == "waiting");

    CHECK(!c->respondToApproval(id, request.requestId + "-other", ApprovalDecision::Accept).ok());
    REQUIRE(c->respondToApproval(id, request.requestId, ApprovalDecision::Accept).ok());
    CHECK(!c->respondToApproval(id, request.requestId, ApprovalDecision::Accept).ok());  // answered already

    REQUIRE(rec.pollUntil(*c, id, maxPolls, detail::isDone));
    CHECK(!rec.last(id)->pendingApproval.has_value());
}

}  // namespace fulmetry::conformance
