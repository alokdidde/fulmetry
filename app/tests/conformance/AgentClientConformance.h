// Every IAgentClient (fake, and the real one against a fake-provider T3 Code server) must pass this.
#pragma once
#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <string>

#include "fulmetry/core/IAgentClient.h"

namespace fulmetry::conformance {

inline void agentClientConformance(const std::function<std::unique_ptr<IAgentClient>()>& make,
                                   const std::string& url, const Project& project, int maxPolls = 50) {
    auto c = make();
    REQUIRE(c->connect(url).ok());
    REQUIRE(!project.comments.empty());
    const Comment& comment = project.comments.front();

    EventBus bus;
    std::vector<Event> seen;
    bus.subscribe([&](const Event& e) { seen.push_back(e); });

    auto id = c->startAgentForComment(comment, std::nullopt);
    REQUIRE(id.ok());

    bool done = false;
    for (int i = 0; i < maxPolls && !done; ++i) {
        c->poll(bus);
        bus.drain();
        for (const auto& e : seen)
            if (auto* a = std::get_if<AgentUpdated>(&e); a && a->card.id == id.value() && a->card.state == AgentState::Done)
                done = true;
    }
    REQUIRE(done);

    bool working = false, resolved = false, revision = false, named = false;
    for (const auto& e : seen) {
        if (auto* u = std::get_if<CommentUpdated>(&e)) {
            CHECK(u->comment.id == comment.id);
            working |= u->comment.status == CommentStatus::Working;
            resolved |= u->comment.status == CommentStatus::Resolved;
        }
        if (auto* r = std::get_if<RevisionSaved>(&e)) revision |= r->revision.number > project.revision.number;
        if (auto* a = std::get_if<AgentUpdated>(&e)) named |= !a->card.name.empty() && a->card.name != "Agent";
    }
    CHECK(working);
    CHECK(resolved);
    CHECK(revision);
    CHECK(named);
}

}  // namespace fulmetry::conformance
