# Owner: S6 Sheets. KiCad parser has no engine dependency; drawing needs the UI layer.
add_library(fulmetry_sheets STATIC
  src/SExpr.cpp
  src/Schematic.cpp
  src/Netlist.cpp)
target_include_directories(fulmetry_sheets PUBLIC include)
target_link_libraries(fulmetry_sheets PUBLIC fulmetry_core)

if(FULMETRY_BUILD_TESTS)
  # The root enables testing after adding modules, so enable it for this directory too.
  enable_testing()
  add_executable(fulmetry_sheets_tests
    tests/main.cpp
    tests/test_sexpr.cpp
    tests/test_schematic.cpp
    tests/test_netlist.cpp)
  target_link_libraries(fulmetry_sheets_tests PRIVATE fulmetry_sheets fulmetry_third_party)
  target_compile_definitions(fulmetry_sheets_tests PRIVATE
    FULMETRY_KICAD_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/fixtures/rover/kicad")
  add_test(NAME sheets_tests COMMAND fulmetry_sheets_tests)
endif()
