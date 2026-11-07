# Owner: S10. Image compare tool for golden-image tests (see tools/cmake/FulmetryGolden.cmake).
add_executable(fulmetry_imgdiff imgdiff.cpp stb_impl.cpp)
target_link_libraries(fulmetry_imgdiff PRIVATE fulmetry_third_party)

add_test(NAME imgdiff_usage COMMAND fulmetry_imgdiff)
set_tests_properties(imgdiff_usage PROPERTIES WILL_FAIL TRUE)
