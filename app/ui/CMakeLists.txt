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

  add_library(fulmetry_ui_raven STATIC raven/Kit.cpp raven/StageImage.cpp raven/WorkbenchView.cpp)
  target_include_directories(fulmetry_ui_raven PUBLIC raven)
  target_link_libraries(fulmetry_ui_raven PUBLIC fulmetry_ui raven::raven)
endif()
