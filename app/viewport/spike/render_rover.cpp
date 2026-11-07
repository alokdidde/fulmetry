// Renders a project's first object offscreen with Wicked Engine and writes a PNG.
//   fulmetry_wicked_spike <project.json> <out.png> [width height] [options]
//     --meshes <dir>     parts without a meshFile use <dir>/<slug>.glb when it exists (stand-in meshes)
//     --explode <0..1>   --select <part id>   --hover <part id>   --xray   --frames <n>   --no-warm
//     --camera ex,ey,ez,tx,ty,tz   eye and target in mm (30 degree field of view) instead of the default view
// Runs headless: Vulkan device without a window (lavapipe works), no swapchain. Prints frame times.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>

#include "fulmetry/project/ProjectIO.h"
#include "fulmetry/viewport/WickedViewport.h"

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <project.json> <out.png> [width height] [--meshes dir] [--explode x] "
                             "[--select id] [--hover id] [--xray] [--frames n] [--no-warm]\n", argv[0]);
        return 2;
    }
    int w = 960, h = 600, argi = 3;
    if (argc > 4 && argv[3][0] != '-') {
        w = std::atoi(argv[3]);
        h = std::atoi(argv[4]);
        argi = 5;
    }
    std::string meshDir, select, hover;
    std::optional<fulmetry::CameraState> camera;
    double explode = 0;
    bool xray = false, warm = true;
    fulmetry::viewport::WickedOptions opt;
    for (int i = argi; i < argc; ++i) {
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if (!std::strcmp(argv[i], "--meshes")) meshDir = next();
        else if (!std::strcmp(argv[i], "--explode")) explode = std::atof(next());
        else if (!std::strcmp(argv[i], "--select")) select = next();
        else if (!std::strcmp(argv[i], "--hover")) hover = next();
        else if (!std::strcmp(argv[i], "--xray")) xray = true;
        else if (!std::strcmp(argv[i], "--frames")) opt.frames = std::atoi(next());
        else if (!std::strcmp(argv[i], "--no-warm")) warm = false;
        else if (!std::strcmp(argv[i], "--camera")) {  // eye x,y,z,target x,y,z (mm)
            fulmetry::CameraState c;
            std::sscanf(next(), "%lf,%lf,%lf,%lf,%lf,%lf", &c.eye.x, &c.eye.y, &c.eye.z, &c.target.x, &c.target.y,
                        &c.target.z);
            c.fovDegrees = 30;
            camera = c;
        }
        else {
            std::fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        }
    }

    auto project = fulmetry::loadProject(argv[1]);
    if (!project.ok()) {
        std::fprintf(stderr, "load failed: %s\n", project.error().message.c_str());
        return 1;
    }
    opt.assetRoot = std::filesystem::absolute(argv[1]).parent_path().string();
    if (!meshDir.empty()) {
        for (auto& o : project->objects)
            for (auto& p : o.parts) {
                if (!p.meshFile.empty()) continue;
                std::string slug = p.id.value.substr(p.id.value.rfind('/') + 1);
                auto file = std::filesystem::absolute(std::filesystem::path(meshDir) / (slug + ".glb"));
                if (o.id == project->objects.front().id && std::filesystem::exists(file)) p.meshFile = file.string();
            }
    }

    fulmetry::viewport::WickedViewport stage(opt);
    stage.resize(w, h);
    if (auto st = stage.loadProject(project.value()); !st.ok()) return 1;
    for (const auto& warning : stage.meshWarnings()) std::fprintf(stderr, "warning: %s\n", warning.c_str());
    std::printf("parts drawn from meshes: %d\n", stage.meshFileCount());
    stage.showObject(project->objects.front().id);  // the rover alone
    stage.setExplode(explode);
    stage.resetView();  // frame what is visible, exploded or not
    if (camera) stage.setCamera(*camera);
    if (!select.empty()) stage.setSelected(fulmetry::PartId{select});
    if (!hover.empty()) stage.setHovered(fulmetry::PartId{hover});
    stage.setXray(xray);

    auto img = stage.renderToImage(w, h);  // first call: device, shader compile, scene build
    if (!img.ok()) {
        std::fprintf(stderr, "render failed: %s: %s\n", img.error().code.c_str(), img.error().message.c_str());
        return 1;
    }
    std::printf("first render (init + shaders + %dx%d): %.1f ms\n", w, h, stage.lastRenderMs());
    if (warm) {
        img = stage.renderToImage(w, h);  // warm: the steady-state cost of one renderToImage
        if (!img.ok()) return 1;
        std::printf("warm render: %.1f ms for %d frames + readback\n", stage.lastRenderMs(), opt.frames);
    }

    if (auto st = fulmetry::viewport::writePng(img.value(), argv[2]); !st.ok()) {
        std::fprintf(stderr, "%s\n", st.error().message.c_str());
        return 1;
    }
    std::printf("wrote %s\n", argv[2]);
    return 0;
}
