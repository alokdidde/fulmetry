// WickedViewport runs the same conformance suite as FakeViewport (AGENTS.md rule 4).
// Needs a Vulkan 1.3 device; lavapipe (mesa-vulkan-drivers) is enough.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "conformance/ViewportConformance.h"
#include "fulmetry/project/ProjectIO.h"
#include "fulmetry/viewport/WickedViewport.h"

using namespace fulmetry;

static Project rover() {
    auto p = loadProject(FULMETRY_SOURCE_DIR "/fixtures/rover/fulmetry.project.json");
    REQUIRE(p.ok());
    return p.value();
}

TEST_CASE("WickedViewport conformance") {
    conformance::viewportConformance([] { return std::make_unique<viewport::WickedViewport>(); }, rover());
}

TEST_CASE("WickedViewport high-key render: white background, coloured parts") {
    viewport::WickedViewport v;
    REQUIRE(v.loadProject(rover()).ok());
    v.showObject(ObjectId{"obj:rover"});
    v.setCamera({{300, -300, 250}, {0, 0, 45}, 35});
    auto img = v.renderToImage(160, 100);
    REQUIRE(img.ok());
    const uint8_t* corner = &img->rgba[0];
    CHECK(corner[0] >= 250);  // background is white
    CHECK(corner[1] >= 250);
    CHECK(corner[2] >= 250);
    size_t saturated = 0;  // camera head red, battery yellow, board green ...
    for (size_t i = 0; i < img->rgba.size(); i += 4) {
        int r = img->rgba[i], g = img->rgba[i + 1], b = img->rgba[i + 2];
        if (std::max({r, g, b}) - std::min({r, g, b}) > 80) ++saturated;
    }
    CHECK(saturated > 20);
}
