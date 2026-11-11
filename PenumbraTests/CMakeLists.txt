file(GLOB_RECURSE PENUMBRA_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(PenumbraTests ${PENUMBRA_TEST_SOURCES})
penumbra_configure_target(PenumbraTests)
target_link_libraries(PenumbraTests PRIVATE Penumbra pn_doctest)
target_compile_definitions(PenumbraTests PRIVATE
    PENUMBRA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    PENUMBRA_DEMO_PROJECT_DIR="${PROJECT_SOURCE_DIR}/DemoProject"
    PENUMBRA_DOCS_DIR="${PROJECT_SOURCE_DIR}/docs")

# Each doctest test suite is registered separately so CTest reports them individually.
foreach(suite Core Scene Serialization Assets Animation Physics Audio Scripting Perception Stim Automation Mcp Renderer)
    add_test(NAME Unit.${suite} COMMAND PenumbraTests --test-suite=${suite} --no-colors)
endforeach()
set_tests_properties(Unit.Renderer PROPERTIES LABELS gpu)

# End-to-end: the feature-test scene exercises every system and the whole Lua API in the real runtime.
add_test(NAME Scene.FeatureTest
    COMMAND PenumbraRuntime --project "${PROJECT_SOURCE_DIR}/DemoProject" --scene Scenes/FeatureTest.pscene
            --headless --warp --fixed-timestep 0.0166667 --exit-on-test --test-timeout 60 --width 320 --height 180)
# The demo level loads, simulates and renders without errors.
add_test(NAME Scene.StealthDemo
    COMMAND PenumbraRuntime --project "${PROJECT_SOURCE_DIR}/DemoProject" --headless --warp --fixed-timestep 0.0166667
            --frames 120 --width 320 --height 180)
set_tests_properties(Scene.FeatureTest Scene.StealthDemo PROPERTIES LABELS gpu TIMEOUT 600)
if(PENUMBRA_BUILD_EDITOR)
    # An agent builds a level from scratch through the editor's headless command line.
    add_test(NAME Automation.EditorCLI
        COMMAND PenumbraEditor --headless --warp --exec "${PROJECT_SOURCE_DIR}/docs/examples/AgentQuickstart.jsonl"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/AgentQuickstart")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/AgentQuickstart")
    # The GUI editor starts, opens the demo project, renders its UI and exits cleanly.
    add_test(NAME Editor.Smoke
        COMMAND PenumbraEditor --warp --frames 30 --no-server --project "${PROJECT_SOURCE_DIR}/DemoProject"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/AgentQuickstart")
    # Claude Code / Claude Desktop drive the editor through "PenumbraEditor --mcp" over stdio.
    add_test(NAME Automation.McpStdio
        COMMAND ${CMAKE_COMMAND} -DEDITOR=$<TARGET_FILE:PenumbraEditor>
                -DSESSION=${CMAKE_CURRENT_SOURCE_DIR}/mcp/StdioSession.jsonl -P ${CMAKE_CURRENT_SOURCE_DIR}/mcp/StdioTest.cmake
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/AgentQuickstart")
    set_tests_properties(Automation.EditorCLI Automation.McpStdio Editor.Smoke PROPERTIES LABELS gpu TIMEOUT 600)
endif()
