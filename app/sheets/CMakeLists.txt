# Owner: S6 Sheets. KiCad parser has no engine dependency; drawing needs the UI layer.
add_library(fulmetry_sheets INTERFACE)
target_link_libraries(fulmetry_sheets INTERFACE fulmetry_core)
