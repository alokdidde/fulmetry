// The app's agent back end: the real T3 Code client when its bridge starts, else the scripted fake.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "fulmetry/core/IAgentClient.h"

namespace fulmetry::net {

struct AgentClientOptions {
    std::string projectRoot;  // folder that holds fulmetry.project.json; agents work in it
    bool forceFake = false;   // never start T3 Code (tests, screenshots)
    enum class FakeHarnesses { Fresh, Some, All } fakeHarnesses = FakeHarnesses::Some;
    // The command that runs `t3 fulmetry`. Empty: FULMETRY_T3, else the T3 Code fork next to the app.
    std::vector<std::string> bridgeCommand;
    std::string t3BaseDir;  // T3 Code's data directory; empty: its default (~/.t3)
};

// The bridge command found for this machine, or empty when there is none.
std::vector<std::string> findBridgeCommand();

// A connected client. When the real one cannot start, the fake comes back and its first poll publishes a
// Banner saying why, so the person knows the agents are simulated.
std::unique_ptr<IAgentClient> makeAgentClient(const Project& project, const AgentClientOptions& options);

}  // namespace fulmetry::net
