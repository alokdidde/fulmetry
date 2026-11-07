// MujocoSimulation against the shared SimulationConformance suite, plus MuJoCo-specific checks.
#include <doctest/doctest.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

#include "conformance/SimulationConformance.h"
#include "fakes/FakeSimulation.h"
#include "fulmetry/sim/MujocoSimulation.h"

using namespace fulmetry;

namespace {
std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
// The fixtures as they are on main (owned by S10).
std::string fixture(const std::string& name) {
    return readFile(std::string(FULMETRY_SOURCE_DIR) + "/fixtures/rover/" + name);
}
// S4's proposed replacements (see docs/streams/s4.md, Blockers).
std::string proposed(const std::string& name) {
    return readFile(std::string(FULMETRY_SOURCE_DIR) + "/app/sim/proposed/" + name);
}
std::unique_ptr<ISimulation> loadedXml(const std::string& xml) {
    auto s = sim::makeMujocoSimulation();
    auto st = s->loadModel(xml);
    REQUIRE_MESSAGE(st.ok(), (st.ok() ? "" : st.error().message));
    return s;
}
double yawDeg(const Quat& q) {
    return std::atan2(2 * (q.w * q.z + q.x * q.y), 1 - 2 * (q.y * q.y + q.z * q.z)) * 180 / 3.14159265358979;
}
}  // namespace

TEST_CASE("MujocoSimulation conformance (fixtures/rover)") {
    conformance::simulationConformance([] { return sim::makeMujocoSimulation(); }, fixture("rover-wheels-60.mjcf"),
                                       fixture("rover-wheels-80.mjcf"));
}

TEST_CASE("MujocoSimulation conformance (app/sim/proposed fixtures)") {
    conformance::simulationConformance([] { return sim::makeMujocoSimulation(); }, proposed("rover-wheels-60.mjcf"),
                                       proposed("rover-wheels-80.mjcf"));
}

TEST_CASE("FakeSimulation still passes with the proposed fixtures") {
    conformance::simulationConformance([] { return std::make_unique<fakes::FakeSimulation>(); },
                                       proposed("rover-wheels-60.mjcf"), proposed("rover-wheels-80.mjcf"));
}

// Prints the numbers quoted in docs/streams/s4.md. Asserts rug crossing only for the proposed fixtures: with the
// fixtures on main the caster ball jams on the 8 mm rug edge, and whether it frees itself is chaotic.
TEST_CASE("MujocoSimulation: measured rug run (reported in docs/streams/s4.md)") {
    for (bool useProposed : {false, true}) {
        for (const char* f : {"rover-wheels-60.mjcf", "rover-wheels-80.mjcf"}) {
            auto s = loadedXml(useProposed ? proposed(f) : fixture(f));
            double d4 = conformance::driveDistanceMm(*s, 4.0);
            double d10 = conformance::driveDistanceMm(*s, 10.0);
            // time to cross the rug (x from 0.3 m to 1.5 m)
            s->reset(7);
            s->setDriveInput({1.0, 0.0});
            double tIn = -1, tOut = -1;
            for (int i = 0; i < 600 && tOut < 0; ++i) {
                s->step(1.0 / 60);
                double x = s->bodyPose("rover")->position.x;
                if (tIn < 0 && x >= 300) tIn = s->time();
                if (tIn >= 0 && x >= 1500) tOut = s->time();
            }
            const std::string label = std::string(useProposed ? "proposed " : "main ") + f;
            MESSAGE(label << ": 4 s -> " << d4 << " mm, 10 s -> " << d10 << " mm, rug crossing "
                          << (tOut > 0 ? tOut - tIn : -1.0) << " s");
            if (useProposed) CHECK(tOut > 0);
        }
    }
}

TEST_CASE("MujocoSimulation: reset restores the initial state") {
    auto s = loadedXml(fixture("rover-wheels-60.mjcf"));
    s->reset(3);
    auto h0 = s->stateHash();
    auto p0 = s->bodyPose("rover").value();
    s->setDriveInput({1.0, 0.3});
    for (int i = 0; i < 60; ++i) s->step(1.0 / 60);
    CHECK(s->stateHash() != h0);
    s->reset(3);
    CHECK(s->stateHash() == h0);
    CHECK(s->time() == 0.0);
    CHECK(s->bodyPose("rover").value() == p0);
    CHECK(p0.position.z == doctest::Approx(30.0));  // fixture places the chassis at 0.03 m
    s->reset(4);
    CHECK(s->stateHash() != h0);  // the seed is part of the hash
}

TEST_CASE("MujocoSimulation: fixed substeps") {
    auto s = loadedXml(fixture("rover-wheels-60.mjcf"));
    for (int i = 0; i < 60; ++i) s->step(1.0 / 60);  // 1/60 s is not a multiple of the 2 ms timestep
    CHECK(s->time() == doctest::Approx(1.0).epsilon(0.003));
    auto t = loadedXml(fixture("rover-wheels-60.mjcf"));
    t->step(1.0);
    CHECK(t->time() == doctest::Approx(s->time()).epsilon(0.003));
}

TEST_CASE("MujocoSimulation: steering and reverse") {
    auto s = loadedXml(proposed("rover-wheels-80.mjcf"));
    s->setDriveInput({0.5, 0.5});  // left wheel stopped, right wheel forward: turns left
    for (int i = 0; i < 15; ++i) s->step(1.0 / 60);
    CHECK(yawDeg(s->bodyPose("rover")->rotation) > 10.0);
    CHECK(s->bodyPose("rover")->position.y > 0.0);
    s->reset(0);
    s->setDriveInput({-1.0, 0.0});
    for (int i = 0; i < 60; ++i) s->step(1.0 / 60);
    CHECK(s->bodyPose("rover")->position.x < -50.0);
    CHECK(s->telemetry()["speed_mps"] < -0.1);
}

TEST_CASE("MujocoSimulation: telemetry") {
    auto s = loadedXml(proposed("rover-wheels-60.mjcf"));
    auto t0 = s->telemetry();
    for (const char* k : {"speed_mps", "on_rug", "current_a", "battery_pct", "rssi_dbm"}) CHECK(t0.count(k) == 1);
    CHECK(t0["on_rug"] == 0.0);
    CHECK(t0["battery_pct"] == 100.0);
    s->setDriveInput({1.0, 0.0});
    bool sawRug = false;
    double peakCurrent = 0;
    for (int i = 0; i < 240; ++i) {
        s->step(1.0 / 60);
        auto t = s->telemetry();
        sawRug = sawRug || t["on_rug"] > 0;
        peakCurrent = std::max(peakCurrent, t["current_a"]);
    }
    auto t1 = s->telemetry();
    CHECK(sawRug);
    CHECK(t1["speed_mps"] == doctest::Approx(0.5).epsilon(0.1));  // 160 rpm on Ø60 mm
    CHECK(peakCurrent > t0["current_a"]);
    CHECK(t1["battery_pct"] < 100.0);
    CHECK(t1["battery_pct"] > 99.0);
    CHECK(t1["rssi_dbm"] < t0["rssi_dbm"]);
}

TEST_CASE("MujocoSimulation: errors") {
    auto s = sim::makeMujocoSimulation();
    CHECK(s->bodyPose("rover").error().code == "not_loaded");
    auto bad = s->loadModel("<mujoco><worldbody><geom type=\"nonsense\"/></worldbody></mujoco>");
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().code == "invalid_model");
    REQUIRE(s->loadModel(fixture("rover-wheels-60.mjcf")).ok());
    CHECK(s->bodyPose("no-such-body").error().code == "not_found");
    s->setControl("no-such-actuator", 1.0);  // ignored
    s->setControl("drive_l", 5.0);
    s->step(0.5);
    CHECK(s->bodyPose("rover")->position.x != 0.0);
}
