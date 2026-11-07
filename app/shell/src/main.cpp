// Fulmetry composition root. In M0 it runs headless on fakes:
//   fulmetry --project fixtures/rover/fulmetry.project.json --render out.ppm
//   fulmetry --project ... --comment      (posts the fixture comment and runs its agent to completion)
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "fakes/FakeAgentClient.h"
#include "fakes/FakeViewport.h"
#include "fulmetry/core/Events.h"
#include "fulmetry/core/Version.h"
#include "fulmetry/project/ProjectIO.h"

using namespace fulmetry;

namespace {
int usage() {
    std::cerr << "usage: fulmetry --project <fulmetry.project.json> [--render out.ppm] [--comment]\n";
    return 2;
}
bool writePpm(const Image& img, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << img.width << " " << img.height << "\n255\n";
    for (size_t i = 0; i < img.rgba.size(); i += 4) out.write(reinterpret_cast<const char*>(&img.rgba[i]), 3);
    return bool(out);
}
}  // namespace

int main(int argc, char** argv) {
    std::string projectPath, renderPath;
    bool runComment = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--project") && i + 1 < argc) projectPath = argv[++i];
        else if (!std::strcmp(argv[i], "--render") && i + 1 < argc) renderPath = argv[++i];
        else if (!std::strcmp(argv[i], "--comment")) runComment = true;
        else if (!std::strcmp(argv[i], "--version")) { std::cout << "fulmetry 0.0.1 contracts v" << kContractsVersion << "\n"; return 0; }
        else return usage();
    }
    if (projectPath.empty()) return usage();

    auto project = loadProject(projectPath);
    if (!project.ok()) {
        std::cerr << "error: " << project.error().code << ": " << project.error().message << "\n";
        return 1;
    }
    std::cout << "loaded " << project->name << ": " << project->objects.size() << " objects, "
              << project->allParts().size() << " parts, r" << project->revision.number << "\n";

    EventBus bus;
    auto viewport = std::make_unique<fakes::FakeViewport>();
    viewport->loadProject(project.value());

    if (!renderPath.empty()) {
        viewport->showObject(project->objects.front().id);
        auto img = viewport->renderToImage(320, 200);
        if (!img.ok() || !writePpm(img.value(), renderPath)) {
            std::cerr << "error: render failed\n";
            return 1;
        }
        std::cout << "rendered " << renderPath << "\n";
    }

    if (runComment) {
        if (project->comments.empty()) { std::cerr << "error: project has no comments\n"; return 1; }
        fakes::FakeAgentClient agents(project.value());
        agents.connect("fake://local");
        bus.subscribe([](const Event& e) {
            if (auto* a = std::get_if<AgentUpdated>(&e))
                std::cout << "agent " << a->card.name << ": " << agentWireState(a->card) << " - " << a->card.step << "\n";
            if (auto* r = std::get_if<RevisionSaved>(&e)) std::cout << "revision r" << r->revision.number << " saved\n";
            if (auto* c = std::get_if<CheckUpdated>(&e)) std::cout << "check " << c->check.name << ": " << toName(c->check.verdict) << "\n";
            if (auto* c = std::get_if<CommentUpdated>(&e)) std::cout << "comment " << c->comment.id.value << ": " << toName(c->comment.status) << "\n";
        });
        auto id = agents.startAgentForComment(project->comments.front(), std::nullopt);
        for (int i = 0; i < 10; ++i) { agents.poll(bus); bus.drain(); }
        (void)id;
    }
    return 0;
}
