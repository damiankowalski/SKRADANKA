add_executable(PenumbraRuntime
    src/RuntimeMain.cpp
    src/RuntimeLayer.cpp
    src/RuntimeLayer.h)
penumbra_configure_target(PenumbraRuntime)
target_link_libraries(PenumbraRuntime PRIVATE Penumbra)

# Shipping builds have no console window; development builds keep one for logs.
set_target_properties(PenumbraRuntime PROPERTIES WIN32_EXECUTABLE $<CONFIG:Dist>)
