# S5 spike: Raven GUI workbench shell (stage + docked chat). Built only with FULMETRY_WITH_RAVEN_GUI=ON.
add_executable(raven_spike main.cpp)
target_link_libraries(raven_spike PRIVATE raven::raven)
target_compile_definitions(raven_spike PRIVATE RAVEN_SPIKE_XML="${CMAKE_CURRENT_SOURCE_DIR}/workbench.xml")
