set(LUMEN_RENDER_SOURCES
    renderer.cpp
    render_commands.cpp
    frame_scheduler.cpp
    resource_manager.cpp
    image_decode.cpp
    stb_image_impl.cpp
    cpu_renderer.cpp
    placeholder_font.cpp
    painter.cpp
)

if(LUMEN_ENABLE_SKIA)
    list(APPEND LUMEN_RENDER_SOURCES skia_renderer.cpp skia_text.cpp)
endif()

# v0.2 阶段7C: Skia Ganesh GPU 后端（需要 SDL3 + Skia）。
if(LUMEN_ENABLE_GPU)
    list(APPEND LUMEN_RENDER_SOURCES skia_gpu_renderer.cpp)
endif()

add_library(lumen-render STATIC ${LUMEN_RENDER_SOURCES})

target_include_directories(lumen-render PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-render PUBLIC lumen-core lumen-text)
target_compile_features(lumen-render PUBLIC cxx_std_20)

# stb_image 头文件（ResourceManager 解码用，仅实现侧可见）。
target_include_directories(lumen-render PRIVATE ${stb_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(lumen-render PRIVATE Threads::Threads)

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
        PATHS
            ${LUMEN_SKIA_ROOT}/out/Release-x64
            ${LUMEN_SKIA_ROOT}/out/Release-arm64
            ${LUMEN_SKIA_ROOT}
        NO_DEFAULT_PATH
        REQUIRED
    )
    # 预编译包的 lib 目录（Linux x64 / macOS arm64/x64 按包不同）。
    get_filename_component(LUMEN_SKIA_LIB_DIR "${LUMEN_SKIA_LIBRARY}" DIRECTORY)
    if(WIN32)
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
        # Skia's GDI font manager uses Uniscribe for shaping.
        target_link_libraries(lumen-render PRIVATE usp10)
        if(TARGET zlibstatic)
            target_link_libraries(lumen-render PRIVATE zlibstatic)
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # Linux prebuilt (Skia-Linux-Release-x64.zip) ships libskia.a plus
        # its bundled third-party archives in the same directory. Link the
        # full set so raster/text/shaping symbols resolve without requiring
        # a system Skia install. System libs cover FontConfig (Linux font
        # discovery) and threading.
        file(GLOB LUMEN_SKIA_BUNDLED_LIBS
            "${LUMEN_SKIA_LIB_DIR}/lib*.a")
        # The glob above already contains libskia.a; drop the duplicate so
        # it is listed once inside the group below.
        list(REMOVE_ITEM LUMEN_SKIA_BUNDLED_LIBS "${LUMEN_SKIA_LIBRARY}")
        # --start-group/--end-group makes the one-pass static link order
        # insensitive to inter-archive dependencies (skia/skunicode/
        # skshaper/harfbuzz/icu reference each other).
        target_link_libraries(lumen-render PRIVATE
            "-Wl,--start-group"
            ${LUMEN_SKIA_LIBRARY}
            ${LUMEN_SKIA_BUNDLED_LIBS}
            "-Wl,--end-group")
        find_package(Threads REQUIRED)
        target_link_libraries(lumen-render PRIVATE
            Threads::Threads ${CMAKE_DL_LIBS} m)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
            target_link_libraries(lumen-render PRIVATE
                PkgConfig::FONTCONFIG)
        else()
            find_library(FONTCONFIG_LIBRARY NAMES fontconfig REQUIRED)
            target_link_libraries(lumen-render PRIVATE ${FONTCONFIG_LIBRARY})
        endif()
        # Bundled freetype + system freetype/fontconfig both need plain zlib
        # symbols (inflateEnd); the bundled libzlib.a precedes the shared
        # system libs on the link line, so also link the system zlib to
        # satisfy the trailing shared objects.
        find_package(ZLIB REQUIRED)
        target_link_libraries(lumen-render PRIVATE ZLIB::ZLIB)
    elseif(APPLE)
        # macOS 预编译同目录附带 skunicode/skshaper/harfbuzz/icu 等归档；
        # ld64 无 --start-group，全量列出即可（libskia 置首）。
        file(GLOB LUMEN_SKIA_BUNDLED_LIBS
            "${LUMEN_SKIA_LIB_DIR}/lib*.a")
        list(REMOVE_ITEM LUMEN_SKIA_BUNDLED_LIBS "${LUMEN_SKIA_LIBRARY}")
        target_link_libraries(lumen-render PRIVATE
            ${LUMEN_SKIA_LIBRARY}
            ${LUMEN_SKIA_BUNDLED_LIBS})
        # skia_text CoreText 端口（SkFontMgr_New_CoreText）：CoreText 的
        # CTFont/CFString 符号落在 CoreFoundation，字形光栅经 CoreGraphics，
        # 缺一即 ld 报 kCF* / ___CFConstantStringClassReference 未定义
        #（macos skia-gpu Build 失败实测）。
        target_link_libraries(lumen-render PRIVATE
            "-framework CoreText"
            "-framework CoreFoundation"
            "-framework CoreGraphics")
        find_package(Threads REQUIRED)
        target_link_libraries(lumen-render PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
        find_package(ZLIB REQUIRED)
        target_link_libraries(lumen-render PRIVATE ZLIB::ZLIB)
    else()
        target_link_libraries(lumen-render PRIVATE ${LUMEN_SKIA_LIBRARY})
    endif()
endif()

# Static Skia references WGL/GLX. On Linux keep OpenGL inside the archive
# group so the GL libraries always accompany Skia's objects regardless of
# how CMake orders the (repeated) lumen-render entries on a consumer's link
# line. Other platforms lack --start-group (MSVC/macOS linkers) and only
# need OpenGL appended (Skia archives are linked in the SKIA block above).
if(LUMEN_ENABLE_GPU)
    find_package(OpenGL REQUIRED)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(lumen-render PRIVATE
            "-Wl,--start-group"
            ${LUMEN_SKIA_LIBRARY}
            ${LUMEN_SKIA_BUNDLED_LIBS}
            OpenGL::GL
            "-Wl,--end-group")
    else()
        target_link_libraries(lumen-render PRIVATE OpenGL::GL)
    endif()
    target_link_libraries(lumen-render PRIVATE SDL3::SDL3)
endif()
