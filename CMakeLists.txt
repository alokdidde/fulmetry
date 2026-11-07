cmake_minimum_required(VERSION 3.24)
project(fulmetry VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Heavy engines are opt-in per module so every stream can build and test against fakes.
option(FULMETRY_WITH_RAVEN_GUI "Build the Raven GUI shell (S5)" OFF)
option(FULMETRY_WITH_WICKED "Build the Wicked Engine viewport (S2)" OFF)
option(FULMETRY_WITH_OCCT "Build the OCCT CAD kernel (S3)" OFF)
option(FULMETRY_WITH_MUJOCO "Build the MuJoCo simulation (S4)" OFF)
option(FULMETRY_BUILD_TESTS "Build tests" ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(fulmetry_third_party INTERFACE)
target_include_directories(fulmetry_third_party SYSTEM INTERFACE third_party)

# enable_testing() must precede the modules so their own add_test() calls register.
if(FULMETRY_BUILD_TESTS)
  enable_testing()
  include(tools/cmake/FulmetryGolden.cmake)
  add_subdirectory(tools/imgdiff)
endif()

# Module list is fixed here (S10 owns this file). Each module owns its own CMakeLists.txt.
add_subdirectory(app/core)
add_subdirectory(app/project)
add_subdirectory(app/viewport)
add_subdirectory(app/cad)
add_subdirectory(app/sim)
add_subdirectory(app/sheets)
add_subdirectory(app/net)
add_subdirectory(app/ui)
add_subdirectory(app/shell)

if(FULMETRY_BUILD_TESTS)
  add_subdirectory(app/tests)
endif()
