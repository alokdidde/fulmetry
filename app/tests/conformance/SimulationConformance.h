// Every ISimulation (fake and MuJoCo) must pass this with fixtures/rover/rover.mjcf.
#pragma once
#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <string>

#include "fulmetry/core/ISimulation.h"

namespace fulmetry::conformance {

inline double driveDistanceMm(ISimulation& s, double seconds) {
    s.reset(7);
    s.setDriveInput({1.0, 0.0});
    for (int i = 0; i < int(seconds * 60); ++i) s.step(1.0 / 60);
    return s.bodyPose("rover")->position.x;
}

inline void simulationConformance(const std::function<std::unique_ptr<ISimulation>()>& make,
                                  const std::string& mjcfSmallWheels, const std::string& mjcfBigWheels) {
    SUBCASE("same model, seed and inputs give the same state hash") {
        auto a = make(), b = make();
        REQUIRE(a->loadModel(mjcfSmallWheels).ok());
        REQUIRE(b->loadModel(mjcfSmallWheels).ok());
        driveDistanceMm(*a, 2.0);
        driveDistanceMm(*b, 2.0);
        CHECK(a->stateHash() == b->stateHash());
    }
    SUBCASE("rover drives forward 1 m") {
        auto s = make();
        REQUIRE(s->loadModel(mjcfBigWheels).ok());
        CHECK(driveDistanceMm(*s, 10.0) > 1000.0);
    }
    SUBCASE("bigger wheels cross the rug faster (UI spec rug example)") {
        auto small = make(), big = make();
        REQUIRE(small->loadModel(mjcfSmallWheels).ok());
        REQUIRE(big->loadModel(mjcfBigWheels).ok());
        CHECK(driveDistanceMm(*big, 4.0) > driveDistanceMm(*small, 4.0) * 1.2);
    }
    SUBCASE("garbage model is rejected") {
        auto s = make();
        CHECK_FALSE(s->loadModel("not xml").ok());
    }
}

}  // namespace fulmetry::conformance
