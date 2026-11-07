// Mesh loading and picking for the viewport. No GPU: runs in every build.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "fulmetry/viewport/MeshAsset.h"

using namespace fulmetry;
using namespace fulmetry::viewport;

namespace {

// Writes a one-triangle-per-face tetrahedron as a .gltf with an embedded base64 buffer.
std::string writeTetra(const std::string& name, const float (&pts)[4][3]) {
    std::vector<unsigned char> bin(4 * 3 * 4 + 12 * 2);
    std::memcpy(bin.data(), pts, 48);
    const uint16_t idx[12] = {0, 2, 1, 0, 1, 3, 0, 3, 2, 1, 2, 3};
    std::memcpy(bin.data() + 48, idx, 24);
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string enc;
    for (size_t i = 0; i < bin.size(); i += 3) {
        uint32_t v = bin[i] << 16 | (i + 1 < bin.size() ? bin[i + 1] << 8 : 0) | (i + 2 < bin.size() ? bin[i + 2] : 0);
        enc += b64[v >> 18 & 63];
        enc += b64[v >> 12 & 63];
        enc += i + 1 < bin.size() ? b64[v >> 6 & 63] : '=';
        enc += i + 2 < bin.size() ? b64[v & 63] : '=';
    }
    float lo[3] = {1e9f, 1e9f, 1e9f}, hi[3] = {-1e9f, -1e9f, -1e9f};
    for (auto& p : pts)
        for (int k = 0; k < 3; ++k) lo[k] = std::min(lo[k], p[k]), hi[k] = std::max(hi[k], p[k]);
    char json[2048];
    std::snprintf(json, sizeof json,
                  R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
                  R"("meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],)"
                  R"("accessors":[{"bufferView":0,"componentType":5126,"count":4,"type":"VEC3",)"
                  R"("min":[%g,%g,%g],"max":[%g,%g,%g]},{"bufferView":1,"componentType":5123,"count":12,"type":"SCALAR"}],)"
                  R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":48},{"buffer":0,"byteOffset":48,"byteLength":24}],)"
                  R"("buffers":[{"byteLength":%zu,"uri":"data:application/octet-stream;base64,%s"}]})",
                  lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], bin.size(), enc.c_str());
    auto path = (std::filesystem::temp_directory_path() / name).string();
    std::ofstream(path) << json;
    return path;
}

}  // namespace

TEST_CASE("box mesh: closed, outward, pickable from every side") {
    MeshAsset b = boxMesh({{-10, -5, -2}, {10, 5, 2}});
    CHECK(b.triangleCount() == 12);
    struct Probe { Vec3 o, d, n; double t; };
    for (const Probe& p : {Probe{{0, 0, 50}, {0, 0, -1}, {0, 0, 1}, 48}, Probe{{50, 0, 0}, {-1, 0, 0}, {1, 0, 0}, 40},
                           Probe{{0, -50, 0}, {0, 1, 0}, {0, -1, 0}, 45}}) {
        auto hit = intersect(b, p.o, p.d);
        REQUIRE(hit);
        CHECK(hit->t == doctest::Approx(p.t));
        CHECK(hit->normal == p.n);
    }
    CHECK_FALSE(intersect(b, {0, 20, 50}, {0, 0, -1}));
    // Winding: every face's geometric normal points away from the centre.
    for (size_t i = 0; i < b.indices.size(); i += 3) {
        auto& a = b.positions[b.indices[i]];
        auto& c1 = b.positions[b.indices[i + 1]];
        auto& c2 = b.positions[b.indices[i + 2]];
        Vec3 e1{c1[0] - a[0], c1[1] - a[1], c1[2] - a[2]}, e2{c2[0] - a[0], c2[1] - a[1], c2[2] - a[2]};
        Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        Vec3 centre{(a[0] + c1[0] + c2[0]) / 3, (a[1] + c1[1] + c2[1]) / 3, (a[2] + c1[2] + c2[2]) / 3};
        CHECK(n.dot(centre) > 0);
    }
}

TEST_CASE("glTF stand-in wheel: metres and Y up become mm and Z up") {
    const Aabb wheelBounds{{-30, -7, -30}, {30, 7, 30}};  // fixture bounds of part:rover/wheel-l
    auto m = loadGltf(FULMETRY_TESTMESHES "/wheel-l.glb", wheelBounds);
    REQUIRE_MESSAGE(m.ok(), m.error().message);
    CHECK(m->units == "m, Y up");
    CHECK(m->triangleCount() > 2000);
    CHECK(m->bounds.max.x == doctest::Approx(29.9).epsilon(0.01));
    CHECK(m->bounds.min.z == doctest::Approx(-29.9).epsilon(0.01));
    CHECK(m->bounds.max.y > 7.0);  // the hub boss stands out on the outer (+y) side
    CHECK(m->bounds.min.y == doctest::Approx(-6.5).epsilon(0.01));
    bool rubber = false, partColour = false;
    for (const auto& s : m->subsets) {
        rubber |= s.material == "rubber" && s.baseColor && s.roughness > 0.8f;
        partColour |= !s.baseColor.has_value();
    }
    CHECK(rubber);
    CHECK_FALSE(partColour);  // every wheel material has its own colour
    // Down onto the tread from above the axle: hits the top of the tyre, normal up.
    auto hit = intersect(m.value(), {0, 0, 100}, {0, 0, -1});
    REQUIRE(hit);
    CHECK(100 - hit->t > 27.5);
    CHECK(hit->normal.z > 0.5);
    // Along the axle into the hub: stops at the hub face, not the far side.
    hit = intersect(m.value(), {0, 100, 0}, {0, -1, 0});
    REQUIRE(hit);
    CHECK(100 - hit->t > 6.0);
    CHECK(hit->normal.y > 0.9);
}

TEST_CASE("glTF in millimetres and Z up is recognised by its extent") {
    const float pts[4][3] = {{0, 0, 0}, {100, 0, 0}, {0, 50, 0}, {0, 0, 20}};
    auto file = writeTetra("fulmetry-tetra-mm.gltf", pts);
    auto m = loadGltf(file, {{0, 0, 0}, {100, 50, 20}});
    REQUIRE_MESSAGE(m.ok(), m.error().message);
    CHECK(m->units == "mm, Z up");
    CHECK(m->bounds.max.x == doctest::Approx(100));
    CHECK(m->bounds.max.y == doctest::Approx(50));
    CHECK(m->bounds.max.z == doctest::Approx(20));
    CHECK(m->normals.size() == m->positions.size());  // computed, the file has none
    // Without an expected size, glTF's own convention applies.
    auto spec = loadGltf(file);
    REQUIRE(spec.ok());
    CHECK(spec->units == "m, Y up");
    CHECK(spec->bounds.max.x == doctest::Approx(100000));
    std::filesystem::remove(file);
}

TEST_CASE("missing or broken files are errors, not crashes") {
    CHECK_FALSE(loadGltf("/nonexistent/part.glb").ok());
    auto path = (std::filesystem::temp_directory_path() / "fulmetry-broken.glb").string();
    std::ofstream(path) << "glTF not really";
    CHECK_FALSE(loadGltf(path).ok());
    std::filesystem::remove(path);
}

TEST_CASE("quaternion rotation") {
    const double h = std::sqrt(0.5);
    Vec3 v = rotate(Quat{h, 0, 0, h}, {1, 0, 0});  // 90 degrees about +z
    CHECK(v.x == doctest::Approx(0).epsilon(1e-12));
    CHECK(v.y == doctest::Approx(1));
    Vec3 back = rotateInverse(Quat{h, 0, 0, h}, v);
    CHECK(back.x == doctest::Approx(1));
}
