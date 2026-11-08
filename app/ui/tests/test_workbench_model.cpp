#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "fakes/FakeAgentClient.h"
#include "fulmetry/project/ProjectIO.h"
#include "fulmetry/ui/ProjectLibrary.h"
#include "fulmetry/ui/WorkbenchModel.h"

using namespace fulmetry;
using namespace fulmetry::ui;

namespace {

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

Project rover() {
    auto p = parseProject(slurp(FULMETRY_SOURCE_DIR "/fixtures/rover/fulmetry.project.json"));
    REQUIRE(p.ok());
    return p.value();
}

}  // namespace

TEST_CASE("workbench model: tabs carry part counts and start on the first object") {
    WorkbenchModel m(rover());
    auto tabs = m.tabs();
    REQUIRE(tabs.size() == 3);
    CHECK(tabs[0].name == "All objects");
    CHECK(tabs[0].count == 2);
    CHECK(tabs[1].name == "Desk rover");
    CHECK(tabs[1].count == 13);
    CHECK(tabs[2].name == "Wireless remote");
    CHECK(tabs[2].count == 6);
    CHECK(m.activeTab() == 1);
    CHECK(m.scope()->value == "obj:rover");
}

TEST_CASE("workbench model: totals follow the scope") {
    WorkbenchModel m(rover());
    Totals t = m.totals();
    CHECK(t.costUsd == doctest::Approx(29.7));
    CHECK(t.massG == doctest::Approx(356));
    CHECK(t.supported == 3);
    CHECK(t.refuted == 1);
    CHECK(t.unresolved == 0);
    CHECK(t.printMinutes == 206);  // 199 g printed at 58 g/h
    CHECK(formatPrintTime(t.printMinutes) == "3 h 26 m");
    CHECK(formatCost(t.costUsd) == "$29.70");
    m.selectTab(0);
    t = m.totals();
    CHECK(t.unresolved == 1);
    CHECK(t.massG == doctest::Approx(356 + 73));
}

TEST_CASE("workbench model: parts list groups Printed, Electronics, Bought, Fasteners") {
    WorkbenchModel m(rover());
    auto groups = m.partGroups();
    REQUIRE(groups.size() == 1);
    CHECK(groups[0].title == "Desk rover \xC2\xB7 parts");
    REQUIRE(groups[0].rows.size() == 13);
    CHECK(groups[0].rows.front().tag == "print");
    CHECK(groups[0].rows[5].tag == "board");
    CHECK(groups[0].rows.back().tag == "hw");
    m.selectTab(0);
    CHECK(m.partGroups().size() == 2);
    CHECK(m.partCount() == 19);
}

TEST_CASE("workbench model: selecting a part moves the tab to its object") {
    WorkbenchModel m(rover());
    auto v = m.version();
    CHECK(m.selectPart(PartId("part:remote/joystick")));
    CHECK(m.version() > v);
    CHECK(m.activeTab() == 2);
    CHECK(m.aboutLabel() == "Thumb joystick");
    CHECK_FALSE(m.selectPart(PartId("part:nope")));
    m.selectTab(1);  // the joystick is not in the rover: selection clears
    CHECK_FALSE(m.selectedPart());
    CHECK(m.aboutLabel() == "Desk rover");
    CHECK(m.objectSubtitle(ObjectId("obj:rover")) == "13 parts \xC2\xB7 linked to Wireless remote");
}

TEST_CASE("workbench model: explode and x-ray are stage modes that bump the version once") {
    WorkbenchModel m(rover());
    CHECK_FALSE(m.exploded());
    CHECK_FALSE(m.xray());
    const auto v0 = m.version();
    m.setExploded(true);
    CHECK(m.exploded());
    CHECK(m.version() == v0 + 1);
    m.setExploded(true);  // no change, no rebuild
    CHECK(m.version() == v0 + 1);
    m.setXray(true);
    CHECK(m.xray());
    m.setExploded(false);
    CHECK_FALSE(m.exploded());
    CHECK(m.version() == v0 + 3);
}

TEST_CASE("workbench model: sample chat loads with agent cards and dividers") {
    WorkbenchModel m(rover());
    REQUIRE(m.loadChat(slurp(FULMETRY_SOURCE_DIR "/app/ui/assets/sample-chat.json")).ok());
    const auto& chat = m.chat();
    REQUIRE(chat.size() == 6);
    CHECK(chat[1].card->title == "Add a wireless remote");
    CHECK(chat[1].card->chips[1].label == "Screen");
    CHECK(chat[2].chips[0].label == "Camera head");
    CHECK(chat[4].kind == ChatEntry::Kind::Divider);
    CHECK(chat[5].card->chips[0].label == "Wheels \xC3\x97" "2");
    CHECK_FALSE(m.loadChat("[1,2]").ok());
}

TEST_CASE("workbench model: sending a message starts an agent whose card follows its progress") {
    WorkbenchModel m(rover());
    m.selectPart(PartId("part:rover/wheel-l"));
    fakes::FakeAgentClient agent(m.project());
    REQUIRE(agent.connect("fake://").ok());
    m.setHarnesses(agent.harnesses());
    auto req = m.compose("  Make the wheels 70 mm", "11:00");
    REQUIRE(req.ok());
    CHECK(req->part->value == "part:rover/wheel-l");
    CHECK(req->provider == "claudeAgent");
    CHECK(m.chat().back().author == "You");
    CHECK(m.chat().back().chips.size() == 1);
    REQUIRE(agent.sendChat(req.value()).ok());
    EventBus bus;
    bus.subscribe([&](const Event& e) { m.apply(e, "11:00"); });
    for (int i = 0; i < 8; ++i) {
        agent.poll(bus);
        bus.drain();
    }
    bool sawCard = false, sawDivider = false;
    for (const auto& e : m.chat()) {
        if (e.card && e.card->title == "Make the wheels 70") {
            sawCard = true;
            CHECK(e.card->done);
            CHECK(e.card->status == "Done");
            CHECK(e.card->harness == "Claude Code \xC2\xB7 Opus 5.5");
        }
        if (e.kind == ChatEntry::Kind::Divider && e.text == "Revision r4 saved") sawDivider = true;
    }
    CHECK(sawCard);
    CHECK(sawDivider);
    CHECK_FALSE(m.compose("   ", "11:01").ok());
}

TEST_CASE("workbench model: the harness choice follows what is ready") {
    WorkbenchModel m(rover());
    fakes::FakeAgentClient fresh(m.project(), false, fakes::FakeAgentClient::Harnesses::Fresh);
    m.setHarnesses(fresh.harnesses());
    CHECK(m.needsOnboarding());
    CHECK(m.choice().harness.empty());
    CHECK(m.choiceLabel() == "No harness connected");
    const size_t before = m.chat().size();
    auto refused = m.compose("hello", "09:00");
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().code == "no_harness");
    CHECK(m.chat().size() == before);  // nothing was added
    CHECK_FALSE(m.choose("codex"));    // not ready

    fakes::FakeAgentClient all(m.project(), false, fakes::FakeAgentClient::Harnesses::All);
    m.setHarnesses(all.harnesses());
    CHECK_FALSE(m.needsOnboarding());
    CHECK(m.choice().harness == "raven");  // nothing chosen: the first ready one
    REQUIRE(m.choose("codex", "gpt-6-luna"));
    CHECK(m.choice() == HarnessChoice{"codex", "gpt-6-luna"});
    CHECK(m.choiceLabel() == "Codex \xC2\xB7 GPT-6 Luna");
    CHECK_FALSE(m.choose("codex", "no-such-model"));
    CHECK(m.choice().model == "gpt-6-luna");
    auto req = m.compose("hello", "09:01");
    REQUIRE(req.ok());
    CHECK(req->provider == "codex");
    CHECK(req->model == "gpt-6-luna");

    // Codex signs out: new work falls back to a ready harness, and the choice comes back when Codex does.
    REQUIRE(all.signOutHarness("codex").ok());
    m.setHarnesses(all.harnesses());
    CHECK(m.choice().harness == "raven");
    CHECK(m.preferredChoice().harness == "codex");
    m.setHarnesses(fakes::FakeAgentClient(m.project(), false, fakes::FakeAgentClient::Harnesses::All).harnesses());
    CHECK(m.choice() == HarnessChoice{"codex", "gpt-6-luna"});
}

#include "fakes/FakeSimulation.h"
#include "fakes/FakeViewport.h"
#include "fulmetry/ui/DriveTest.h"

TEST_CASE("drive test: the scripted drive moves the rover's parts on the stage and records what happened") {
    const Project p = rover();
    auto setup = parseSimSetup(slurp(FULMETRY_SOURCE_DIR "/fixtures/rover/sim/living-room.json"), p);
    REQUIRE(setup.ok());
    CHECK(setup->environment.props.size() > 5);
    CHECK(setup->surfaceNames.front() == "Rug, 8 mm pile");
    fakes::FakeSimulation sim;
    fakes::FakeViewport vp;
    REQUIRE(vp.loadProject(p).ok());
    DriveTest drive(sim, &vp, p, setup.value());
    REQUIRE(drive.start(slurp(FULMETRY_SOURCE_DIR "/fixtures/rover/sim/living-room.mjcf")).ok());
    REQUIRE(vp.environment().has_value());
    CHECK(drive.wheelDiameterMm() == doctest::Approx(60));
    const PartId chassis("part:rover/chassis-tub"), wheel("part:rover/wheel-l");
    const Transform chassis0 = vp.drawnPlacement(chassis);
    CHECK(chassis0.position.z == doctest::Approx(30));  // at the start the parts are where the manifest has them

    drive.runRoute();
    bool sawRug = false;
    for (int i = 0; i < 600 && !drive.routeDone(); ++i) {
        drive.step(1.0 / 60);
        sawRug |= drive.onSurface();
    }
    CHECK(drive.routeDone());
    CHECK(sawRug);
    CHECK(drive.distanceM() > 1.2);  // the fake slows 60 mm wheels to 0.2 m/s on the rug
    const Transform chassis1 = vp.drawnPlacement(chassis), wheel1 = vp.drawnPlacement(wheel);
    CHECK(chassis1.position.x > chassis0.position.x + 800);
    // the wheel stays on its axle, 68 mm to the side of the chassis
    const Vec3 d = wheel1.position - chassis1.position;
    CHECK(std::sqrt(d.x * d.x + d.y * d.y) == doctest::Approx(68).epsilon(0.02));
    REQUIRE(!drive.findings().empty());
    const Finding* surface = nullptr;
    bool speed = false;
    for (const auto& f : drive.findings()) {
        if (f.id == "surface") surface = &f;
        speed |= f.id == "speed";
    }
    REQUIRE(surface);  // the rug was measured, even though the run ended on it
    CHECK_FALSE(surface->good);
    CHECK(surface->part == std::optional<PartId>(PartId("part:rover/wheel-l")));
    CHECK(speed);

    drive.stop();
    CHECK_FALSE(vp.environment().has_value());
    CHECK(vp.drawnPlacement(chassis) == chassis0);
    CHECK_FALSE(parseSimSetup("{\"model\":\"x\",\"object\":\"obj:nope\",\"bodies\":{}}", p).ok());
}

TEST_CASE("a project starts from an idea, named after it, and opens from the recent list") {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / ("fulmetry-library-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(tmp);
    ProjectLibrary library((tmp / "projects").string(), (tmp / "settings.json").string());

    CHECK(ProjectLibrary::nameFromIdea("a plant watering robot that waters my basil when the soil is dry") ==
          "Plant watering robot");
    CHECK(ProjectLibrary::nameFromIdea("Build a desk lamp, with a dimmer") == "Desk lamp");
    CHECK(ProjectLibrary::nameFromIdea("   ") == "New project");
    CHECK_FALSE(library.createFromIdea("  ", "10:00").ok());

    auto first = library.createFromIdea("a plant watering robot that waters my basil", "10:00");
    REQUIRE(first.ok());
    auto project = loadProject(first.value());
    REQUIRE(project.ok());
    CHECK(project->name == "Plant watering robot");
    REQUIRE(project->objects.size() == 1);
    CHECK(project->objects[0].parts.empty());

    // The idea is the chat's first message, which the workbench loads.
    WorkbenchModel model(project.value());
    REQUIRE(model.loadChat(slurp(ProjectLibrary::chatPath(first.value()))).ok());
    REQUIRE(model.chat().size() == 1);
    CHECK(model.chat()[0].text == "a plant watering robot that waters my basil");

    // The same idea again gets its own folder; the newest is first in the recent list.
    auto second = library.createFromIdea("A plant watering robot", "10:01");
    REQUIRE(second.ok());
    CHECK(fs::path(second.value()).parent_path() != fs::path(first.value()).parent_path());
    auto recent = library.recent();
    REQUIRE(recent.size() == 2);
    CHECK(recent[0].path == second.value());
    CHECK(recent[1].detail == "Empty: started from an idea");

    // A project opened again moves to the top; one that no longer loads drops out.
    library.touch(first.value());
    CHECK(library.recent()[0].path == first.value());
    fs::remove_all(fs::path(second.value()).parent_path());
    CHECK(library.recent().size() == 1);
    fs::remove_all(tmp);
}
