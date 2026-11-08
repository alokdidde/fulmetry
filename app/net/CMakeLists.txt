# Owner: S7 Agent bridge. CliSignIn: Claude Code and Codex accounts through their own CLIs, as T3 Code uses them.
add_library(fulmetry_net STATIC src/CliSignIn.cpp)
target_include_directories(fulmetry_net PUBLIC include)
target_link_libraries(fulmetry_net PUBLIC fulmetry_core PRIVATE fulmetry_third_party)
# std::getenv is fine here; MSVC flags it as unsafe (C4996), which -WX turns into an error.
target_compile_definitions(fulmetry_net PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)

if(FULMETRY_BUILD_TESTS AND NOT WIN32)
  add_executable(fulmetry_net_tests tests/test_cli_sign_in.cpp)
  target_include_directories(fulmetry_net_tests PRIVATE ${PROJECT_SOURCE_DIR}/app/tests)
  target_link_libraries(fulmetry_net_tests PRIVATE fulmetry_net fulmetry_third_party)
  add_test(NAME net COMMAND fulmetry_net_tests)
endif()
