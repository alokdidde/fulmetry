#include "fulmetry/ui/ProjectLibrary.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "fulmetry/project/ProjectIO.h"

namespace fulmetry::ui {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

std::string slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

json readSettings(const std::string& path) {
    auto j = json::parse(slurp(path), nullptr, false);
    return j.is_object() ? j : json::object();
}

std::vector<std::string> words(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string slug(const std::string& name) {
    std::string s;
    for (char c : lower(name)) {
        if (std::isalnum(static_cast<unsigned char>(c))) s += c;
        else if (!s.empty() && s.back() != '-') s += '-';
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s.empty() ? "project" : s;
}

}  // namespace

ProjectLibrary::ProjectLibrary(std::string root, std::string settings)
    : root_(std::move(root)), settings_(std::move(settings)) {}

std::string ProjectLibrary::nameFromIdea(const std::string& idea) {
    // The idea up to its first clause ("that", "which", "so", a comma...), without a leading article.
    static const std::vector<std::string> stops = {"that", "which", "who", "so", "to", "for", "with", "when", "and"};
    std::vector<std::string> kept;
    for (auto w : words(idea)) {
        bool clauseEnds = false;
        while (!w.empty() && std::string(",.;:!?").find(w.back()) != std::string::npos) {
            w.pop_back();
            clauseEnds = true;
        }
        const std::string lw = lower(w);
        if (kept.empty() && (lw == "a" || lw == "an" || lw == "the" || lw == "make" || lw == "build")) continue;
        if (!kept.empty() && std::find(stops.begin(), stops.end(), lw) != stops.end()) break;
        if (!w.empty()) kept.push_back(w);
        if (clauseEnds || kept.size() == 6) break;
    }
    if (kept.empty()) return "New project";
    std::string name;
    for (const auto& w : kept) name += (name.empty() ? "" : " ") + w;
    name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
    return name;
}

std::string ProjectLibrary::chatPath(const std::string& projectPath) {
    return (fs::path(projectPath).parent_path() / "chat.json").string();
}

std::vector<ProjectEntry> ProjectLibrary::recent() const {
    std::vector<ProjectEntry> out;
    const json j = readSettings(settings_);
    for (const auto& p : j.value("recent", json::array())) {
        if (!p.is_string()) continue;
        auto project = loadProject(p.get<std::string>());
        if (!project.ok()) continue;
        int parts = 0;
        for (const auto& o : project->objects) parts += static_cast<int>(o.parts.size());
        const auto objects = project->objects.size();
        std::string detail = parts == 0 ? "Empty: started from an idea"
                                        : std::to_string(parts) + (parts == 1 ? " part" : " parts");
        if (objects > 1) detail += " · " + std::to_string(objects) + " objects";
        out.push_back({project->name, p.get<std::string>(), detail});
    }
    return out;
}

void ProjectLibrary::touch(const std::string& path) {
    if (settings_.empty()) return;
    json j = readSettings(settings_);
    json list = json::array({path});
    for (const auto& p : j.value("recent", json::array()))
        if (p.is_string() && p.get<std::string>() != path && static_cast<int>(list.size()) < kRecent) list.push_back(p);
    j["recent"] = list;
    std::error_code ec;
    fs::create_directories(fs::path(settings_).parent_path(), ec);
    std::ofstream(settings_, std::ios::binary | std::ios::trunc) << j.dump(2) << "\n";
}

Result<std::string> ProjectLibrary::createFromIdea(const std::string& idea, const std::string& time) {
    if (words(idea).empty()) return Error{"invalid_argument", "describe what you want to make"};
    const std::string name = nameFromIdea(idea);
    std::error_code ec;
    fs::path dir = fs::path(root_) / slug(name);
    for (int n = 2; fs::exists(dir); ++n) dir = fs::path(root_) / (slug(name) + "-" + std::to_string(n));
    if (!fs::create_directories(dir, ec) || ec)
        return Error{"io", "cannot make " + dir.string() + ": " + ec.message()};

    Project project;
    project.name = name;
    project.revision.summary = "Started from an idea";
    project.objects.push_back(Object{ObjectId("obj:" + slug(name)), name, {}});
    const std::string path = (dir / "fulmetry.project.json").string();
    if (auto st = saveProject(project, path); !st.ok()) return st.error();

    const json chat = {{"entries", json::array({{{"author", "You"}, {"time", time}, {"text", idea}}})}};
    std::ofstream(chatPath(path), std::ios::binary | std::ios::trunc) << chat.dump(2) << "\n";
    touch(path);
    return path;
}

}  // namespace fulmetry::ui
