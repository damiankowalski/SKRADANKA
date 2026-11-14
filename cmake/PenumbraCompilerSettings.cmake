# Compiler settings applied to Penumbra's own targets (never to third-party code).
# Warnings are errors on every compiler: MSVC /W4 /WX, clang (macOS) -Wall -Wextra -Werror.

function(penumbra_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_definitions(${target} PUBLIC
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_RADIANS
        GLM_ENABLE_EXPERIMENTAL
        $<$<CONFIG:Debug>:PN_DEBUG>
        $<$<CONFIG:Release>:PN_RELEASE>
        $<$<CONFIG:Dist>:PN_DIST>)
    if(WIN32)
        target_compile_definitions(${target} PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /MP /bigobj
            /wd4100   # unreferenced formal parameter (interfaces)
            /wd4201)  # nameless struct/union (glm)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Werror
            -Wno-unused-parameter              # interfaces (as /wd4100)
            -Wno-missing-field-initializers)   # partial aggregate initialization is deliberate
    endif()
endfunction()
