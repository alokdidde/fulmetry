// Load and save fulmetry.project.json. Owned by S1 (Project model).
#pragma once
#include <string>

#include "fulmetry/core/Model.h"
#include "fulmetry/core/Result.h"

namespace fulmetry {

Result<Project> parseProject(const std::string& json);
Result<Project> loadProject(const std::string& path);
std::string serializeProject(const Project& project);  // stable: same project, same bytes
Status saveProject(const Project& project, const std::string& path);

}  // namespace fulmetry
