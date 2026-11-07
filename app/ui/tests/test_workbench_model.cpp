#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <fstream>
#include <sstream>

#include "fakes/FakeAgentClient.h"
#include "fulmetry/project/ProjectIO.h"
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
    auto req = m.compose("  Make the wheels 70 mm", "11:00");
    REQUIRE(req.ok());
    CHECK(req->part->value == "part:rover/wheel-l");
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
        }
        if (e.kind == ChatEntry::Kind::Divider && e.text == "Revision r4 saved") sawDivider = true;
    }
    CHECK(sawCard);
    CHECK(sawDivider);
    CHECK_FALSE(m.compose("   ", "11:01").ok());
}
