file(GLOB_RECURSE PENUMBRA_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(PenumbraTests ${PENUMBRA_TEST_SOURCES})
penumbra_configure_target(PenumbraTests)
target_link_libraries(PenumbraTests PRIVATE Penumbra pn_doctest)
target_compile_definitions(PenumbraTests PRIVATE
    PENUMBRA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    PENUMBRA_DEMO_PROJECT_DIR="${PROJECT_SOURCE_DIR}/DemoProject")

# Each doctest test suite is registered separately so CTest reports them individually.
foreach(suite Core Scene Serialization Assets Physics Audio Scripting Perception Stim Automation Renderer)
    add_test(NAME Unit.${suite} COMMAND PenumbraTests --test-suite=${suite} --no-colors)
endforeach()
set_tests_properties(Unit.Renderer PROPERTIES LABELS gpu)
