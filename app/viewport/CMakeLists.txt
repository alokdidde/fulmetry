# Owner: S2 Viewport. Real implementation builds when FULMETRY_WITH_WICKED is ON; until then the app uses the fake.

# Part meshes (glTF loading, box fallback, ray picking). No GPU and no Wicked, so it builds and is tested everywhere.
add_library(fulmetry_viewport_mesh STATIC src/MeshAsset.cpp)
target_include_directories(fulmetry_viewport_mesh PUBLIC include)
target_include_directories(fulmetry_viewport_mesh SYSTEM PRIVATE third_party)  # cgltf 1.14 (MIT), vendored
target_link_libraries(fulmetry_viewport_mesh PUBLIC fulmetry_core)

if(FULMETRY_BUILD_TESTS)
  add_executable(fulmetry_viewport_mesh_tests tests/test_mesh_asset.cpp)
  target_link_libraries(fulmetry_viewport_mesh_tests PRIVATE fulmetry_viewport_mesh fulmetry_third_party)
  target_compile_definitions(fulmetry_viewport_mesh_tests PRIVATE
    FULMETRY_TESTMESHES="${CMAKE_CURRENT_SOURCE_DIR}/spike/testmeshes")
  add_test(NAME viewport_mesh COMMAND fulmetry_viewport_mesh_tests)
endif()

if(NOT FULMETRY_WITH_WICKED)
  add_library(fulmetry_viewport INTERFACE)
  target_link_libraries(fulmetry_viewport INTERFACE fulmetry_core)
  return()
endif()

# Wicked Engine from source, pinned commit (see docs/streams/s2.md). Keep the checkout outside the repo:
#   git clone https://github.com/turanszkij/WickedEngine.git ~/deps/WickedEngine && git -C ... checkout <sha>
# Linux needs SDL2 dev headers, Vulkan headers/loader, and a Vulkan 1.3 driver (lavapipe works headless).
set(WICKED_ROOT "" CACHE PATH "Wicked Engine source checkout (the repo root that holds WickedEngine/)")
if(NOT EXISTS "${WICKED_ROOT}/WickedEngine/CMakeLists.txt")
  message(FATAL_ERROR "FULMETRY_WITH_WICKED=ON needs -DWICKED_ROOT=<WickedEngine checkout>; got '${WICKED_ROOT}'")
endif()
add_subdirectory(wicked)  # builds the engine with its own flags, not ours

add_library(fulmetry_viewport STATIC src/WickedViewport.cpp)
target_include_directories(fulmetry_viewport PUBLIC include)
target_link_libraries(fulmetry_viewport PUBLIC fulmetry_core PRIVATE WickedEngine)
# Wicked headers are not -Wpedantic clean; keep our warnings but treat theirs as system headers.
target_include_directories(fulmetry_viewport SYSTEM PRIVATE ${WICKED_ROOT}/WickedEngine)
target_compile_definitions(fulmetry_viewport PUBLIC FULMETRY_HAS_WICKED=1)

# Wicked compiles shaders at runtime with libdxcompiler.so, loaded from the executable's directory.
set(FULMETRY_DXCOMPILER "${WICKED_ROOT}/WickedEngine/libdxcompiler.so")
function(fulmetry_wicked_runtime target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FULMETRY_DXCOMPILER} $<TARGET_FILE_DIR:${target}>)
endfunction()

add_executable(fulmetry_wicked_spike spike/render_rover.cpp)
target_link_libraries(fulmetry_wicked_spike PRIVATE fulmetry_viewport fulmetry_project)
fulmetry_wicked_runtime(fulmetry_wicked_spike)

if(FULMETRY_BUILD_TESTS)
  add_executable(fulmetry_viewport_tests tests/test_wicked_viewport.cpp)
  target_include_directories(fulmetry_viewport_tests PRIVATE ${PROJECT_SOURCE_DIR}/app/tests)
  target_link_libraries(fulmetry_viewport_tests PRIVATE fulmetry_viewport fulmetry_project fulmetry_third_party)
  target_compile_definitions(fulmetry_viewport_tests PRIVATE FULMETRY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  fulmetry_wicked_runtime(fulmetry_viewport_tests)
  # Root CMakeLists calls enable_testing() after the modules; enable it here so this test registers.
  enable_testing()
  add_test(NAME viewport_wicked COMMAND fulmetry_viewport_tests)
  add_test(NAME viewport_wicked_spike
    COMMAND fulmetry_wicked_spike ${PROJECT_SOURCE_DIR}/fixtures/rover/fulmetry.project.json
            ${CMAKE_CURRENT_BINARY_DIR}/rover-wicked.png 640 400)
endif()
