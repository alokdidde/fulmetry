# Owner: S4 Simulation. Real implementation builds when FULMETRY_WITH_MUJOCO is ON; until then the app uses the fake.
#
# MuJoCo is found in one of two ways:
#   -DMUJOCO_ROOT=<dir>   an unpacked official release (include/mujoco/mujoco.h, lib/libmujoco.so|.dylib|mujoco.lib)
#   find_package(mujoco)  a CMake package (vcpkg or a source build), giving mujoco::mujoco
add_library(fulmetry_sim INTERFACE)
target_link_libraries(fulmetry_sim INTERFACE fulmetry_core)

if(NOT FULMETRY_WITH_MUJOCO)
  return()
endif()

set(MUJOCO_ROOT "" CACHE PATH "Unpacked MuJoCo release (contains include/ and lib/). Empty: use find_package(mujoco).")

if(MUJOCO_ROOT)
  find_path(FULMETRY_MUJOCO_INCLUDE_DIR mujoco/mujoco.h PATHS "${MUJOCO_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
  find_library(FULMETRY_MUJOCO_LIBRARY NAMES mujoco PATHS "${MUJOCO_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
  add_library(fulmetry_mujoco_imported UNKNOWN IMPORTED)
  set_target_properties(fulmetry_mujoco_imported PROPERTIES
    IMPORTED_LOCATION "${FULMETRY_MUJOCO_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${FULMETRY_MUJOCO_INCLUDE_DIR}")
  set(FULMETRY_MUJOCO_TARGET fulmetry_mujoco_imported)
  message(STATUS "fulmetry_sim: MuJoCo from MUJOCO_ROOT=${MUJOCO_ROOT}")
else()
  find_package(mujoco CONFIG REQUIRED)
  set(FULMETRY_MUJOCO_TARGET mujoco::mujoco)
  message(STATUS "fulmetry_sim: MuJoCo ${mujoco_VERSION} from find_package")
endif()

add_library(fulmetry_sim_mujoco STATIC src/MujocoSimulation.cpp)
target_include_directories(fulmetry_sim_mujoco PUBLIC include)
target_link_libraries(fulmetry_sim_mujoco PUBLIC fulmetry_core PRIVATE ${FULMETRY_MUJOCO_TARGET})
target_compile_features(fulmetry_sim_mujoco PUBLIC cxx_std_20)
# Consumers (the shell) can switch from FakeSimulation to MujocoSimulation on this definition.
target_compile_definitions(fulmetry_sim_mujoco INTERFACE FULMETRY_HAS_MUJOCO=1)
target_link_libraries(fulmetry_sim INTERFACE fulmetry_sim_mujoco)

if(FULMETRY_BUILD_TESTS)
  enable_testing()
  add_executable(fulmetry_sim_tests tests/main.cpp tests/test_mujoco_simulation.cpp)
  # The shared conformance suite lives in app/tests/conformance (read-only for S4).
  target_include_directories(fulmetry_sim_tests PRIVATE "${PROJECT_SOURCE_DIR}/app/tests")
  target_link_libraries(fulmetry_sim_tests PRIVATE fulmetry_sim_mujoco fulmetry_third_party)
  target_compile_definitions(fulmetry_sim_tests PRIVATE FULMETRY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  add_test(NAME sim_tests COMMAND fulmetry_sim_tests)
endif()
