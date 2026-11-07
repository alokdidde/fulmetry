# Owner: S5 Workbench UI. Real implementation builds when FULMETRY_WITH_RAVEN_GUI is ON; until then the app uses the fake.
add_library(fulmetry_ui INTERFACE)
target_link_libraries(fulmetry_ui INTERFACE fulmetry_core)
if(FULMETRY_WITH_RAVEN_GUI)
  message(STATUS "fulmetry_ui: real implementation not written yet (Raven GUI panels)")
endif()
