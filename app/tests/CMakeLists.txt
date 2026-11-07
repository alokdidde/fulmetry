add_executable(fulmetry_tests
  unit/main.cpp
  unit/test_project.cpp
  unit/test_events.cpp
  unit/test_fakes_conformance.cpp)
target_include_directories(fulmetry_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fulmetry_tests PRIVATE fulmetry_core fulmetry_project fulmetry_third_party)
target_compile_definitions(fulmetry_tests PRIVATE FULMETRY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME unit COMMAND fulmetry_tests)

add_test(NAME shell_smoke COMMAND fulmetry --project ${PROJECT_SOURCE_DIR}/fixtures/rover/fulmetry.project.json --comment)
set_tests_properties(shell_smoke PROPERTIES PASS_REGULAR_EXPRESSION "comment cmt:1: resolved")

# Golden images (S10). The fake viewport is deterministic up to floating point, so the limits allow a few
# flipped edge pixels between GCC, Clang and MSVC but not a moved part; golden_rover-fake-iso-head-moved proves it.
set(rover_fixture ${PROJECT_SOURCE_DIR}/fixtures/rover/fulmetry.project.json)
fulmetry_add_golden_test(NAME rover-fake-iso
  COMMAND $<TARGET_FILE:fulmetry> --project ${rover_fixture} --render rover-fake-iso.ppm
  OUTPUT rover-fake-iso.ppm
  THRESHOLD 8 MAX_RMSE 6.0 MAX_FRACTION 0.002)

# Same render with the camera head moved 10 mm along x (generated from the fixture at configure time, so it
# follows fixture edits). Must be reported as a mismatch with the same limits.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${rover_fixture})
file(READ ${rover_fixture} rover_json)
set(rover_moved ${CMAKE_CURRENT_BINARY_DIR}/rover-head-moved.project.json)
file(REMOVE ${rover_moved})
string(JSON part_count LENGTH "${rover_json}" objects 0 parts)
math(EXPR part_last "${part_count} - 1")
foreach(i RANGE ${part_last})
  string(JSON part_id GET "${rover_json}" objects 0 parts ${i} id)
  if(part_id STREQUAL "part:rover/camera-head")
    string(JSON head_x GET "${rover_json}" objects 0 parts ${i} placement position 0)
    math(EXPR head_x "${head_x} - 10")
    string(JSON rover_json SET "${rover_json}" objects 0 parts ${i} placement position 0 "${head_x}")
    file(WRITE ${rover_moved} "${rover_json}")  # missing part: no file, so the test fails loudly
  endif()
endforeach()
fulmetry_add_golden_test(NAME rover-fake-iso-head-moved
  COMMAND $<TARGET_FILE:fulmetry> --project ${rover_moved} --render rover-fake-iso-head-moved.ppm
  OUTPUT rover-fake-iso-head-moved.ppm
  GOLDEN ${PROJECT_SOURCE_DIR}/fixtures/golden/rover-fake-iso.png
  THRESHOLD 8 MAX_RMSE 6.0 MAX_FRACTION 0.002
  EXPECT_MISMATCH)
