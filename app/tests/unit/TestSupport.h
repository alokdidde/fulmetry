#pragma once
#include <fstream>
#include <sstream>
#include <string>

#include "fulmetry/project/ProjectIO.h"

#ifndef FULMETRY_SOURCE_DIR
#error "FULMETRY_SOURCE_DIR must be defined by CMake"
#endif

namespace fulmetry::test {
inline std::string fixture(const std::string& rel) { return std::string(FULMETRY_SOURCE_DIR) + "/fixtures/" + rel; }
inline std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
inline Project rover() {
    auto p = loadProject(fixture("rover/fulmetry.project.json"));
    if (!p.ok()) throw std::runtime_error(p.error().message);
    return p.value();
}
}  // namespace fulmetry::test
