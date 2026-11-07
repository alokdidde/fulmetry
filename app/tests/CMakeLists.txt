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
