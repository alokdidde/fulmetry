# Owner: S4 Simulation. Real implementation builds when FULMETRY_WITH_MUJOCO is ON; until then the app uses the fake.
add_library(fulmetry_sim INTERFACE)
target_link_libraries(fulmetry_sim INTERFACE fulmetry_core)
if(FULMETRY_WITH_MUJOCO)
  message(STATUS "fulmetry_sim: real implementation not written yet (MuJoCo)")
endif()
