# Contracts as C++ headers. Header-only, no third-party includes. Owner: integrator.
add_library(fulmetry_core INTERFACE)
target_include_directories(fulmetry_core INTERFACE include)
target_compile_features(fulmetry_core INTERFACE cxx_std_20)
