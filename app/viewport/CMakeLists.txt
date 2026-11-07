# Owner: S2 Viewport. Real implementation builds when FULMETRY_WITH_WICKED is ON; until then the app uses the fake.
add_library(fulmetry_viewport INTERFACE)
target_link_libraries(fulmetry_viewport INTERFACE fulmetry_core)
if(FULMETRY_WITH_WICKED)
  message(STATUS "fulmetry_viewport: real implementation not written yet (Wicked Engine in a Raven GUI viewport)")
endif()
