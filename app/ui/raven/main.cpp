// fulmetry_workbench: the M1 workbench shell (Raven GUI) on the real project model and a fake agent back end.
//
//   fulmetry_workbench [--project fulmetry.project.json] [--chat sample-chat.json] [--assets app/ui/assets]
//                      [--size 1440x900] [--chat-left] [--open tray|menu] [--select <part id>] [--hover <part id>] [--tab N]
//                      [--explode] [--xray] [--orbit DX,DY] [--zoom N] [--still] [--interactive-scale S]
//                      [--frames N] [--run-seconds S] [--shot out.png] [--headless] [--strict]
//                      [--harnesses fresh|some|all] [--open onboard|picker] [--onboard <harness id>] [--do <command>]...
//                      [--simulate [setup.json]] [--sim-seconds T] [--sim-drive THROTTLE,STEER]
//
// Built with FULMETRY_WITH_WICKED, the stage is the live Wicked render of the project (LiveStage); --still
// shows the pre-rendered image instead, which is all a build without Wicked has. --shot waits for the stage's
// resting (soft-shadow) render. --orbit drags the camera by DX,DY stage units and --zoom turns the wheel N
// notches (positive: closer) before that.
// --headless uses Raven's null platform and backend (no display, no GPU): the ctest runs it that way.
// --strict exits with 2 when Raven reported any warning or error (unknown XML tag or attribute, missing file...).
// --harnesses sets which harnesses the fake back end starts with ready (fresh: none, the first-run onboarding).
// --do runs a harness command once the window is up, as its button would (WorkbenchView::harnessCommand), e.g.
// --do signin:codex. The chosen harness and model are kept in settings.json under FULMETRY_CONFIG_DIR, else
// $XDG_CONFIG_HOME/fulmetry or ~/.config/fulmetry (%APPDATA%\Fulmetry on Windows).
// Simulate (the button, or --simulate at start) drives the object in a room: fixtures/rover/sim/living-room.json by
// default, on MuJoCo when this build has it (FULMETRY_WITH_MUJOCO), else on the kinematic fake. --sim-seconds steps
// the physics to that time as fast as it can and holds there, for screenshots; --sim-drive replaces the scripted
// drive with that input.
#include <Raven.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC  // Wicked links its own copy of stb_image_write
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <stb/stb_image_write.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "LiveStage.h"
#include "SimulateMode.h"
#include "WorkbenchView.h"
#include "fakes/FakeAgentClient.h"
#include "fakes/FakeSimulation.h"
#include "fulmetry/project/ProjectIO.h"
#if defined(FULMETRY_HAS_WICKED)
#include "fulmetry/viewport/WickedViewport.h"
#endif
#if defined(FULMETRY_HAS_MUJOCO)
#include "fulmetry/sim/MujocoSimulation.h"
#endif

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
    std::string select, hover;
    std::string shot;
    std::string harnesses = "some";
    std::string onboard;
    std::vector<std::string> commands;
    std::string simSetup = FULMETRY_SOURCE_DIR "/fixtures/rover/sim/living-room.json";
    bool simulate = false;
    double simSeconds = 0;
    bool simDrive = false;
    double simThrottle = 0, simSteer = 0;
    int width = 0, height = 0;
    int frames = 0;          // quit after this many frames (0: run until closed or --run-seconds)
    double runSeconds = 0;
    int tab = -1;
    float orbitX = 0, orbitY = 0, zoom = 0;
    float interactiveScale = 1;
    bool chatLeft = false, headless = false, strict = false;
    bool still = false, explode = false, xray = false;
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
        else if (a == "--hover") o.hover = next();
        else if (a == "--shot") o.shot = next();
        else if (a == "--harnesses") o.harnesses = next();
        else if (a == "--onboard") o.onboard = next();
        else if (a == "--do") o.commands.push_back(next());
        else if (a == "--simulate") {
            o.simulate = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') o.simSetup = next();
        } else if (a == "--sim-seconds") o.simSeconds = std::atof(next().c_str());
        else if (a == "--sim-drive") {
            o.simDrive = std::sscanf(next().c_str(), "%lf,%lf", &o.simThrottle, &o.simSteer) >= 1;
        } else if (a == "--frames") o.frames = std::atoi(next().c_str());
        else if (a == "--run-seconds") o.runSeconds = std::atof(next().c_str());
        else if (a == "--size") std::sscanf(next().c_str(), "%dx%d", &o.width, &o.height);
        else if (a == "--chat-left") o.chatLeft = true;
        else if (a == "--tab") o.tab = std::atoi(next().c_str());
        else if (a == "--orbit") std::sscanf(next().c_str(), "%f,%f", &o.orbitX, &o.orbitY);
        else if (a == "--zoom") o.zoom = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--interactive-scale") o.interactiveScale = static_cast<float>(std::atof(next().c_str()));
        else if (a == "--still") o.still = true;
        else if (a == "--explode") o.explode = true;
        else if (a == "--xray") o.xray = true;
        else if (a == "--headless") o.headless = true;
        else if (a == "--strict") o.strict = true;
        else std::fprintf(stderr, "unknown argument %s\n", a.c_str());
    }
    if (o.chat.empty()) o.chat = o.assets + "/sample-chat.json";
    if (o.stage.empty()) o.stage = o.assets + "/stage/rover-highkey.png";
    return o;
}

// Where the harness choice is kept between runs.
std::filesystem::path settingsPath() {
    namespace fs = std::filesystem;
    if (const char* d = std::getenv("FULMETRY_CONFIG_DIR"); d && *d) return fs::path(d) / "settings.json";
#ifdef _WIN32
    if (const char* d = std::getenv("APPDATA"); d && *d) return fs::path(d) / "Fulmetry" / "settings.json";
#else
    if (const char* d = std::getenv("XDG_CONFIG_HOME"); d && *d) return fs::path(d) / "fulmetry" / "settings.json";
    if (const char* d = std::getenv("HOME"); d && *d) return fs::path(d) / ".config" / "fulmetry" / "settings.json";
#endif
    return {};
}

fulmetry::ui::HarnessChoice loadChoice() {
    const auto path = settingsPath();
    if (path.empty()) return {};
    auto j = nlohmann::json::parse(slurp(path.string()), nullptr, false);
    if (j.is_discarded() || !j.is_object()) return {};
    return {j.value("harness", ""), j.value("model", "")};
}

void saveChoice(const fulmetry::ui::HarnessChoice& c) {
    const auto path = settingsPath();
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    auto j = nlohmann::json::parse(slurp(path.string()), nullptr, false);
    if (j.is_discarded() || !j.is_object()) j = nlohmann::json::object();
    j["harness"] = c.harness;
    j["model"] = c.model;
    std::ofstream(path, std::ios::binary | std::ios::trunc) << j.dump(2) << "\n";
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
    if (o.tab >= 0) model.selectTab(o.tab);
    if (!o.select.empty() && !model.selectPart(PartId(o.select)))
        std::fprintf(stderr, "no part %s\n", o.select.c_str());
    model.setExploded(o.explode);
    model.setXray(o.xray);

    // The stage: the live render when this build has one, else the still image.
    std::unique_ptr<IViewport> viewport;
    ui::LiveStage::Options lo;
    lo.interactiveScale = o.interactiveScale;
#if defined(FULMETRY_HAS_WICKED)
    if (!o.still) {
        viewport::WickedOptions wo;
        wo.assetRoot = std::filesystem::absolute(o.project).parent_path().string();  // meshFile paths
        auto wicked = std::make_unique<viewport::WickedViewport>(wo);
        if (auto st = wicked->loadProject(project.value()); !st.ok()) {
            std::fprintf(stderr, "stage: %s\n", st.error().message.c_str());
            return 1;
        }
        for (const auto& w : wicked->meshWarnings()) std::fprintf(stderr, "stage: %s\n", w.c_str());
        std::printf("stage: Wicked, %d parts drawn from meshes\n", wicked->meshFileCount());
        lo.idleFrames = wo.frames;
        lo.setFrames = [v = wicked.get()](int n) { v->setSoftShadowFrames(n); };
        lo.setFraming = [v = wicked.get()](double l, double t, double r, double b) { v->setFramingRect(l, t, r, b); };
        viewport = std::move(wicked);
    }
#endif

    const auto start0 = o.harnesses == "fresh" ? fakes::FakeAgentClient::Harnesses::Fresh
                        : o.harnesses == "all"   ? fakes::FakeAgentClient::Harnesses::All
                                                 : fakes::FakeAgentClient::Harnesses::Some;
    fakes::FakeAgentClient agent(project.value(), false, start0);
    model.setPreferredChoice(loadChoice());
    ui::HarnessChoice savedChoice = model.preferredChoice();
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
        std::unique_ptr<ui::LiveStage> live;
        if (viewport) {
            live = std::make_unique<ui::LiveStage>(*viewport, model, view, lo);
            live->attach();
        } else {
            if (!view.stage().loadFile(o.stage)) std::fprintf(stderr, "cannot load stage image %s\n", o.stage.c_str());
            // Pins for the stage image: where each anchored part lands in that frame.
            const std::string sidecar = o.stage.substr(0, o.stage.find_last_of('.')) + ".json";
            auto j = nlohmann::json::parse(slurp(sidecar), nullptr, false);
            if (!j.is_discarded() && j.contains("pins"))
                for (auto& [part, xy] : j["pins"].items())
                    if (xy.is_array() && xy.size() == 2) view.setPinAnchor(PartId(part), xy[0].get<float>(), xy[1].get<float>());
        }
        // Simulate: the room and its physics, for the stage when it is live.
        std::unique_ptr<ui::SimulateMode> sim;
        if (viewport) {
            namespace fs = std::filesystem;
            auto setup = ui::parseSimSetup(slurp(o.simSetup), project.value());
            if (!setup.ok()) {
                std::fprintf(stderr, "simulate: %s: %s\n", o.simSetup.c_str(), setup.error().message.c_str());
            } else {
                ui::SimulateMode::Options so;
                so.mjcf = slurp((fs::path(o.simSetup).parent_path() / setup->model).string());
                so.fixedSeconds = o.simSeconds;
#if defined(FULMETRY_HAS_MUJOCO)
                std::unique_ptr<ISimulation> physics = sim::makeMujocoSimulation();
                std::printf("simulate: MuJoCo\n");
#else
                std::unique_ptr<ISimulation> physics = std::make_unique<fakes::FakeSimulation>();
                std::printf("simulate: kinematic fake (build with FULMETRY_WITH_MUJOCO for the physics)\n");
#endif
                sim = std::make_unique<ui::SimulateMode>(view, live.get(), *viewport, std::move(physics),
                                                         std::move(setup.value()), so);
                sim->attach();
            }
        }
        if (o.width > 0 && o.height > 0) Rwindow_set_size(view.window(), o.width, o.height);
        if (o.open == "tray") view.setTrayOpen(true);
        if (o.open == "menu") view.setMenuOpen(true);
        for (const auto& cmd : o.commands)
            if (!view.harnessCommand(cmd)) std::fprintf(stderr, "harness command %s failed\n", cmd.c_str());
        if (o.open == "onboard" || !o.onboard.empty()) view.setOnboardOpen(true, o.onboard);
        if (o.open == "picker") view.setPickerOpen(true);
        if (o.simulate) {
            if (!sim) std::fprintf(stderr, "simulate: needs the live stage (a build with Wicked, without --still)\n");
            else if (sim->start() && o.simDrive) sim->setDrive(DriveInput{o.simThrottle, o.simSteer});
        }
        std::printf("workbench ready: %s, %zu chat entries\n", model.project().name.c_str(), model.chat().size());
        std::fflush(stdout);

        const auto start = std::chrono::steady_clock::now();
        int frame = 0;
        bool orbited = o.orbitX == 0 && o.orbitY == 0 && o.zoom == 0 && o.hover.empty();
        while (o.headless ? Rdraw_frame() : Rwait_frame(sim && sim->running() ? 1.0 / 120 : 0.05)) {
            const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            view.update(t);
            if (sim) sim->update(t);
            if (!(model.preferredChoice() == savedChoice)) {
                savedChoice = model.preferredChoice();
                saveChoice(savedChoice);
            }
            if (live) {
                live->update(t);
                if (!orbited && live->settled()) {
                    // A scripted drag in steps, as a mouse would send it: quick frames, then one resting render.
                    for (int i = 0; i < 4; ++i) live->orbit(o.orbitX / 4, o.orbitY / 4);
                    if (o.zoom != 0) live->zoom(o.zoom, 360, 450);
                    if (!o.hover.empty()) live->hoverPart(PartId(o.hover));
                    orbited = true;
                }
            }
            ++frame;
            if (frame == 20) view.reportLayout();
            // Screenshots and frame counts wait for the stage's resting render (at most 120 s).
            const bool stageReady = !live || (live->settled() && orbited) || t > 120;
            if (o.frames > 0 && frame >= o.frames && stageReady) break;
            if (o.runSeconds > 0 && t >= o.runSeconds) break;
        }
        if (sim && sim->running()) {
            const auto& d = sim->drive();
            std::printf("simulate: t %.2f s, %.2f m, at %.0f %.0f mm, speed %.2f m/s, on %s, battery %.2f%%\n", d.time(),
                        d.distanceM(), d.position().x, d.position().y, d.speed(), d.surface().c_str(), d.batteryPct());
            for (const auto& f : d.findings())
                std::printf("finding %s (%s): %s\n", f.id.c_str(), f.good ? "good" : "bad", f.text.c_str());
        }
        if (live)
            std::printf("stage frame times: quick %.0f ms, resting %.0f ms\n", live->interactiveMs(), live->idleMs());
        if (!o.shot.empty()) {
            if (saveShot(view.window(), o.shot)) std::printf("screenshot: %s\n", o.shot.c_str());
            else std::fprintf(stderr, "screenshot failed (backend cannot capture)\n");
        }
        std::printf("new agents run on: %s/%s\n", model.choice().harness.c_str(), model.choice().model.c_str());
        std::printf("frames: %d, raven reports: %d, chat entries: %zu, selection: %s\n", frame, g_reports,
                    model.chat().size(), model.selectedPart() ? model.selectedPart()->value.c_str() : "none");
    }
    Rshutdown();
    return o.strict && g_reports > 0 ? 2 : 0;
}
