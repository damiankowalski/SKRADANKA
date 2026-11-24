# Third-party dependencies. Every dependency is pinned to an exact tag or commit.
# All of them are declared SYSTEM so their headers never trip Penumbra's /W4 /WX.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# ---------------------------------------------------------------------------
# Libraries with a usable CMake build
# ---------------------------------------------------------------------------

# GLFW - windowing and input
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(USE_MSVC_RUNTIME_LIBRARY_DLL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
    SYSTEM)

# GLM - math
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
    SYSTEM)

# NVRHI - rendering hardware interface: Direct3D 12 on Windows, Vulkan everywhere (MoltenVK on macOS).
# NVRHI fetches its pinned Vulkan-Headers (NVRHI_VULKAN_HEADERS_GIT_TAG) for the Vulkan backend.
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX11 OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_DX12 ${WIN32} CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
FetchContent_Declare(nvrhi
    GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git
    GIT_TAG bf0bbd9b666a7297492beb1999a4ddc0dfded143
    # Drivers without mesh shaders (Intel Xe) crash on the unconditional mesh dispatch signature.
    PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/nvrhi-mesh-signature.patch -P ${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake
    SYSTEM)

# Jolt - physics
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY ON CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(USE_ASSERTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.5.0
    GIT_SHALLOW TRUE
    # MSVC builds Jolt with /fp:fast; from MSVC 19.52 optimized builds broke ragdoll hinge limits and settling.
    PATCH_COMMAND ${CMAKE_COMMAND} -DPATCH=${CMAKE_CURRENT_LIST_DIR}/patches/jolt-precise-fp.patch -P ${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake
    SOURCE_SUBDIR Build
    SYSTEM)

# EnTT - entity component system
FetchContent_Declare(EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.16.0
    GIT_SHALLOW TRUE
    SYSTEM)

FetchContent_MakeAvailable(glfw glm nvrhi JoltPhysics EnTT)

# ---------------------------------------------------------------------------
# Header-only / source-only libraries (wrapped in our own targets).
# SOURCE_SUBDIR points at a non-existent folder so FetchContent only downloads.
# ---------------------------------------------------------------------------

macro(penumbra_fetch_sources name repo tag)
    FetchContent_Declare(${name}
        GIT_REPOSITORY ${repo}
        GIT_TAG ${tag}
        SOURCE_SUBDIR _penumbra_no_cmake)
    FetchContent_MakeAvailable(${name})
endmacro()

# Third-party sources compiled by our own targets below: their warnings are not ours to fix, and a newer
# compiler's new warning (recent clang's -Wnontrivial-memcall in DetourNavMesh.cpp) must not show up in every build.
# -w and /W0 silence everything without naming a warning, so no compiler meets an option it does not know.
function(penumbra_third_party_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W0)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
    set_target_properties(${target} PROPERTIES FOLDER ThirdParty)
endfunction()

penumbra_fetch_sources(lua        https://github.com/lua/lua.git                   v5.4.7)
penumbra_fetch_sources(sol2       https://github.com/ThePhD/sol2.git               v3.5.0)
penumbra_fetch_sources(miniaudio  https://github.com/mackron/miniaudio.git         0.11.26)
penumbra_fetch_sources(cgltf      https://github.com/jkuhlmann/cgltf.git           v1.15)
penumbra_fetch_sources(stb        https://github.com/nothings/stb.git              4e9b368bfc071e97206ea5c6adba68dd0cbf8897)
penumbra_fetch_sources(json       https://github.com/nlohmann/json.git             v3.12.0)
penumbra_fetch_sources(imgui      https://github.com/ocornut/imgui.git             v1.92.9-docking)
penumbra_fetch_sources(imguizmo   https://github.com/CedricGuillemet/ImGuizmo.git  105e4fde45e4e74aae8973d98750820ef7aca7e2)
penumbra_fetch_sources(doctest    https://github.com/doctest/doctest.git           v2.5.3)
penumbra_fetch_sources(recastnavigation https://github.com/recastnavigation/recastnavigation.git v1.6.0)

# Lua 5.4 (C library, compiled as C)
file(GLOB PN_LUA_SOURCES "${lua_SOURCE_DIR}/*.c")
list(FILTER PN_LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua|ltests)\\.c$")
add_library(pn_lua STATIC ${PN_LUA_SOURCES})
target_include_directories(pn_lua SYSTEM PUBLIC "${lua_SOURCE_DIR}")
target_compile_definitions(pn_lua PRIVATE _CRT_SECURE_NO_WARNINGS)
penumbra_third_party_target(pn_lua)

# sol2 (Lua C++ binding)
add_library(pn_sol2 INTERFACE)
target_include_directories(pn_sol2 SYSTEM INTERFACE "${sol2_SOURCE_DIR}/include")
target_compile_definitions(pn_sol2 INTERFACE SOL_ALL_SAFETIES_ON=1 SOL_PRINT_ERRORS=0)
target_link_libraries(pn_sol2 INTERFACE pn_lua)

# Small single-header libraries
add_library(pn_miniaudio INTERFACE)
target_include_directories(pn_miniaudio SYSTEM INTERFACE "${miniaudio_SOURCE_DIR}")
add_library(pn_cgltf INTERFACE)
target_include_directories(pn_cgltf SYSTEM INTERFACE "${cgltf_SOURCE_DIR}")
add_library(pn_stb INTERFACE)
target_include_directories(pn_stb SYSTEM INTERFACE "${stb_SOURCE_DIR}")
add_library(pn_json INTERFACE)
target_include_directories(pn_json SYSTEM INTERFACE "${json_SOURCE_DIR}/single_include")
add_library(pn_doctest INTERFACE)
target_include_directories(pn_doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")

# Recast (navmesh generation) + Detour (navmesh queries), built as one static library.
file(GLOB PN_RECAST_SOURCES
    "${recastnavigation_SOURCE_DIR}/Recast/Source/*.cpp"
    "${recastnavigation_SOURCE_DIR}/Detour/Source/*.cpp")
add_library(pn_recast STATIC ${PN_RECAST_SOURCES})
target_include_directories(pn_recast SYSTEM PUBLIC
    "${recastnavigation_SOURCE_DIR}/Recast/Include"
    "${recastnavigation_SOURCE_DIR}/Detour/Include")
penumbra_third_party_target(pn_recast)

# Dear ImGui (docking branch): the engine's debug panels (runtime) and the editor
add_library(pn_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/imgui_demo.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp")
target_include_directories(pn_imgui SYSTEM PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends")
target_link_libraries(pn_imgui PUBLIC glfw)
penumbra_third_party_target(pn_imgui)

# The UI fonts come with the pinned ImGui and are compiled in (ImGui/ImGuiFonts.h): Roboto Medium
# (Apache License 2.0) and Cousine Regular (SIL Open Font License 1.1).
include("${CMAKE_CURRENT_LIST_DIR}/EmbedFile.cmake")
set(PN_EMBEDDED_FONTS "${CMAKE_BINARY_DIR}/generated/EmbeddedFonts.cpp")
penumbra_embed_files("${PN_EMBEDDED_FONTS}"
    g_PenumbraFontRoboto "${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf"
    g_PenumbraFontCousine "${imgui_SOURCE_DIR}/misc/fonts/Cousine-Regular.ttf")
add_library(pn_fonts STATIC "${PN_EMBEDDED_FONTS}")
penumbra_third_party_target(pn_fonts)

# ImGuizmo (transform gizmos), used by the editor only
if(PENUMBRA_BUILD_EDITOR)
add_library(pn_imguizmo STATIC "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
target_include_directories(pn_imguizmo SYSTEM PUBLIC "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(pn_imguizmo PUBLIC pn_imgui)
penumbra_third_party_target(pn_imguizmo)
endif()

# ---------------------------------------------------------------------------
# DirectX Shader Compiler - used at build time only. Windows: the pinned prebuilt release (DXIL and
# SPIR-V). Elsewhere Microsoft ships no binaries: the dxc of the LunarG Vulkan SDK (docs/macOS.md).
# ---------------------------------------------------------------------------
if(WIN32)
    FetchContent_Declare(dxc
        URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR _penumbra_no_cmake)
    FetchContent_MakeAvailable(dxc)
    set(PENUMBRA_DXC_EXECUTABLE "${dxc_SOURCE_DIR}/bin/x64/dxc.exe" CACHE FILEPATH "DXC executable" FORCE)
else()
    find_program(PENUMBRA_DXC_EXECUTABLE dxc
        HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{HOME}/VulkanSDK/macOS/bin" /usr/local/bin /opt/homebrew/bin
        DOC "DXC executable (from the LunarG Vulkan SDK)")
    if(NOT PENUMBRA_DXC_EXECUTABLE)
        message(FATAL_ERROR "dxc not found. Install the LunarG Vulkan SDK (see docs/macOS.md) and set VULKAN_SDK, "
            "or pass -DPENUMBRA_DXC_EXECUTABLE=/path/to/dxc.")
    endif()
endif()
