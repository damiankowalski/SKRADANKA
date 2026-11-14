file(GLOB_RECURSE PENUMBRA_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
file(GLOB PENUMBRA_SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsl"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.hlsli")

add_library(Penumbra STATIC ${PENUMBRA_SOURCES} ${PENUMBRA_SHADER_SOURCES})
set_source_files_properties(${PENUMBRA_SHADER_SOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)
penumbra_configure_target(Penumbra)

target_include_directories(Penumbra PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(Penumbra PRIVATE PN_VERSION_STRING="${PROJECT_VERSION}")
target_precompile_headers(Penumbra PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/pnpch.h")

target_link_libraries(Penumbra PUBLIC
    glfw
    glm::glm
    EnTT::EnTT
    nvrhi
    nvrhi_vk
    Jolt
    pn_sol2
    pn_json
    pn_miniaudio
    pn_cgltf
    pn_stb
    pn_recast)
# The Vulkan device is created by RenderDevice (vulkan.hpp, loader opened at run time).
target_link_libraries(Penumbra PRIVATE Vulkan::Headers ${CMAKE_DL_LIBS})
target_compile_definitions(Penumbra PRIVATE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
if(WIN32)
    target_link_libraries(Penumbra PUBLIC nvrhi_d3d12 d3d12 dxgi dxguid dbghelp ws2_32)
elseif(APPLE)
    # miniaudio's Core Audio backend.
    target_link_libraries(Penumbra PUBLIC "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
endif()

# Shader permutations: "<Name>|<file>|<entry>|<profile>[|DEFINE=1,DEFINE2=1]"
include(${CMAKE_CURRENT_SOURCE_DIR}/shaders/ShaderList.cmake)
penumbra_compile_shaders(
    TARGET Penumbra
    SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    SHADERS ${PENUMBRA_SHADERS})
