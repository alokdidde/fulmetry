# OcctCadKernel against the shared conformance suite, plus STEP round trips and the extra queries.
add_executable(fulmetry_cad_tests main.cpp test_occt_kernel.cpp)
target_include_directories(fulmetry_cad_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/app/tests)
target_link_libraries(fulmetry_cad_tests PRIVATE fulmetry_cad fulmetry_project fulmetry_third_party)
target_compile_definitions(fulmetry_cad_tests PRIVATE FULMETRY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME cad_tests COMMAND fulmetry_cad_tests)
set_tests_properties(cad_tests PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
