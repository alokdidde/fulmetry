// WickedViewport runs the same conformance suite as FakeViewport (AGENTS.md rule 4), with box parts and with the
// stand-in meshes. Needs a Vulkan 1.3 device; lavapipe (mesa-vulkan-drivers) is enough.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

#include "conformance/ViewportConformance.h"
#include "fulmetry/project/ProjectIO.h"
#include "fulmetry/viewport/WickedViewport.h"

using namespace fulmetry;
using viewport::WickedOptions;
using viewport::WickedViewport;

static Project rover() {
    auto p = loadProject(FULMETRY_SOURCE_DIR "/fixtures/rover/fulmetry.project.json");
    REQUIRE(p.ok());
    return p.value();
}

// The rover with the stand-in meshes from app/viewport/spike/testmeshes, as the spike loads them.
static Project roverWithMeshes(bool fasteners = true) {
    Project p = rover();
    for (auto& o : p.objects)
        for (auto& part : o.parts) {
            std::string slug = part.id.value.substr(part.id.value.rfind('/') + 1);
            std::string file = std::string(FULMETRY_TESTMESHES) + "/" + slug + ".glb";
            if (o.id.value == "obj:rover" && std::filesystem::exists(file) && (fasteners || slug != "fasteners"))
                part.meshFile = file;
        }
    return p;
}

static WickedOptions fast() {
    WickedOptions o;
    o.frames = 1;
    return o;
}

static bool saturated(const Image& img, int x, int y) {
    const uint8_t* px = &img.rgba[(size_t(y) * img.width + x) * 4];
    return std::max({px[0], px[1], px[2]}) - std::min({px[0], px[1], px[2]}) > 80;
}

TEST_CASE("WickedViewport conformance (boxes)") {
    conformance::viewportConformance([] { return std::make_unique<WickedViewport>(fast()); }, rover());
}

TEST_CASE("WickedViewport conformance (meshes)") {
    // The fasteners mesh is eight screws: the centre of its bounds is empty, so the suite's "pick the bounds
    // centre" cannot hit it. That is the suite's box assumption, not a picking fault (see docs/streams/s2.md).
    conformance::viewportConformance([] { return std::make_unique<WickedViewport>(fast()); }, roverWithMeshes(false));
}

TEST_CASE("meshes load in place of boxes, broken ones fall back to boxes") {
    Project p = roverWithMeshes();
    p.objects[0].parts[0].meshFile = "missing/chassis.glb";
    WickedViewport v(fast());
    REQUIRE(v.loadProject(p).ok());
    CHECK(v.meshFileCount() == 12);
    REQUIRE(v.meshWarnings().size() == 1);
    CHECK(v.meshWarnings()[0].find("chassis-tub") != std::string::npos);
}

TEST_CASE("picking hits the mesh, not its bounds") {
    WickedViewport v(fast());
    REQUIRE(v.loadProject(roverWithMeshes()).ok());
    v.resize(640, 400);
    const PartId wheel{"part:rover/wheel-l"};
    v.isolate(wheel);
    v.flyTo(wheel);
    const Vec3 at = v.drawnPlacement(wheel).position;
    // Corner of the wheel's bounds on its outer face: inside the box, outside the round wheel.
    auto corner = v.project(at + Vec3{27, 7, 27});
    REQUIRE(corner);
    CHECK_FALSE(v.pick(corner->first, corner->second).has_value());
    // The tyre at 45 degrees, just inside the tread.
    auto tyre = v.project(at + Vec3{19.5, 0, 19.5});
    REQUIRE(tyre);
    auto hit = v.pick(tyre->first, tyre->second);
    REQUIRE(hit);
    CHECK(hit->part == wheel);
    CHECK((hit->point - at).length() > 20.0);  // on the tread, well outside the hub
}

TEST_CASE("rotated parts are drawn and picked with their rotation") {
    Project p = rover();
    const PartId battery{"part:rover/battery"};
    for (auto& part : p.objects[0].parts)
        if (part.id == battery) part.placement.rotation = Quat{std::sqrt(0.5), 0, 0, std::sqrt(0.5)};  // 90 deg about z
    WickedViewport v(fast());
    REQUIRE(v.loadProject(p).ok());
    v.resize(320, 200);
    v.isolate(battery);
    v.flyTo(battery);
    const Vec3 at = v.drawnPlacement(battery).position;
    // The 70 x 35 mm pack now runs along y: (0, 30) is on it, (30, 0) is not.
    auto on = v.project(at + Vec3{0, 30, 0}), off = v.project(at + Vec3{30, 0, 0});
    REQUIRE(on);
    REQUIRE(off);
    auto hit = v.pick(on->first, on->second);
    REQUIRE(hit);
    CHECK(hit->part == battery);
    CHECK_FALSE(v.pick(off->first, off->second).has_value());
    auto img = v.renderToImage(320, 200);
    REQUIRE(img.ok());
    CHECK(saturated(img.value(), on->first, on->second));  // yellow where the pick says the battery is
    CHECK_FALSE(saturated(img.value(), off->first, off->second));
}

TEST_CASE("high-key render: white background, coloured parts, red selection outline") {
    WickedViewport v(fast());
    REQUIRE(v.loadProject(roverWithMeshes()).ok());
    v.showObject(ObjectId{"obj:rover"});
    v.resize(320, 200);
    v.resetView();
    v.setSelected(PartId{"part:rover/wheel-l"});
    auto img = v.renderToImage(320, 200);
    REQUIRE(img.ok());
    for (auto [x, y] : {std::pair{0, 0}, std::pair{319, 0}, std::pair{0, 199}, std::pair{319, 199}}) {
        const uint8_t* px = &img->rgba[(size_t(y) * 320 + x) * 4];
        CHECK(px[0] >= 252);  // pure white stage, no sky band or AO halo at the edges
        CHECK(px[1] >= 252);
        CHECK(px[2] >= 252);
    }
    size_t red = 0, grey = 0;
    for (size_t i = 0; i < img->rgba.size(); i += 4) {
        int r = img->rgba[i], g = img->rgba[i + 1], b = img->rgba[i + 2];
        if (r > 180 && g < 110 && b < 100) ++red;                                  // camera head and selection
        if (r > 150 && r < 235 && std::abs(r - g) < 8 && std::abs(g - b) < 8) ++grey;  // shading and shadow
    }
    CHECK(red > 200);
    CHECK(grey > 500);
    // The wheel is black: its red outline means red pixels next to the tyre, beyond the camera head's.
    WickedViewport plain(fast());
    REQUIRE(plain.loadProject(roverWithMeshes()).ok());
    plain.showObject(ObjectId{"obj:rover"});
    plain.resize(320, 200);
    plain.resetView();
    auto noSel = plain.renderToImage(320, 200);
    REQUIRE(noSel.ok());
    size_t redPlain = 0;
    for (size_t i = 0; i < noSel->rgba.size(); i += 4)
        if (noSel->rgba[i] > 180 && noSel->rgba[i + 1] < 110 && noSel->rgba[i + 2] < 100) ++redPlain;
    CHECK(red > redPlain + 100);
}
