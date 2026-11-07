#include <doctest/doctest.h>

#include <string>

#include "SheetsTestSupport.h"

using namespace fulmetry::sheets;

TEST_CASE("fixture parses as a KiCad 8 schematic") {
    const Schematic& s = test::rover();
    CHECK(s.version == 20231120);
    CHECK(s.generator == "eeschema");
    CHECK(s.generatorVersion == "8.0");
    CHECK(s.title == "Desk rover main board");
    CHECK(s.libSymbols.size() == 12);
    CHECK_FALSE(s.wires.empty());
    CHECK(s.junctions.size() == 4);
    CHECK(s.noConnects.size() == 4);
    CHECK(s.labels.size() == 12);  // 10 local + 2 global
}

TEST_CASE("fixture has the expected parts") {
    const Schematic& s = test::rover();
    for (const char* ref : {"U1", "U2", "U3", "J1", "J2", "J3", "J4", "R1", "D1", "C1", "C2", "C3", "C4"}) {
        CAPTURE(ref);
        const PlacedSymbol* p = s.findSymbol(ref);
        REQUIRE(p);
        CHECK_FALSE(s.isPower(*p));
        CHECK(s.libSymbolFor(*p) != nullptr);
        CHECK_FALSE(p->footprint.empty());
    }
    const PlacedSymbol* u1 = s.findSymbol("U1");
    CHECK(u1->libId == "rover:ESP32-WROOM-32E");
    CHECK(u1->value == "ESP32-WROOM-32E");
    CHECK(u1->footprint == "RF_Module:ESP32-WROOM-32E");
    CHECK(u1->at == Point{101.6, 88.9});
    CHECK(s.findSymbol("U2")->value == "DRV8833PWP");
    CHECK(s.findSymbol("U3")->value == "AP2112K-3.3");
    CHECK(s.findSymbol("R1")->rotationDeg == 90);
    CHECK(s.findSymbol("J1")->mirrorY);

    int power = 0;
    for (const auto& sym : s.symbols)
        if (s.isPower(sym)) ++power;
    CHECK(power == 20);  // 11 GND, 4 VBAT, 3 +3V3, 2 PWR_FLAG
}

TEST_CASE("lib symbols keep pin number, name, type and position") {
    const Schematic& s = test::rover();
    const LibSymbol* esp = s.libSymbolFor(*s.findSymbol("U1"));
    REQUIRE(esp);
    CHECK(esp->pins.size() == 13);
    const LibPin* io2 = nullptr;
    for (const auto& p : esp->pins)
        if (p.number == "24") io2 = &p;
    REQUIRE(io2);
    CHECK(io2->name == "IO2");
    CHECK(io2->type == PinType::Bidirectional);
    CHECK(io2->at == Point{15.24, -5.08});
    CHECK(io2->angleDeg == 180);
    CHECK(io2->unit == 1);
    CHECK(io2->bodyStyle == 1);

    const PlacedSymbol* gnd = nullptr;
    for (const auto& sym : s.symbols)
        if (sym.value == "GND") gnd = &sym;
    REQUIRE(gnd);
    const LibSymbol* g = s.libSymbolFor(*gnd);
    CHECK(g->power);
    CHECK(g->pins.at(0).type == PinType::PowerIn);
    CHECK(g->pins.at(0).hidden);
    CHECK(std::string(toName(PinType::OpenCollector)) == "open_collector");
}

TEST_CASE("pins are transformed onto the sheet") {
    const Schematic& s = test::rover();
    auto pinAt = [&](const char* ref, const char* number) {
        for (const auto& pp : placedPins(s, *s.findSymbol(ref)))
            if (pp.pin->number == number) return pp.at;
        FAIL("pin not found");
        return Point{};
    };
    // Rotation 0: library y-up becomes sheet y-down.
    CHECK(test::near(pinAt("U1", "2"), Point{101.6, 68.58}));
    CHECK(test::near(pinAt("U1", "24"), Point{116.84, 93.98}));
    // Rotation 90 (counter-clockwise): pin 1 moves from the top to the left.
    CHECK(test::near(pinAt("R1", "1"), Point{123.19, 93.98}));
    CHECK(test::near(pinAt("R1", "2"), Point{130.81, 93.98}));
    // Rotation 180: anode on the left.
    CHECK(test::near(pinAt("D1", "2"), Point{134.62, 93.98}));
    // Mirror y: connector pins face right.
    CHECK(test::near(pinAt("J1", "1"), Point{30.48, 43.18}));
    CHECK(test::near(pinAt("J1", "2"), Point{30.48, 45.72}));
}

TEST_CASE("transform: rotation then mirror x") {
    PlacedSymbol sym;
    sym.at = {10, 10};
    sym.rotationDeg = 90;
    sym.mirrorX = true;
    // Library (0, 3.81) -> sheet (0, -3.81) -> rotate 90 ccw -> (-3.81, 0) -> mirror x keeps it.
    CHECK(test::near(toSheet(sym, {0, 3.81}), Point{10 - 3.81, 10}));
    // Library (2, 0) -> rotate -> (0, -2) -> mirror x -> (0, 2).
    CHECK(test::near(toSheet(sym, {2, 0}), Point{10, 12}));
}

TEST_CASE("units and body styles filter pins") {
    const char* text = R"((kicad_sch (version 20231120)
      (lib_symbols (symbol "x:Dual"
        (symbol "Dual_0_1" (pin power_in line (at 0 5 270) (length 1) (name "V+") (number "8")))
        (symbol "Dual_1_1" (pin input line (at -5 0 0) (length 1) (name "A") (number "1")))
        (symbol "Dual_2_1" (pin input line (at -5 0 0) (length 1) (name "B") (number "5")))
        (symbol "Dual_2_2" (pin input line (at -5 2 0) (length 1) (name "B'") (number "6")))))
      (symbol (lib_id "x:Dual") (at 0 0 0) (unit 2) (property "Reference" "U9" (at 0 0 0)))))";
    auto r = parseSchematic(text);
    REQUIRE(r.ok());
    auto pins = placedPins(r.value(), r->symbols.at(0));
    REQUIRE(pins.size() == 2);
    CHECK(pins[0].pin->number == "8");
    CHECK(pins[1].pin->number == "5");
}

TEST_CASE("instance reference is used when the property is not annotated") {
    auto r = parseSchematic(R"((kicad_sch (lib_symbols)
      (symbol (lib_id "Device:R") (at 0 0 0) (property "Reference" "R?" (at 0 0 0))
        (instances (project "p" (path "/x" (reference "R7") (unit 1)))))))");
    REQUIRE(r.ok());
    CHECK(r->symbols.at(0).reference == "R7");
    CHECK(r->libSymbolFor(r->symbols.at(0)) == nullptr);
    CHECK(placedPins(r.value(), r->symbols.at(0)).empty());
}

TEST_CASE("malformed schematics return errors") {
    struct Case {
        const char* text;
        const char* code;
    };
    const Case cases[] = {
        {"", "parse_error"},
        {"(kicad_sch (wire (pts (xy 0 0) (xy 1 1))", "parse_error"},
        {"(kicad_pcb (version 1))", "invalid_schematic"},
        {"(kicad_sch (wire (pts (xy 0 0))))", "invalid_schematic"},
        {"(kicad_sch (wire (pts (xy 0 zero) (xy 1 1))))", "invalid_schematic"},
        {"(kicad_sch (wire))", "invalid_schematic"},
        {"(kicad_sch (junction))", "invalid_schematic"},
        {"(kicad_sch (junction (at 1e30 0)))", "invalid_schematic"},
        {"(kicad_sch (label (at 0 0 0)))", "invalid_schematic"},
        {"(kicad_sch (symbol (at 0 0 0)))", "invalid_schematic"},
        {"(kicad_sch (symbol (lib_id \"a:b\")))", "invalid_schematic"},
        {"(kicad_sch (symbol (lib_id \"a:b\") (at 0 0 0) (unit -1)))", "invalid_schematic"},
        {"(kicad_sch (lib_symbols (symbol \"a:b\" (pin sideways line (at 0 0 0) (number \"1\")))))", "invalid_schematic"},
        {"(kicad_sch (lib_symbols (symbol \"a:b\" (pin input line (at 0 0 0)))))", "invalid_schematic"},
        {"(kicad_sch (lib_symbols (symbol (pin input line))))", "invalid_schematic"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.text);
        auto r = parseSchematic(c.text);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().code == c.code);
        CHECK_FALSE(r.error().message.empty());
    }
    CHECK(loadSchematic("/nope/missing.kicad_sch").error().code == "not_found");
}

TEST_CASE("truncated fixture never crashes") {
    const std::string& text = test::roverText();
    // Every prefix cut at a line boundary is either an error or (at the very end) a valid schematic.
    int errors = 0;
    for (std::size_t i = 0; i < text.size(); i += 97) {
        auto r = parseSchematic(std::string_view(text).substr(0, i));
        if (!r.ok()) ++errors;
    }
    CHECK(errors > 0);
    CHECK(parseSchematic(text).ok());
}
