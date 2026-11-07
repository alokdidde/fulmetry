#include <doctest/doctest.h>

#include <cstdio>
#include <string>

#include "conformance/CadKernelConformance.h"
#include "fulmetry/cad/OcctCadKernel.h"
#include "unit/TestSupport.h"

using namespace fulmetry;
using cad::OcctCadKernel;

namespace {
Part boxPart(const std::string& id, Vec3 min, Vec3 max, Vec3 position = {}) {
    Part p;
    p.id = PartId(id);
    p.bounds = {min, max};
    p.placement.position = position;
    return p;
}
double perimeter(const Polyline& l) {
    double s = 0;
    for (size_t i = 1; i < l.size(); ++i) s += (l[i] - l[i - 1]).length();
    return s;
}
}  // namespace

TEST_CASE("OcctCadKernel conformance on the rover fixture") {
    conformance::cadKernelConformance([] { return std::make_unique<OcctCadKernel>(); }, test::rover());
}

TEST_CASE("a box written to STEP loads back with the same volume and bounds") {
    const std::string path = "occt_roundtrip_box.step";
    std::remove(path.c_str());
    {
        OcctCadKernel k;
        REQUIRE(k.loadPart(boxPart("part:t/box", {-5, -10, 0}, {5, 10, 30}), "").ok());
        REQUIRE(k.writeStep(PartId("part:t/box"), path).ok());
    }
    OcctCadKernel k;
    Part p = boxPart("part:t/box", {}, {});  // bounds unused when a STEP path is given
    REQUIRE(k.loadPart(p, path).ok());
    auto mp = k.massProperties(p.id, 1.24);
    REQUIRE(mp.ok());
    CHECK(mp->volumeMm3 == doctest::Approx(10.0 * 20 * 30).epsilon(1e-6));
    CHECK(mp->massG == doctest::Approx(6.0 * 1.24).epsilon(1e-6));
    CHECK(mp->centroid.z == doctest::Approx(15));
    auto bb = k.boundingBox(p.id);
    REQUIRE(bb.ok());
    CHECK(bb->min.x == doctest::Approx(-5));
    CHECK(bb->max.y == doctest::Approx(10));
    CHECK(bb->max.z == doctest::Approx(30));
    std::remove(path.c_str());
}

TEST_CASE("loadPart reports errors instead of throwing") {
    OcctCadKernel k;
    CHECK(k.loadPart(boxPart("part:t/a", {}, {}), "does/not/exist.step").error().code == "io_error");
    CHECK(k.loadPart(boxPart("part:t/a", {0, 0, 0}, {1, 0, 1}), "").error().code == "invalid_bounds");
    CHECK(k.tessellate(PartId("part:t/a"), 0.1).error().code == "not_found");
}

TEST_CASE("tessellation of a box: six faces with stable ids and outward normals") {
    OcctCadKernel k;
    Part p = boxPart("part:t/box", {0, 0, 0}, {10, 20, 30});
    REQUIRE(k.loadPart(p, "").ok());
    auto m = k.tessellate(p.id, 0.1);
    REQUIRE(m.ok());
    REQUIRE(m->faces.size() == 6);
    for (size_t i = 0; i < 6; ++i) CHECK(m->faces[i].value == "part:t/box#" + std::to_string(i));
    CHECK(m->positions.size() == m->normals.size());
    CHECK(m->triangleFace.size() == m->indices.size() / 3);
    // Same ids on a second, finer pass.
    auto fine = k.tessellate(p.id, 0.01);
    REQUIRE(fine.ok());
    CHECK(fine->faces == m->faces);

    Vec3 c{5, 10, 15};
    double area = 0;
    for (size_t t = 0; t < m->triangleFace.size(); ++t) {
        auto v = [&](size_t k3) {
            uint32_t i = m->indices[t * 3 + k3];
            return Vec3{m->positions[i * 3], m->positions[i * 3 + 1], m->positions[i * 3 + 2]};
        };
        uint32_t i0 = m->indices[t * 3];
        Vec3 n{m->normals[i0 * 3], m->normals[i0 * 3 + 1], m->normals[i0 * 3 + 2]};
        Vec3 a = v(0), b = v(1), d = v(2);
        Vec3 e1 = b - a, e2 = d - a;
        Vec3 cr{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        area += cr.length() / 2;
        CHECK(n.length() == doctest::Approx(1).epsilon(1e-5));
        CHECK(cr.dot(n) > 0);           // winding agrees with the normal
        CHECK((a - c).dot(n) > 0);      // normal points out of the box
    }
    CHECK(area == doctest::Approx(2 * (10.0 * 20 + 20.0 * 30 + 10.0 * 30)).epsilon(1e-6));
}

TEST_CASE("minDistance uses placement and is zero when touching or contained") {
    OcctCadKernel k;
    REQUIRE(k.loadPart(boxPart("part:t/a", {0, 0, 0}, {10, 10, 10}), "").ok());
    REQUIRE(k.loadPart(boxPart("part:t/b", {0, 0, 0}, {10, 10, 10}, {13, 4, 0}), "").ok());
    REQUIRE(k.loadPart(boxPart("part:t/c", {0, 0, 0}, {10, 10, 10}, {10, 0, 0}), "").ok());
    REQUIRE(k.loadPart(boxPart("part:t/d", {2, 2, 2}, {4, 4, 4}), "").ok());
    REQUIRE(k.loadPart(boxPart("part:t/e", {0, 0, 0}, {1, 1, 1}, {13, 13, 14}), "").ok());
    CHECK(k.minDistance(PartId("part:t/a"), PartId("part:t/b")).value() == doctest::Approx(3));
    CHECK(k.minDistance(PartId("part:t/a"), PartId("part:t/c")).value() == doctest::Approx(0));
    CHECK(k.minDistance(PartId("part:t/a"), PartId("part:t/d")).value() == doctest::Approx(0));
    CHECK(k.minDistance(PartId("part:t/a"), PartId("part:t/e")).value() == doctest::Approx(std::sqrt(9.0 + 9 + 16)));
    CHECK(k.minDistance(PartId("part:t/a"), PartId("part:t/nope")).error().code == "not_found");
}

TEST_CASE("section cuts closed polylines") {
    OcctCadKernel k;
    Part p = boxPart("part:t/box", {-10, -5, 0}, {10, 5, 20});
    REQUIRE(k.loadPart(p, "").ok());

    auto mid = k.section(p.id, Plane{{0, 0, 7}, {0, 0, 1}});
    REQUIRE(mid.ok());
    REQUIRE(mid->size() == 1);
    const Polyline& l = mid->front();
    CHECK(l.front() == l.back());
    CHECK(perimeter(l) == doctest::Approx(60));
    for (const Vec3& v : l) CHECK(v.z == doctest::Approx(7));

    auto above = k.section(p.id, Plane{{0, 0, 25}, {0, 0, 1}});
    REQUIRE(above.ok());
    CHECK(above->empty());

    auto vertical = k.section(p.id, Plane{{0, 0, 0}, {1, 0, 0}});
    REQUIRE(vertical.ok());
    REQUIRE(vertical->size() == 1);
    CHECK(perimeter(vertical->front()) == doctest::Approx(2 * (10 + 20)));
}
