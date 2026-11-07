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

TEST_CASE("agents are named after their task") {
    CHECK(fakes::FakeAgentClient::nameFor("It has to drive on the rug… make the wheels bigger with more grip") ==
          "Make the wheels bigger");
    CHECK(fakes::FakeAgentClient::nameFor("add a small LED.") == "Add a small LED");
}
