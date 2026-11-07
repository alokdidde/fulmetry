# Builds Wicked Engine (library only) from WICKED_ROOT. Our root adds -Wall -Wextra -Wpedantic and the ci preset
# makes warnings errors; neither applies to third-party code, so clear both for this directory tree.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
set(CMAKE_COMPILE_WARNING_AS_ERROR OFF)
set(WICKED_EDITOR OFF CACHE BOOL "" FORCE)
set(WICKED_TESTS OFF CACHE BOOL "" FORCE)
set(WICKED_IMGUI_EXAMPLE OFF CACHE BOOL "" FORCE)
set(WICKED_LINUX_TEMPLATE OFF CACHE BOOL "" FORCE)
set(WICKED_WINDOWS_TEMPLATE OFF CACHE BOOL "" FORCE)
# macOS: Wicked's CMake does not build for Apple (Metal); its Xcode project does. Build the WickedEngine_MacOS static
# library with xcodebuild into this build tree and import it as WickedEngine. Needs Xcode (the macOS 26 SDK).
# WICKED_LAZY_OBJECT_PSO (patches/0001) as on Linux: compiling every object pipeline at startup takes many minutes.
if(APPLE)
  set(_wicked_derived ${CMAKE_CURRENT_BINARY_DIR}/xcode)
  set(_wicked_lib ${_wicked_derived}/Build/Products/Release/libWickedEngine_MacOS.a)
  set(FULMETRY_MACOS_DEPLOYMENT "15.0" CACHE STRING "Oldest macOS the app runs on")
  add_custom_command(OUTPUT ${_wicked_lib}
    COMMAND xcodebuild -project ${WICKED_ROOT}/WickedEngine/WickedEngine.xcodeproj -target WickedEngine_MacOS
            -configuration Release -arch arm64 ONLY_ACTIVE_ARCH=NO SYMROOT=${_wicked_derived}/Build/Products
            OBJROOT=${_wicked_derived}/Build/Intermediates MACOSX_DEPLOYMENT_TARGET=${FULMETRY_MACOS_DEPLOYMENT}
            "GCC_PREPROCESSOR_DEFINITIONS=JPH_DEBUG_RENDERER WICKED_LAZY_OBJECT_PSO"
            CODE_SIGNING_ALLOWED=NO -quiet build
    COMMENT "Wicked Engine (Metal) with xcodebuild"
    VERBATIM)
  add_custom_target(wicked_xcode DEPENDS ${_wicked_lib})
  add_library(WickedEngine STATIC IMPORTED GLOBAL)
  set_target_properties(WickedEngine PROPERTIES IMPORTED_LOCATION ${_wicked_lib})
  add_dependencies(WickedEngine wicked_xcode)
  target_include_directories(WickedEngine SYSTEM INTERFACE ${WICKED_ROOT}/WickedEngine ${WICKED_ROOT}/WickedEngine/Utility/metal)
  target_compile_definitions(WickedEngine INTERFACE JPH_DEBUG_RENDERER)
  target_link_libraries(WickedEngine INTERFACE
    "-framework Metal" "-framework MetalKit" "-framework MetalFX" "-framework QuartzCore" "-framework Foundation"
    "-framework AppKit" "-framework GameController" "-framework IOKit" "-framework CoreHaptics"
    "-framework AudioToolbox" "-framework CoreAudio" "-framework AVFoundation" "-framework UniformTypeIdentifiers")
  return()
endif()

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
