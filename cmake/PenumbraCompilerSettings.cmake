# Compiler settings applied to Penumbra's own targets (never to third-party code).

function(penumbra_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_definitions(${target} PUBLIC
        NOMINMAX
        WIN32_LEAN_AND_MEAN
        _CRT_SECURE_NO_WARNINGS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_ENABLE_EXPERIMENTAL
        $<$<CONFIG:Debug>:PN_DEBUG>
        $<$<CONFIG:Release>:PN_RELEASE>
        $<$<CONFIG:Dist>:PN_DIST>)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /MP /bigobj
            /wd4100   # unreferenced formal parameter (interfaces)
            /wd4201)  # nameless struct/union (glm)
    endif()
endfunction()
