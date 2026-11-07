// Spike: render one offscreen frame of a project's parts as boxes with Wicked Engine and write a PNG.
//   fulmetry_wicked_spike <project.json> <out.png> [width height [explode 0..1]]
// Runs headless: Vulkan device without a window (lavapipe works), no swapchain.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "fulmetry/project/ProjectIO.h"
#include "fulmetry/viewport/WickedViewport.h"

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <project.json> <out.png> [width height [explode]]\n", argv[0]);
        return 2;
    }
    int w = argc > 4 ? std::atoi(argv[3]) : 960;
    int h = argc > 4 ? std::atoi(argv[4]) : 600;

    auto project = fulmetry::loadProject(argv[1]);
    if (!project.ok()) {
        std::fprintf(stderr, "load failed: %s\n", project.error().message.c_str());
        return 1;
    }
    fulmetry::viewport::WickedViewport stage;
    if (auto st = stage.loadProject(project.value()); !st.ok()) return 1;
    stage.showObject(project->objects.front().id);  // the rover alone
    stage.setCamera({{300, -300, 250}, {0, 0, 45}, 35});
    if (argc > 5) {
        stage.setExplode(std::atof(argv[5]));
        stage.setCamera({{420, -420, 380}, {0, 0, 45}, 35});
    }

    auto img = stage.renderToImage(w, h);  // first call: device, shader compile, scene build
    if (!img.ok()) {
        std::fprintf(stderr, "render failed: %s: %s\n", img.error().code.c_str(), img.error().message.c_str());
        return 1;
    }
    std::printf("first render (init + shaders + %dx%d): %.1f ms\n", w, h, stage.lastRenderMs());
    img = stage.renderToImage(w, h);  // warm: the steady-state cost of one renderToImage
    if (!img.ok()) return 1;
    std::printf("warm render: %.1f ms for 4 frames + readback\n", stage.lastRenderMs());

    if (auto st = fulmetry::viewport::writePng(img.value(), argv[2]); !st.ok()) {
        std::fprintf(stderr, "%s\n", st.error().message.c_str());
        return 1;
    }
    std::printf("wrote %s\n", argv[2]);
    return 0;
}
