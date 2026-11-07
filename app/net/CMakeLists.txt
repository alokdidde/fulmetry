# Owner: S7 Agent bridge. The T3 Code client: runs `t3 fulmetry` (agent/t3code) and speaks JSON lines with it.
add_library(fulmetry_net STATIC src/T3AgentClient.cpp src/Subprocess.cpp src/AgentClient.cpp)
target_include_directories(fulmetry_net PUBLIC include PRIVATE src ${PROJECT_SOURCE_DIR}/app/tests)
target_link_libraries(fulmetry_net PUBLIC fulmetry_core PRIVATE fulmetry_third_party)
find_package(Threads REQUIRED)
target_link_libraries(fulmetry_net PUBLIC Threads::Threads)
# Development builds find the fork's source checkout; a packaged app finds t3/bin.mjs next to itself.
target_compile_definitions(fulmetry_net PRIVATE FULMETRY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

if(FULMETRY_BUILD_TESTS)
  add_executable(fulmetry_net_tests tests/test_t3_agent_client.cpp)
  target_include_directories(fulmetry_net_tests PRIVATE src ${PROJECT_SOURCE_DIR}/app/tests)
  target_link_libraries(fulmetry_net_tests PRIVATE fulmetry_net fulmetry_project fulmetry_third_party)
  target_compile_definitions(fulmetry_net_tests PRIVATE FULMETRY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  add_test(NAME net COMMAND fulmetry_net_tests)
endif()
