// The projects a person has on this machine: where new ones go, the recent list, and starting one from an idea.
// GUI-free; the home screen (WorkbenchView) shows it. Owned by S5.
#pragma once
#include <string>
#include <vector>

#include "fulmetry/core/Result.h"

namespace fulmetry::ui {

struct ProjectEntry {
    std::string name;
    std::string path;    // its fulmetry.project.json
    std::string detail;  // "3 parts · 2 objects", or "Empty: started from an idea"
};

class ProjectLibrary {
public:
    // `root`: the folder new projects are made in (one folder each). `settings`: the settings.json that keeps
    // the recent list next to the harness choice.
    ProjectLibrary(std::string root, std::string settings);

    const std::string& root() const { return root_; }

    // Most recent first, only projects that still load.
    std::vector<ProjectEntry> recent() const;
    // Puts `path` first in the recent list (at most kRecent kept).
    void touch(const std::string& path);

    // A new project folder under root() named after the idea, holding an empty project and a chat whose first
    // message is the idea. Returns the project file's path.
    Result<std::string> createFromIdea(const std::string& idea, const std::string& time);

    // "Plant watering robot" from "a plant watering robot that waters my basil when the soil is dry".
    static std::string nameFromIdea(const std::string& idea);
    // Where a project's own chat is kept: chat.json beside its project file.
    static std::string chatPath(const std::string& projectPath);

    static constexpr int kRecent = 8;

private:
    std::string root_, settings_;
};

}  // namespace fulmetry::ui
