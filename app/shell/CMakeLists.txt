# Owner: integrator. Composition root: picks real modules or fakes.
add_executable(fulmetry src/main.cpp)
target_link_libraries(fulmetry PRIVATE fulmetry_core fulmetry_project fulmetry_viewport fulmetry_cad fulmetry_sim
                                       fulmetry_net fulmetry_ui)
target_include_directories(fulmetry PRIVATE ${PROJECT_SOURCE_DIR}/app/tests)
