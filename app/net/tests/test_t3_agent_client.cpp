#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <thread>

#include "fulmetry/net/AgentClient.h"
#include "fulmetry/net/T3AgentClient.h"
#include "unit/TestSupport.h"

using namespace fulmetry;

namespace {

ChatRequest chatText(const std::string& text) {
    ChatRequest r;
    r.text = text;
    return r;
}

std::vector<std::string> fakeBridge() {
#ifdef _WIN32
    const char* python = "python";
#else
    const char* python = "python3";
#endif
    return {python, std::string(FULMETRY_SOURCE_DIR) + "/app/net/tests/fake_bridge.py"};
}

// Events the client has received so far, published through a bus and collected.
struct Collected {
    std::vector<AgentCard> cards;
    std::vector<ChatMessage> chat;
    std::vector<Comment> comments;
    std::vector<std::string> banners;
    std::vector<std::vector<Harness>> harnessLists;
};

// The fake bridge writes a request's events before its answer, so once a call returns its events are queued.
Collected drain(IAgentClient& client, EventBus& bus) {
    Collected got;
    int id = bus.subscribe([&](const Event& e) {
        if (auto* a = std::get_if<AgentUpdated>(&e)) got.cards.push_back(a->card);
        if (auto* c = std::get_if<ChatPosted>(&e)) got.chat.push_back(c->message);
        if (auto* c = std::get_if<CommentUpdated>(&e)) got.comments.push_back(c->comment);
        if (auto* b = std::get_if<Banner>(&e)) got.banners.push_back(b->text);
        if (auto* h = std::get_if<HarnessesUpdated>(&e)) got.harnessLists.push_back(h->harnesses);
    });
    client.poll(bus);
    bus.drain();
    bus.unsubscribe(id);
    return got;
}

}  // namespace

TEST_CASE("agentUpdated decodes waiting and stopped onto the card") {
    auto waiting = net::wire::agentCard(
        R"({"type":"fulmetry.agentUpdated","agentId":"t1","name":"Resize wheels","state":"waiting","step":"Run pytest?",)"
        R"("parts":["part:rover/wheel-l"],"replies":2,"commentId":"cmt:1","pendingApproval":{"requestId":"r1","title":"Run pytest?"},)"
        R"("provider":"raven","model":"default","branch":"fulmetry/cmt-1","worktreePath":"/w/1"})");
    CHECK(waiting.state == AgentState::Working);
    REQUIRE(waiting.pendingApproval);
    CHECK(waiting.pendingApproval->requestId == "r1");
    CHECK(std::string(agentWireState(waiting)) == "waiting");
    CHECK(waiting.parts.size() == 1);
    CHECK(waiting.comment->value == "cmt:1");
    CHECK(waiting.worktreePath == "/w/1");

    auto stopped = net::wire::agentCard(R"({"agentId":"t1","name":"x","state":"stopped","step":"Stopped"})");
    CHECK(stopped.state == AgentState::Failed);
    CHECK(stopped.stopped);
    CHECK(std::string(agentWireState(stopped)) == "stopped");
}

TEST_CASE("a harness decodes with its sign-in prompt") {
    auto h = net::wire::harness(
        R"({"id":"codex","driver":"codex","name":"Codex","status":"checking","canSignIn":true,)"
        R"("models":[{"id":"gpt-6.1-sol","name":"GPT-6.1 Sol"}],)"
        R"("setup":{"step":"signingIn","prompt":"deviceCode","url":"https://x/device","userCode":"AB-12","fraction":-1}})");
    CHECK(h.status == HarnessStatus::Checking);
    CHECK(h.setup.step == HarnessSetup::Step::SigningIn);
    CHECK(h.setup.prompt == HarnessSetup::Prompt::DeviceCode);
    CHECK(h.setup.userCode == "AB-12");
    CHECK(h.models.front().id == "gpt-6.1-sol");
}

TEST_CASE("chat and a comment reach the bridge and come back as events") {
    auto project = test::rover();
    net::T3AgentClient client(project, FULMETRY_SOURCE_DIR "/fixtures/rover", fakeBridge());
    REQUIRE(client.connect("t3://local").ok());
    CHECK(client.connected());
    REQUIRE(client.harnesses().size() == 2);
    CHECK(client.harnesses().front().id == "raven");

    EventBus bus;
    auto chat = client.sendChat(chatText("what is this?"));
    REQUIRE(chat.ok());
    CHECK(chat.value().value == "chat");

    Comment c;
    c.id = CommentId("cmt:9");
    c.anchor.part = PartId("part:rover/wheel-l");
    c.text = "make the wheels bigger";
    auto started = client.startAgentForComment(c, std::nullopt);
    REQUIRE(started.ok());
    CHECK(started.value().agent.value == "thread-cmt:9");

    // Two more requests: every agent works, then finishes.
    REQUIRE(client.reply(AgentId("chat"), "thanks").ok());
    StartAgentOptions onClaude;
    onClaude.provider = "claudeAgent";
    auto refused = client.startAgentForComment(c, std::nullopt, onClaude);
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().code == "harness_not_ready");

    auto got = drain(client, bus);
    REQUIRE(got.harnessLists.size() == 1);
    REQUIRE(got.chat.size() == 1);
    CHECK(got.chat[0].author == "Raven");
    CHECK(got.chat[0].agent->value == "chat");
    REQUIRE(got.comments.size() == 2);
    CHECK(got.comments[0].status == CommentStatus::Working);
    CHECK(got.comments[1].status == CommentStatus::Resolved);
    CHECK(got.comments[1].text == "make the wheels bigger");  // merged from the comment that started it
    CHECK(got.comments[1].reply == "Resized the wheels to 70 mm.");
    CHECK(got.cards.back().state == AgentState::Done);
}

TEST_CASE("an unsupported harness call fails with the bridge's code") {
    net::T3AgentClient client(test::rover(), FULMETRY_SOURCE_DIR "/fixtures/rover", fakeBridge());
    REQUIRE(client.connect("t3://local").ok());
    auto st = client.signInHarness("raven");
    REQUIRE_FALSE(st.ok());
    CHECK(st.error().code == "unsupported");
    CHECK(client.signInHarness("claudeAgent").ok());
}

TEST_CASE("a bridge that dies says so and refuses calls") {
#ifndef _WIN32
    setenv("FAKE_BRIDGE", "die", 1);
    net::T3AgentClient client(test::rover(), FULMETRY_SOURCE_DIR "/fixtures/rover", fakeBridge());
    auto st = client.connect("t3://local");
    unsetenv("FAKE_BRIDGE");
    REQUIRE_FALSE(st.ok());
    EventBus bus;
    auto got = drain(client, bus);
    CHECK_FALSE(got.banners.empty());
    CHECK(client.sendChat(chatText("hello")).error().code == "not_connected");
#endif
}

TEST_CASE("without a bridge the factory falls back to the fake and says why") {
    net::AgentClientOptions options;
    options.bridgeCommand = {"/nonexistent/fulmetry-bridge"};
    auto client = net::makeAgentClient(test::rover(), options);
    REQUIRE(client);
    CHECK(client->connected());
    EventBus bus;
    auto got = drain(*client, bus);
    REQUIRE(got.banners.size() == 1);
    CHECK(got.banners[0].rfind("Agents are simulated", 0) == 0);
}
