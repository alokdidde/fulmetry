# Owner: S1 Project model.
add_library(fulmetry_project STATIC src/ProjectIO.cpp)
target_include_directories(fulmetry_project PUBLIC include)
target_link_libraries(fulmetry_project PUBLIC fulmetry_core PRIVATE fulmetry_third_party)
