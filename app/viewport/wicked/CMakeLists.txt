# Builds Wicked Engine (library only) from WICKED_ROOT. Our root adds -Wall -Wextra -Wpedantic and the ci preset
# makes warnings errors; neither applies to third-party code, so clear both for this directory tree.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(WICKED_EDITOR OFF CACHE BOOL "" FORCE)
set(WICKED_TESTS OFF CACHE BOOL "" FORCE)
set(WICKED_IMGUI_EXAMPLE OFF CACHE BOOL "" FORCE)
set(WICKED_LINUX_TEMPLATE OFF CACHE BOOL "" FORCE)
set(WICKED_WINDOWS_TEMPLATE OFF CACHE BOOL "" FORCE)
# Wicked's root CMakeLists sets C++17, its platform defines and options, then adds WickedEngine/.
# SYSTEM (CMake 3.25+) marks its include directories as system headers for our targets.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.25)
  set(_wicked_system SYSTEM)
endif()
add_subdirectory(${WICKED_ROOT} ${CMAKE_CURRENT_BINARY_DIR}/engine EXCLUDE_FROM_ALL ${_wicked_system})

# patches/0001-lazy-object-psos.patch makes object pipelines compile on first use instead of all permutations at
# startup (which needs >16 GB on lavapipe). Apply it to the checkout: git -C $WICKED_ROOT apply <patch>.
file(STRINGS ${WICKED_ROOT}/WickedEngine/wiRenderer.cpp _wicked_lazy REGEX "WICKED_LAZY_OBJECT_PSO")
if(_wicked_lazy)
  target_compile_definitions(WickedEngine_ext_shaders PRIVATE WICKED_LAZY_OBJECT_PSO)
else()
  message(WARNING "Wicked checkout lacks patches/0001-lazy-object-psos.patch; software Vulkan will run out of memory")
endif()
