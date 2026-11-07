# Owner: S3 CAD kernel. Real implementation builds when FULMETRY_WITH_OCCT is ON; until then the app uses the fake.
if(NOT FULMETRY_WITH_OCCT)
  add_library(fulmetry_cad INTERFACE)
  target_link_libraries(fulmetry_cad INTERFACE fulmetry_core)
  return()
endif()

# OCCT 7.6+ (apt: libocct-*-dev; vcpkg: opencascade). Only the toolkits the kernel uses are linked.
# No version in find_package: OCCT's config-version file accepts exact matches only.
find_package(OpenCASCADE REQUIRED CONFIG)
if(OpenCASCADE_VERSION VERSION_LESS 7.6)
  message(FATAL_ERROR "fulmetry_cad needs OCCT 7.6 or newer, found ${OpenCASCADE_VERSION}")
endif()
message(STATUS "fulmetry_cad: OCCT ${OpenCASCADE_VERSION} from ${OpenCASCADE_DIR}")
set(FULMETRY_OCCT_LIBS
  TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo TKTopAlgo TKPrim TKBO TKShHealing TKMesh
  TKXSBase TKSTEP TKSTEPBase TKSTEPAttr TKSTEP209)

add_library(fulmetry_cad STATIC src/OcctCadKernel.cpp)
target_include_directories(fulmetry_cad PUBLIC include)
target_include_directories(fulmetry_cad SYSTEM PRIVATE ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(fulmetry_cad PUBLIC fulmetry_core PRIVATE ${FULMETRY_OCCT_LIBS})
target_compile_definitions(fulmetry_cad PUBLIC FULMETRY_HAS_OCCT=1)

if(FULMETRY_BUILD_TESTS)
  enable_testing()  # the root adds this directory before it calls enable_testing()
  add_subdirectory(tests)
endif()
