#pragma once
#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "fulmetry/sheets/Schematic.h"

namespace fulmetry::sheets::test {

inline bool near(Point a, Point b) { return std::fabs(a.x - b.x) < 1e-6 && std::fabs(a.y - b.y) < 1e-6; }

inline std::string roverPath() { return std::string(FULMETRY_KICAD_FIXTURE_DIR) + "/rover-main.kicad_sch"; }

inline const std::string& roverText() {
    static const std::string text = [] {
        std::ifstream in(roverPath(), std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }();
    return text;
}

inline const Schematic& rover() {
    static const Schematic sch = [] {
        auto r = loadSchematic(roverPath());
        REQUIRE_MESSAGE(r.ok(), (r.ok() ? std::string() : r.error().message));
        return r.value();
    }();
    return sch;
}

}  // namespace fulmetry::sheets::test
