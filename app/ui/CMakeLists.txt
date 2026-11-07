# Owner: S5 Workbench UI.
# fulmetry_ui: what the workbench shows (WorkbenchModel), GUI-free, always built and unit-tested.
# With FULMETRY_WITH_RAVEN_GUI=ON: fulmetry_ui_raven (the Raven view) and fulmetry_workbench (the shell).
add_library(fulmetry_ui STATIC src/WorkbenchModel.cpp)
target_include_directories(fulmetry_ui PUBLIC include)
target_link_libraries(fulmetry_ui PUBLIC fulmetry_core PRIVATE fulmetry_third_party)

if(FULMETRY_BUILD_TESTS)
  enable_testing()
  add_executable(fulmetry_ui_tests tests/test_workbench_model.cpp)
  target_include_directories(fulmetry_ui_tests PRIVATE ${PROJECT_SOURCE_DIR}/app/tests)
  target_link_libraries(fulmetry_ui_tests PRIVATE fulmetry_ui fulmetry_project fulmetry_third_party)
  target_compile_definitions(fulmetry_ui_tests PRIVATE FULMETRY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  add_test(NAME ui_model_tests COMMAND fulmetry_ui_tests)
endif()

if(FULMETRY_WITH_RAVEN_GUI)
  # Raven GUI (github.com/oscar-soirey/Raven, Apache-2.0) from a source checkout outside the repo.
  # The pinned commit is in docs/streams/s5.md.
  set(RAVEN_GUI_ROOT "" CACHE PATH "Path to a Raven GUI source checkout (github.com/oscar-soirey/Raven)")
  if(NOT EXISTS "${RAVEN_GUI_ROOT}/include/Raven.h")
    message(FATAL_ERROR "FULMETRY_WITH_RAVEN_GUI=ON needs RAVEN_GUI_ROOT pointing at a Raven checkout")
  endif()
  set(RAVEN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(RAVEN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(RAVEN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(RAVEN_BUILD_WEB OFF CACHE BOOL "" FORCE)
  add_subdirectory("${RAVEN_GUI_ROOT}" "${CMAKE_BINARY_DIR}/_deps/raven_gui" EXCLUDE_FROM_ALL)
  # Third-party C: keep our -Wall -Wextra -Wpedantic and warnings-as-errors off it.
  set_target_properties(raven PROPERTIES COMPILE_WARNING_AS_ERROR OFF)
  target_compile_options(raven PRIVATE $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>)

  add_subdirectory(spike)

  add_library(fulmetry_ui_raven STATIC raven/Kit.cpp raven/StageImage.cpp raven/WorkbenchView.cpp raven/WorkbenchHarness.cpp
                                       raven/LiveStage.cpp)
  target_include_directories(fulmetry_ui_raven PUBLIC raven)
  target_link_libraries(fulmetry_ui_raven PUBLIC fulmetry_ui raven::raven)

  add_executable(fulmetry_workbench raven/main.cpp)
  target_include_directories(fulmetry_workbench PRIVATE ${PROJECT_SOURCE_DIR}/app/tests)
  # With FULMETRY_WITH_WICKED the stage is the live Wicked render (fulmetry_viewport defines FULMETRY_HAS_WICKED);
  # without it, fulmetry_viewport is an interface target and the stage shows assets/stage/rover-highkey.png.
  target_link_libraries(fulmetry_workbench PRIVATE fulmetry_ui_raven fulmetry_project fulmetry_viewport
                                                   fulmetry_third_party)
  if(FULMETRY_WITH_WICKED)
    fulmetry_wicked_runtime(fulmetry_workbench)
  endif()
  target_compile_definitions(fulmetry_workbench PRIVATE
    FULMETRY_UI_ASSETS="${CMAKE_CURRENT_SOURCE_DIR}/assets" FULMETRY_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

  if(FULMETRY_BUILD_TESTS)
    # Builds the whole shell on Raven's null platform and backend (no display, no GPU), runs 120 frames with
    # the action menu open, the chat docked left and a part selected, and fails on any Raven warning or error.
    add_test(NAME ui_workbench_headless
      COMMAND fulmetry_workbench --headless --strict --frames 120 --open menu --chat-left
              --select part:rover/camera-head)
    # With Wicked the stage renders live (lavapipe), and the run continues past 120 frames until it has settled.
    set_tests_properties(ui_workbench_headless PROPERTIES
      PASS_REGULAR_EXPRESSION "frames: [0-9]+, raven reports: 0, chat entries: 6, selection: part:rover/camera-head"
      ENVIRONMENT "RAVEN_LIVE_RELOAD=0")
    # First run: nothing is ready, the onboarding screen opens by itself; Raven is signed in with a key and
    # becomes what new agents run on.
    add_test(NAME ui_workbench_onboarding
      COMMAND fulmetry_workbench --headless --strict --still --run-seconds 3 --harnesses fresh
              --do signin:raven --do answer:raven=sk-test)
    set_tests_properties(ui_workbench_onboarding PROPERTIES
      PASS_REGULAR_EXPRESSION "onboarding open.*harness answer: raven.*new agents run on: raven/claude-sonnet-5-5\nframes: [0-9]+, raven reports: 0"
      ENVIRONMENT "RAVEN_LIVE_RELOAD=0;FULMETRY_CONFIG_DIR=${CMAKE_CURRENT_BINARY_DIR}/test-config")
  endif()
endif()
