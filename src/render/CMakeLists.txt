set(LUMEN_RENDER_SOURCES
    renderer.cpp
    cpu_renderer.cpp
    placeholder_font.cpp
    painter.cpp
)

if(LUMEN_ENABLE_SKIA)
    list(APPEND LUMEN_RENDER_SOURCES skia_renderer.cpp)
endif()

add_library(lumen-render STATIC ${LUMEN_RENDER_SOURCES})

target_include_directories(lumen-render PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-render PUBLIC lumen-core)
target_compile_features(lumen-render PUBLIC cxx_std_20)

if(LUMEN_ENABLE_SKIA)
    # Marks the adapter as genuinely compiled in; rendererStageName() and
    # the stage-name test key off this instead of a hard-coded claim.
    target_compile_definitions(lumen-render PRIVATE LUMEN_HAS_SKIA_BACKEND=1)
    # Skia stays private to the implementation (pimpl); public headers are
    # free of Skia types. Skia headers include each other by short paths, so
    # its own subdirectories must be on the search path as well.
    target_include_directories(lumen-render PRIVATE
        ${LUMEN_SKIA_ROOT}
        ${LUMEN_SKIA_ROOT}/include/core
        ${LUMEN_SKIA_ROOT}/include/config
    )
    find_library(LUMEN_SKIA_LIBRARY NAMES skia
        PATHS ${LUMEN_SKIA_ROOT}/out/Release-x64
        NO_DEFAULT_PATH
        REQUIRED
    )
    # skia.lib's bundled freetype references zlib symbols (both plain and
    # Chrome-prefixed); the package zlib.lib covers Cr_z_*, the in-tree
    # zlibstatic covers the plain ones.
    find_library(LUMEN_SKIA_ZLIB NAMES zlib
        PATHS ${LUMEN_SKIA_ROOT}/out/Release-x64
        NO_DEFAULT_PATH
        REQUIRED
    )
    target_link_libraries(lumen-render PRIVATE ${LUMEN_SKIA_LIBRARY}
                                              ${LUMEN_SKIA_ZLIB})
    if(WIN32)
        # Skia's GDI font manager uses Uniscribe for shaping.
        target_link_libraries(lumen-render PRIVATE usp10)
    endif()
    if(TARGET zlibstatic)
        target_link_libraries(lumen-render PRIVATE zlibstatic)
    endif()
endif()
