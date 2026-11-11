file(GLOB_RECURSE PENUMBRA_EDITOR_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(PenumbraEditor ${PENUMBRA_EDITOR_SOURCES})
penumbra_configure_target(PenumbraEditor)
target_include_directories(PenumbraEditor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(PenumbraEditor PRIVATE Penumbra pn_imgui)
# The editor ships next to the runtime so "Export Game" can find PenumbraRuntime.exe.
add_dependencies(PenumbraEditor PenumbraRuntime)
