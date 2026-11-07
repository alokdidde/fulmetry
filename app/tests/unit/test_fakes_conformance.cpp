// The fakes must pass the same suites the real modules will. Real modules add their own
// TEST_CASE calling the same conformance function with their own factory.
#include <doctest/doctest.h>

#include "TestSupport.h"
#include "conformance/AgentClientConformance.h"
#include "conformance/CadKernelConformance.h"
#include "conformance/SimulationConformance.h"
#include "conformance/ViewportConformance.h"
#include "fakes/FakeAgentClient.h"
#include "fakes/FakeCadKernel.h"
#include "fakes/FakeSimulation.h"
#include "fakes/FakeViewport.h"

using namespace fulmetry;

TEST_CASE("FakeViewport conformance") {
    conformance::viewportConformance([] { return std::make_unique<fakes::FakeViewport>(); }, test::rover());
}

TEST_CASE("FakeCadKernel conformance") {
    conformance::cadKernelConformance([] { return std::make_unique<fakes::FakeCadKernel>(); }, test::rover());
}

TEST_CASE("FakeSimulation conformance") {
    conformance::simulationConformance([] { return std::make_unique<fakes::FakeSimulation>(); },
                                       test::readFile(test::fixture("rover/rover-wheels-60.mjcf")),
                                       test::readFile(test::fixture("rover/rover-wheels-80.mjcf")));
}

TEST_CASE("FakeAgentClient conformance") {
    Project p = test::rover();
    conformance::agentClientConformance([p] { return std::make_unique<fakes::FakeAgentClient>(p); }, "fake://local", p);
}

TEST_CASE("FakeAgentClient approval conformance") {
    Project p = test::rover();
    conformance::agentApprovalConformance([p] { return std::make_unique<fakes::FakeAgentClient>(p, true); },
                                          "fake://local", p);
}

TEST_CASE("FakeAgentClient harness conformance") {
    Project p = test::rover();
    conformance::harnessConformance([p] { return std::make_unique<fakes::FakeAgentClient>(p); }, "fake://local", p);
}

TEST_CASE("fake harnesses: a first run has nothing ready, and Codex signs in with a device code") {
    Project p = test::rover();
    fakes::FakeAgentClient c(p, false, fakes::FakeAgentClient::Harnesses::Fresh);
    REQUIRE(c.connect("fake://local").ok());
    for (const auto& h : c.harnesses()) CHECK(h.status != HarnessStatus::Ready);
    ChatRequest hello;
    hello.text = "hello";
    auto none = c.sendChat(hello);
    REQUIRE(!none.ok());
    CHECK(none.error().code == "no_harness");

    REQUIRE(c.signInHarness("codex").ok());
    auto codex = [&] {
        for (const auto& h : c.harnesses()) if (h.id == "codex") return h;
        return Harness{};
    };
    CHECK(codex().setup.prompt == HarnessSetup::Prompt::DeviceCode);
    CHECK(!codex().setup.userCode.empty());
    EventBus bus;
    for (int i = 0; i < 6; ++i) c.poll(bus);
    CHECK(codex().status == HarnessStatus::Ready);
    auto chat = c.sendChat(hello);  // the first ready harness runs it
    CHECK(chat.ok());
}

TEST_CASE("agents are named after their task") {
    CHECK(fakes::FakeAgentClient::nameFor("It has to drive on the rug… make the wheels bigger with more grip") ==
          "Make the wheels bigger");
    CHECK(fakes::FakeAgentClient::nameFor("add a small LED.") == "Add a small LED");
}
