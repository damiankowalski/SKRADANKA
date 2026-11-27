file(GLOB_RECURSE PENUMBRA_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_executable(PenumbraTests ${PENUMBRA_TEST_SOURCES})
penumbra_configure_target(PenumbraTests)
target_link_libraries(PenumbraTests PRIVATE Penumbra pn_doctest)
target_compile_definitions(PenumbraTests PRIVATE
    PENUMBRA_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
    PENUMBRA_DEMO_PROJECT_DIR="${PROJECT_SOURCE_DIR}/DemoProject"
    PENUMBRA_DOCS_DIR="${PROJECT_SOURCE_DIR}/docs")

# Each doctest test suite is registered separately so CTest reports them individually.
set(PENUMBRA_TEST_SUITES Core Scene Serialization Assets Animation Physics Audio Scripting Perception Navigation World Mission
    MissionOne Stim Automation Mcp Renderer Debug FogOfWar Fog Playtest)
foreach(suite ${PENUMBRA_TEST_SUITES})
    add_test(NAME Unit.${suite} COMMAND PenumbraTests --test-suite=${suite} --no-colors)
endforeach()
# A suite missing from the list would never run in CTest or CI (Mission once went unnoticed): refuse it.
foreach(source ${PENUMBRA_TEST_SOURCES})
    file(STRINGS "${source}" suite_lines REGEX "TEST_SUITE\\(\"[A-Za-z]+\"\\)")
    foreach(line ${suite_lines})
        string(REGEX MATCH "TEST_SUITE\\(\"([A-Za-z]+)\"\\)" match "${line}")
        if(match AND NOT CMAKE_MATCH_1 IN_LIST PENUMBRA_TEST_SUITES)
            message(FATAL_ERROR "${source}: test suite '${CMAKE_MATCH_1}' is not in PENUMBRA_TEST_SUITES (PenumbraTests/CMakeLists.txt)")
        endif()
    endforeach()
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
# The first mission's blockout, the biggest level: loads, simulates and renders without errors.
add_test(NAME Scene.Mission1Demo
    COMMAND PenumbraRuntime --project "${PROJECT_SOURCE_DIR}/DemoProject" --scene Scenes/Mission1.pscene --headless --warp
            --fixed-timestep 0.0166667 --frames 120 --width 320 --height 180)
set_tests_properties(Scene.FeatureTest Scene.StealthDemo Scene.Mission1Demo PROPERTIES LABELS gpu TIMEOUT 600)
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
    # The mood lab (docs/art/mood): four lighting variants of the cloister rendered to eight PNGs.
    add_test(NAME Automation.MoodLab
        COMMAND PenumbraEditor --headless --warp --exec "${PROJECT_SOURCE_DIR}/docs/art/mood/MoodLab.jsonl"
                --project "${PROJECT_SOURCE_DIR}/DemoProject"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/MoodLab")
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/MoodLab/docs/art/mood/v3")
    set_tests_properties(Automation.EditorCLI Automation.McpStdio Editor.Smoke Automation.MoodLab PROPERTIES LABELS gpu TIMEOUT 600)
endif()

# AssetGen (Tools/AssetGen): seams, normal maps, provenance, the model allow-list, the texture catalog. Plain
# Python through uv, no model and no Apple Silicon needed; skipped where uv is not installed.
find_program(PENUMBRA_UV_EXECUTABLE uv)
if(PENUMBRA_UV_EXECUTABLE)
    add_test(NAME Tools.AssetGen
        COMMAND "${PENUMBRA_UV_EXECUTABLE}" run --project "${PROJECT_SOURCE_DIR}/Tools/AssetGen" --frozen
                python -m unittest discover -s "${PROJECT_SOURCE_DIR}/Tools/AssetGen/tests"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(Tools.AssetGen PROPERTIES TIMEOUT 600)
    # AudioGen (Tools/AudioGen): loudness, true peak, loop seams, broken files, the listening room's feedback,
    # promotion, the model allow-list. numpy and scipy only, no model.
    add_test(NAME Tools.AudioGen
        COMMAND "${PENUMBRA_UV_EXECUTABLE}" run --project "${PROJECT_SOURCE_DIR}/Tools/AudioGen" --frozen
                python -m unittest discover -s "${PROJECT_SOURCE_DIR}/Tools/AudioGen/tests"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(Tools.AudioGen PROPERTIES TIMEOUT 600)
else()
    message(STATUS "uv not found: the Tools.AssetGen and Tools.AudioGen tests are skipped (their README.md)")
endif()

# PropGen (Tools/PropGen): the mesh kit, the .glb writer, the props' checks, and the committed props matching their
# scripts. Python's standard library only.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME Tools.PropGen
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover -s "${PROJECT_SOURCE_DIR}/Tools/PropGen/tests"
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    set_tests_properties(Tools.PropGen PROPERTIES TIMEOUT 300)
else()
    message(STATUS "Python 3 not found: the Tools.PropGen test is skipped (Tools/PropGen/README.md)")
endif()
