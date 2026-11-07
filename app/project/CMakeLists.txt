# Owner: S1 Project model.
add_library(fulmetry_project STATIC src/ProjectIO.cpp src/CheckGraph.cpp)
target_include_directories(fulmetry_project PUBLIC include)
target_link_libraries(fulmetry_project PUBLIC fulmetry_core PRIVATE fulmetry_third_party)

if(FULMETRY_BUILD_TESTS)
  # The root calls enable_testing() after adding this directory, so turn it on here as well.
  enable_testing()
  add_executable(fulmetry_project_tests tests/main.cpp tests/test_check_graph.cpp)
  target_include_directories(fulmetry_project_tests PRIVATE tests)
  target_link_libraries(fulmetry_project_tests PRIVATE fulmetry_project fulmetry_third_party)
  target_compile_definitions(fulmetry_project_tests PRIVATE FULMETRY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  add_test(NAME project_tests COMMAND fulmetry_project_tests)
endif()
