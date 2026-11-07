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
        ChatRequest req{"add a small LED", ChatScope::Project, std::nullopt, std::nullopt, "cmd:conformance-chat", "", ""};
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

// Harness setup. The client must offer at least one ready harness and one that needs signing in with a prompt the
// app can answer (browser accept or credential fields), and one it can install when canInstall says so.
namespace detail {
inline const Harness* findHarness(const std::vector<Harness>& hs, const std::string& id) {
    for (const auto& h : hs) if (h.id == id) return &h;
    return nullptr;
}
inline const std::vector<Harness>* lastHarnesses(const Recorder& rec) {
    const std::vector<Harness>* out = nullptr;
    for (const auto& e : rec.seen)
        if (auto* u = std::get_if<HarnessesUpdated>(&e)) out = &u->harnesses;
    return out;
}
inline bool pollHarness(IAgentClient& c, Recorder& rec, const std::string& id, int maxPolls,
                        const std::function<bool(const Harness&)>& pred) {
    for (int i = 0; i < maxPolls; ++i) {
        c.poll(rec.bus);
        rec.bus.drain();
        if (auto* hs = lastHarnesses(rec))
            if (auto* h = findHarness(*hs, id); h && pred(*h)) return true;
    }
    return false;
}
}  // namespace detail

inline void harnessConformance(const std::function<std::unique_ptr<IAgentClient>()>& make, const std::string& url,
                               const Project& project, int maxPolls = 50) {
    auto c = make();
    REQUIRE(c->connect(url).ok());
    detail::Recorder rec;
    const auto all = c->harnesses();
    REQUIRE(!all.empty());
    const Harness* ready = nullptr;
    const Harness* signIn = nullptr;
    const Harness* install = nullptr;
    for (const auto& h : all) {
        CHECK(!h.id.empty());
        CHECK(!h.name.empty());
        if (h.status == HarnessStatus::Ready && !ready) ready = &h;
        if (h.status == HarnessStatus::NeedsSignIn && h.canSignIn && !signIn) signIn = &h;
        if (h.status == HarnessStatus::NeedsInstall && h.canInstall && !install) install = &h;
        if (h.status == HarnessStatus::Ready) CHECK(!h.models.empty());
    }
    REQUIRE(ready);

    SUBCASE("an agent runs on the harness and model it is given") {
        const std::string model = ready->models.back().id;
        ChatRequest req{"add a small LED", ChatScope::Project, std::nullopt, std::nullopt, "cmd:conformance-harness",
                        ready->id, model};
        auto id = c->sendChat(req);
        REQUIRE(id.ok());
        REQUIRE(rec.pollUntil(*c, id.value(), maxPolls, [](const AgentCard&) { return true; }));
        CHECK(rec.last(id.value())->provider == ready->id);
        CHECK(rec.last(id.value())->model == model);

        StartAgentOptions opts;
        opts.provider = ready->id;
        REQUIRE(!project.comments.empty());
        auto started = c->startAgentForComment(project.comments.front(), std::nullopt, opts);
        REQUIRE(started.ok());
        REQUIRE(rec.pollUntil(*c, started->agent, maxPolls, [](const AgentCard&) { return true; }));
        CHECK(rec.last(started->agent)->provider == ready->id);
        CHECK(rec.last(started->agent)->model == ready->models.front().id);  // its default
    }

    SUBCASE("unknown or unready harnesses and models are refused") {
        ChatRequest req{"hello", ChatScope::Project, std::nullopt, std::nullopt, "", "no-such-harness", ""};
        CHECK(!c->sendChat(req).ok());
        req.provider = ready->id;
        req.model = "no-such-model";
        CHECK(!c->sendChat(req).ok());
        if (signIn) {
            req.provider = signIn->id;
            req.model.clear();
            CHECK(!c->sendChat(req).ok());
        }
        CHECK(!c->installHarness("no-such-harness").ok());
        CHECK(!c->signInHarness("no-such-harness").ok());
        CHECK(!c->answerSignIn(ready->id, {}).ok());  // nothing to answer
    }

    SUBCASE("signing in prompts, verifies and ends ready; signing out and back in works") {
        REQUIRE(signIn);
        const std::string id = signIn->id;
        REQUIRE(c->signInHarness(id).ok());
        REQUIRE(detail::pollHarness(*c, rec, id, maxPolls, [](const Harness& h) {
            return h.setup.prompt != HarnessSetup::Prompt::None || h.status == HarnessStatus::Ready;
        }));
        const Harness prompted = *detail::findHarness(*detail::lastHarnesses(rec), id);
        if (prompted.status != HarnessStatus::Ready) {
            CHECK(prompted.status == HarnessStatus::Checking);
            SignInAnswer answer;
            for (const auto& f : prompted.setup.fields) answer.values[f.name] = "test-value";
            if (prompted.setup.prompt == HarnessSetup::Prompt::Credentials) {
                CHECK(!prompted.setup.fields.empty());
                CHECK(!c->answerSignIn(id, SignInAnswer{}).ok());  // empty fields are refused
            }
            if (prompted.setup.prompt == HarnessSetup::Prompt::Browser ||
                prompted.setup.prompt == HarnessSetup::Prompt::Credentials)
                REQUIRE(c->answerSignIn(id, answer).ok());
            if (prompted.setup.prompt == HarnessSetup::Prompt::DeviceCode) {
                CHECK(!prompted.setup.url.empty());
                CHECK(!prompted.setup.userCode.empty());
            }
            REQUIRE(detail::pollHarness(*c, rec, id, maxPolls, [](const Harness& h) { return h.status == HarnessStatus::Ready; }));
        }
        REQUIRE(c->signOutHarness(id).ok());
        CHECK(detail::findHarness(c->harnesses(), id)->status == HarnessStatus::NeedsSignIn);
        REQUIRE(c->signInHarness(id).ok());
        REQUIRE(c->cancelHarnessSetup(id).ok());
        CHECK(detail::findHarness(c->harnesses(), id)->status == HarnessStatus::NeedsSignIn);
    }

    SUBCASE("installing reports progress and ends installed") {
        if (!install) return;
        const std::string id = install->id;
        REQUIRE(c->installHarness(id).ok());
        CHECK(detail::findHarness(c->harnesses(), id)->setup.step == HarnessSetup::Step::Installing);
        REQUIRE(detail::pollHarness(*c, rec, id, maxPolls, [](const Harness& h) {
            return h.status == HarnessStatus::NeedsSignIn || h.status == HarnessStatus::Ready;
        }));
        CHECK(!detail::findHarness(c->harnesses(), id)->version.empty());
    }

    SUBCASE("turning a harness off keeps it but takes it out of use, and back on restores it") {
        const std::string id = ready->id;
        REQUIRE(c->setHarnessEnabled(id, false).ok());
        CHECK(detail::findHarness(c->harnesses(), id)->status == HarnessStatus::Disabled);
        ChatRequest req{"hello", ChatScope::Project, std::nullopt, std::nullopt, "", id, ""};
        CHECK(!c->sendChat(req).ok());
        REQUIRE(c->setHarnessEnabled(id, true).ok());
        CHECK(detail::findHarness(c->harnesses(), id)->status == HarnessStatus::Ready);
    }

    SUBCASE("a registry harness can be added and removed; built-in ones cannot be removed") {
        auto found = c->searchHarnessRegistry("");
        REQUIRE(found.ok());
        if (found->empty()) return;
        auto added = c->addRegistryHarness(found->front().id);
        REQUIRE(added.ok());
        const Harness* h = detail::findHarness(c->harnesses(), added.value());
        REQUIRE(h);
        CHECK(h->fromRegistry);
        CHECK(!c->removeHarness(ready->id).ok());
        REQUIRE(c->removeHarness(added.value()).ok());
        CHECK(!detail::findHarness(c->harnesses(), added.value()));
        CHECK(!c->addRegistryHarness("no-such-agent").ok());
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
