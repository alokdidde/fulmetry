# Owner: S5 Workbench UI. Real implementation builds when FULMETRY_WITH_RAVEN_GUI is ON; until then the app uses the fake.
add_library(fulmetry_ui INTERFACE)
target_link_libraries(fulmetry_ui INTERFACE fulmetry_core)

if(FULMETRY_WITH_RAVEN_GUI)
  # Raven GUI (github.com/oscar-soirey/Raven, Apache-2.0) from a source checkout outside the repo.
  # The commit the spike was measured on is pinned in docs/streams/s5.md.
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
endif()
