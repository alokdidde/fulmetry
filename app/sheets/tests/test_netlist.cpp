#include <doctest/doctest.h>

#include <ostream>  // MSVC: doctest streams std::string_view and needs the full ostream type

#include <map>
#include <string>
#include <vector>

#include "SheetsTestSupport.h"
#include "fulmetry/sheets/Netlist.h"

using namespace fulmetry::sheets;

namespace {

std::vector<std::string> nodesOf(const Net& n) {
    std::vector<std::string> out;
    for (const auto& node : n.nodes) out.push_back(node.ref + "." + node.pin);
    return out;
}

}  // namespace

TEST_CASE("rover nets match KiCad's netlist export") {
    // From `kicad-cli sch export netlist` (KiCad 7.0.11, on a copy down-converted to the 7 format).
    // KiCad writes local labels as "/AIN1"; we keep the plain label name.
    const std::map<std::string, std::vector<std::string>> expected = {
        {"+3V3", {"C4.1", "U1.2", "U1.3", "U3.5"}},
        {"AIN1", {"U1.10", "U2.16"}},
        {"AIN2", {"U1.11", "U2.15"}},
        {"BIN1", {"U1.13", "U2.9"}},
        {"BIN2", {"U1.16", "U2.10"}},
        {"MOTOR_nSLEEP", {"U1.12", "U2.1"}},
        {"GND", {"C1.2", "C3.2", "C4.2", "D1.1", "J1.2", "J2.3", "U1.1", "U1.39", "U2.3", "U2.6", "U2.13", "U3.2"}},
        {"Net-(D1-A)", {"D1.2", "R1.2"}},
        {"Net-(J3-Pin_1)", {"J3.1", "U2.2"}},
        {"Net-(J3-Pin_2)", {"J3.2", "U2.4"}},
        {"Net-(J4-Pin_1)", {"J4.1", "U2.7"}},
        {"Net-(J4-Pin_2)", {"J4.2", "U2.5"}},
        {"Net-(U1-IO2)", {"R1.1", "U1.24"}},
        {"Net-(U2-VCP)", {"C2.2", "U2.11"}},
        {"Net-(U2-VINT)", {"C1.1", "U2.14"}},
        {"SERVO_PWM", {"J2.1", "U1.23"}},
        {"VBAT", {"C2.1", "C3.1", "J1.1", "J2.2", "U2.12", "U3.1", "U3.3"}},
        {"unconnected-(U1-RXD0-Pad34)", {"U1.34"}},
        {"unconnected-(U1-TXD0-Pad35)", {"U1.35"}},
        {"unconnected-(U2-nFAULT-Pad8)", {"U2.8"}},
        {"unconnected-(U3-NC-Pad4)", {"U3.4"}},
    };
    std::vector<Net> nets = buildNets(test::rover());
    std::map<std::string, std::vector<std::string>> got;
    for (const auto& n : nets) got[n.name] = nodesOf(n);
    CHECK(got == expected);
    CHECK(nets.size() == expected.size());
}

TEST_CASE("VBAT reaches the battery, the motor driver and the regulator input") {
    Netlist nl = Netlist::build(test::rover());
    const Net* vbat = nl.find("VBAT");
    REQUIRE(vbat);
    CHECK_FALSE(vbat->autoNamed);
    CHECK(vbat->has("J1", "1"));
    CHECK(vbat->has("U2", "VM"));
    CHECK(vbat->has("U3", "VIN"));
    CHECK(nl.netOf("J1", "1") == vbat);
    CHECK(nl.netOf("U2", "12") == vbat);
    CHECK(nl.netOf("U3", "VIN") == vbat);
    CHECK(nl.netOf("J2", "2") == vbat);  // servo power
}

TEST_CASE("GND is one net") {
    Netlist nl = Netlist::build(test::rover());
    const Net* gnd = nl.find("GND");
    REQUIRE(gnd);
    int named = 0;
    for (const auto& n : nl.nets())
        if (n.name == "GND") ++named;
    CHECK(named == 1);
    for (auto [ref, pin] : {std::pair{"J1", "2"}, {"U1", "1"}, {"U1", "39"}, {"U2", "GND"}, {"U3", "GND"},
                            {"D1", "K"}, {"J2", "3"}, {"C1", "2"}}) {
        CAPTURE(ref);
        CAPTURE(pin);
        CHECK(nl.netOf(ref, pin) == gnd);
    }
    // PWR_FLAG sits on GND but is not a node and does not rename it.
    for (const auto& node : gnd->nodes) CHECK(node.ref[0] != '#');
}

TEST_CASE("the LED GPIO net joins U1 IO2 to the resistor") {
    Netlist nl = Netlist::build(test::rover());
    const Net* led = nl.netOf("U1", "IO2");
    REQUIRE(led);
    CHECK(led == nl.netOf("U1", "24"));
    CHECK(led == nl.netOf("R1", "1"));
    CHECK(led->autoNamed);
    CHECK(led->name == "Net-(U1-IO2)");
    CHECK(led->nodes.size() == 2);
    CHECK(nl.netOf("R1", "2") == nl.netOf("D1", "A"));
    CHECK(nl.netOf("R1", "2") != led);
}

TEST_CASE("labels, power and no-connect details") {
    Netlist nl = Netlist::build(test::rover());
    CHECK(nl.netOf("U1", "IO25") == nl.netOf("U2", "AIN1"));
    CHECK(nl.netOf("U1", "IO15")->name == "SERVO_PWM");
    CHECK(nl.netOf("U3", "VOUT")->name == "+3V3");
    CHECK(nl.netOf("U1", "RXD0")->noConnect);
    CHECK(nl.netOf("U1", "nope") == nullptr);
    CHECK(nl.netOf("Q9", "1") == nullptr);
    CHECK(nl.find("NOPE") == nullptr);
    CHECK(nl.netOf("U2", "AISEN")->nodes.front().type == PinType::Passive);
}

namespace {

// Two resistors and a label, wired by hand for the connectivity rules.
std::string twoResistors(const std::string& body) {
    return R"((kicad_sch (version 20231120)
      (lib_symbols (symbol "Device:R"
        (symbol "R_1_1"
          (pin passive line (at 0 3.81 270) (length 1.27) (name "~") (number "1"))
          (pin passive line (at 0 -3.81 90) (length 1.27) (name "~") (number "2")))))
      (symbol (lib_id "Device:R") (at 0 0 0) (unit 1) (property "Reference" "R1" (at 0 0 0)))
      (symbol (lib_id "Device:R") (at 20 0 0) (unit 1) (property "Reference" "R2" (at 0 0 0)))
      )" + body + ")";
}

Netlist netsOf(const std::string& text) {
    auto r = parseSchematic(text);
    REQUIRE_MESSAGE(r.ok(), (r.ok() ? std::string() : r.error().message));
    return Netlist::build(r.value());
}

}  // namespace

TEST_CASE("connectivity: wires, mid-wire points and crossings") {
    // R1.2 is at (0, 3.81), R2.2 at (20, 3.81). A wire between them joins them.
    Netlist joined = netsOf(twoResistors("(wire (pts (xy 0 3.81) (xy 20 3.81)))"));
    CHECK(joined.netOf("R1", "2") == joined.netOf("R2", "2"));
    CHECK(joined.netOf("R1", "2")->name == "Net-(R1-Pad2)");
    CHECK(joined.netOf("R1", "1")->name == "unconnected-(R1-Pad1)");

    // A wire passing under a pin (pin on its interior) connects; a label on the interior names it.
    Netlist mid = netsOf(twoResistors(R"((wire (pts (xy -5 3.81) (xy 25 3.81)))
                                         (label "MID" (at 10 3.81 0)))"));
    CHECK(mid.netOf("R1", "2") == mid.netOf("R2", "2"));
    CHECK(mid.netOf("R1", "2")->name == "MID");

    // Crossing wires do not connect without a junction at a shared endpoint.
    Netlist cross = netsOf(twoResistors(R"((wire (pts (xy 0 3.81) (xy 0 20)))
                                           (wire (pts (xy -10 10) (xy 30 10)))
                                           (wire (pts (xy 20 3.81) (xy 20 30))))"));
    CHECK(cross.netOf("R1", "2") != cross.netOf("R2", "2"));

    // Separate wires with the same local label join.
    Netlist labelled = netsOf(twoResistors(R"((wire (pts (xy 0 3.81) (xy 0 10))) (label "SIG" (at 0 10 0))
                                              (wire (pts (xy 20 3.81) (xy 20 10))) (label "SIG" (at 20 10 0)))"));
    CHECK(labelled.netOf("R1", "2") == labelled.netOf("R2", "2"));
    CHECK(labelled.netOf("R1", "2")->name == "SIG");

    // Global labels outrank local labels; polylines become segments.
    Netlist ranked = netsOf(twoResistors(R"((wire (pts (xy 0 3.81) (xy 10 3.81) (xy 20 3.81)))
                                            (label "aaa" (at 0 3.81 0)) (global_label "ZZZ" (at 20 3.81 0)))"));
    CHECK(ranked.netOf("R1", "2")->name == "ZZZ");
    CHECK(ranked.netOf("R1", "2")->labels == std::vector<std::string>{"ZZZ", "aaa"});
}

TEST_CASE("natural order") {
    CHECK(naturalLess("R2", "R10"));
    CHECK_FALSE(naturalLess("R10", "R2"));
    CHECK(naturalLess("U1", "U2"));
    CHECK(naturalLess("C1", "R1"));
    CHECK(naturalLess("A", "A1"));
    CHECK_FALSE(naturalLess("U1", "U1"));
}
