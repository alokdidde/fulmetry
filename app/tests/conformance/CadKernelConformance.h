// Every ICadKernel (fake and OCCT) must pass this on box-shaped parts.
#pragma once
#include <doctest/doctest.h>

#include <functional>
#include <memory>

#include "fulmetry/core/ICadKernel.h"

namespace fulmetry::conformance {

inline void cadKernelConformance(const std::function<std::unique_ptr<ICadKernel>()>& make, const Project& project) {
    auto k = make();
    for (const Part* p : project.allParts()) REQUIRE(k->loadPart(*p, p->stepFile).ok());

    SUBCASE("bounding box matches the manifest within 0.1%") {
        for (const Part* p : project.allParts()) {
            auto bb = k->boundingBox(p->id);
            REQUIRE(bb.ok());
            Vec3 a = bb->size(), b = p->bounds.size();
            CHECK(a.x == doctest::Approx(b.x).epsilon(0.001));
            CHECK(a.y == doctest::Approx(b.y).epsilon(0.001));
            CHECK(a.z == doctest::Approx(b.z).epsilon(0.001));
        }
    }
    SUBCASE("tessellation maps every triangle to a face") {
        const Part* p = project.allParts().front();
        auto m = k->tessellate(p->id, 0.1);
        REQUIRE(m.ok());
        CHECK(m->indices.size() % 3 == 0);
        CHECK(m->triangleFace.size() == m->indices.size() / 3);
        for (auto f : m->triangleFace) CHECK(f < m->faces.size());
    }
    SUBCASE("mass of a box part") {
        const Part* p = project.allParts().front();
        auto mp = k->massProperties(p->id, 1.24);  // PLA g/cm^3
        REQUIRE(mp.ok());
        Vec3 s = p->bounds.size();
        CHECK(mp->volumeMm3 == doctest::Approx(s.x * s.y * s.z).epsilon(0.001));
        CHECK(mp->massG == doctest::Approx(s.x * s.y * s.z / 1000 * 1.24).epsilon(0.001));
    }
    SUBCASE("unknown part is an error, not a crash") {
        CHECK_FALSE(k->boundingBox(PartId("part:nope/nope")).ok());
        CHECK(k->boundingBox(PartId("part:nope/nope")).error().code == "not_found");
    }
}

}  // namespace fulmetry::conformance
