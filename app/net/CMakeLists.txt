# Owner: S7 Agent bridge. WebSocket client for the T3 Code server, JSON-RPC client for the worker.
add_library(fulmetry_net INTERFACE)
target_link_libraries(fulmetry_net INTERFACE fulmetry_core)
