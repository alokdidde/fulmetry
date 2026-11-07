// fulmetry_workbench: the M1 workbench shell (Raven GUI) on the real project model and a fake agent back end.
//
//   fulmetry_workbench [--project fulmetry.project.json] [--chat sample-chat.json] [--assets app/ui/assets]
//                      [--size 1440x900] [--chat-left] [--open tray|menu] [--select <part id>]
//                      [--frames N] [--run-seconds S] [--shot out.png] [--headless] [--strict]
//
// --headless uses Raven's null platform and backend (no display, no GPU): the ctest runs it that way.
// --strict exits with 2 when Raven reported any warning or error (unknown XML tag or attribute, missing file...).
#include <Raven.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <stb/stb_image_write.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "WorkbenchView.h"
#include "fakes/FakeAgentClient.h"
#include "fulmetry/project/ProjectIO.h"

#ifndef FULMETRY_UI_ASSETS
#define FULMETRY_UI_ASSETS "app/ui/assets"
#endif
#ifndef FULMETRY_SOURCE_DIR
#define FULMETRY_SOURCE_DIR "."
#endif

namespace {

struct Options {
    std::string assets = FULMETRY_UI_ASSETS;
    std::string project = FULMETRY_SOURCE_DIR "/fixtures/rover/fulmetry.project.json";
    std::string chat;
    std::string stage;
    std::string open;
    std::string select;
    std::string shot;
    int width = 0, height = 0;
    int frames = 0;          // quit after this many frames (0: run until closed or --run-seconds)
    double runSeconds = 0;
    bool chatLeft = false, headless = false, strict = false;
};

int g_reports = 0;

void onError(RError code, RSeverity severity, const char* detail) {
    if (severity >= R_SEVERITY_WARNING) ++g_reports;
    std::fprintf(stderr, "[raven] %s (%s): %s\n", Rerror_to_string(code), Rseverity_to_string(severity),
                 detail ? detail : "");
}

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--assets") o.assets = next();
        else if (a == "--project") o.project = next();
        else if (a == "--chat") o.chat = next();
        else if (a == "--stage") o.stage = next();
        else if (a == "--open") o.open = next();
        else if (a == "--select") o.select = next();
        else if (a == "--shot") o.shot = next();
        else if (a == "--frames") o.frames = std::atoi(next().c_str());
        else if (a == "--run-seconds") o.runSeconds = std::atof(next().c_str());
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height);
        else if (a == "--chat-left") o.chatLeft = true;
        else if (a == "--headless") o.headless = true;
        else if (a == "--strict") o.strict = true;
        else std::fprintf(stderr, "unknown argument %s\n", a.c_str());
    }
    if (o.chat.empty()) o.chat = o.assets + "/sample-chat.json";
    if (o.stage.empty()) o.stage = o.assets + "/stage/rover-highkey.png";
    return o;
}

bool saveShot(Rid window, const std::string& path) {
    int w = 0, h = 0;
    uint8_t* px = Rwindow_capture(window, &w, &h);
    if (!px) return false;
    const int ok = stbi_write_png(path.c_str(), w, h, 4, px, w * 4);
    Rpixels_free(px);
    return ok != 0;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace fulmetry;
    const Options o = parse(argc, argv);
    Rerror_set_callback(onError);

    auto project = loadProject(o.project);
    if (!project.ok()) {
        std::fprintf(stderr, "cannot load %s: %s\n", o.project.c_str(), project.error().message.c_str());
        return 1;
    }
    ui::WorkbenchModel model(project.value());
    if (auto st = model.loadChat(slurp(o.chat)); !st.ok())
        std::fprintf(stderr, "chat %s: %s\n", o.chat.c_str(), st.error().message.c_str());
    if (!o.select.empty() && !model.selectPart(PartId(o.select)))
        std::fprintf(stderr, "no part %s\n", o.select.c_str());

    fakes::FakeAgentClient agent(project.value());
    agent.connect("fake://workbench");
    EventBus bus;

    RInitDesc desc{};
    desc.backend = o.headless ? R_BACKEND_NULL : R_BACKEND_VULKAN;
    desc.headless = o.headless ? R_TRUE : R_FALSE;
    desc.app_name = "fulmetry-workbench";
    if (!Rinit_ex(&desc)) {
        std::fprintf(stderr, "Rinit_ex failed\n");
        return 1;
    }
    {
        ui::WorkbenchView::Options vo;
        vo.assets = o.assets;
        vo.chatLeft = o.chatLeft;
        ui::WorkbenchView view(model, &agent, bus, vo);
        if (!view.build()) {
            Rshutdown();
            return 1;
        }
        if (!view.stage().loadFile(o.stage)) std::fprintf(stderr, "cannot load stage image %s\n", o.stage.c_str());
        // Pins for the stage image: where each anchored part lands in that frame.
        const std::string sidecar = o.stage.substr(0, o.stage.find_last_of('.')) + ".json";
        auto j = nlohmann::json::parse(slurp(sidecar), nullptr, false);
        if (!j.is_discarded() && j.contains("pins"))
            for (auto& [part, xy] : j["pins"].items())
                if (xy.is_array() && xy.size() == 2) view.setPinAnchor(PartId(part), xy[0].get<float>(), xy[1].get<float>());
        if (o.width > 0 && o.height > 0) Rwindow_set_size(view.window(), o.width, o.height);
        if (o.open == "tray") view.setTrayOpen(true);
        if (o.open == "menu") view.setMenuOpen(true);
        std::printf("workbench ready: %s, %zu chat entries\n", model.project().name.c_str(), model.chat().size());
        std::fflush(stdout);

        const auto start = std::chrono::steady_clock::now();
        int frame = 0;
        while (o.headless ? Rdraw_frame() : Rwait_frame(0.05)) {
            const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            view.update(t);
            ++frame;
            if (frame == 20) view.reportLayout();
            if (o.frames > 0 && frame >= o.frames) break;
            if (o.runSeconds > 0 && t >= o.runSeconds) break;
        }
        if (!o.shot.empty()) {
            if (saveShot(view.window(), o.shot)) std::printf("screenshot: %s\n", o.shot.c_str());
            else std::fprintf(stderr, "screenshot failed (backend cannot capture)\n");
        }
        std::printf("frames: %d, raven reports: %d, chat entries: %zu, selection: %s\n", frame, g_reports,
                    model.chat().size(), model.selectedPart() ? model.selectedPart()->value.c_str() : "none");
    }
    Rshutdown();
    return o.strict && g_reports > 0 ? 2 : 0;
}
