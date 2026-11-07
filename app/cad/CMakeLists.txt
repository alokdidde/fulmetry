# Owner: S3 CAD kernel. Real implementation builds when FULMETRY_WITH_OCCT is ON; until then the app uses the fake.
add_library(fulmetry_cad INTERFACE)
target_link_libraries(fulmetry_cad INTERFACE fulmetry_core)
if(FULMETRY_WITH_OCCT)
  message(STATUS "fulmetry_cad: real implementation not written yet (OCCT)")
endif()
