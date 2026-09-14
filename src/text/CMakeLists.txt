add_library(lumen-text STATIC
    grapheme.cpp
    font_manager.cpp
    bidi.cpp
    text_layout.cpp
    editing_value.cpp
    editing_history.cpp
    skia_font_manager.cpp
    mobile_font_manager.cpp
)

target_include_directories(lumen-text PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# lumen-text 只依赖 lumen-core（plan §6：Skia 实现是可选目标，契约不含
# 平台/Skia 类型）。
target_link_libraries(lumen-text PUBLIC lumen-core)
# M9：移动字体后端（stb_truetype；实现侧私有依赖）。
if(TARGET stb)
    target_compile_definitions(lumen-text PRIVATE LUMEN_HAS_STB_TRUETYPE=1)
    target_include_directories(lumen-text PRIVATE ${stb_SOURCE_DIR})
endif()
target_compile_features(lumen-text PUBLIC cxx_std_20)

# M1：Skia-backed FontManager（桌面正式字体/回退/shaping）。
# 公共头文件无 Skia 类型；实现侧私有链接 Skia + FontConfig。
if(LUMEN_ENABLE_SKIA)
    target_compile_definitions(lumen-text PRIVATE LUMEN_HAS_SKIA_TEXT=1)
    target_include_directories(lumen-text PRIVATE
        ${LUMEN_SKIA_ROOT}
        ${LUMEN_SKIA_ROOT}/include/core
        ${LUMEN_SKIA_ROOT}/include/config
    )
    find_library(LUMEN_TEXT_SKIA_LIBRARY NAMES skia
        PATHS
            ${LUMEN_SKIA_ROOT}/out/Release-x64
            ${LUMEN_SKIA_ROOT}/out/Release-arm64
            ${LUMEN_SKIA_ROOT}
        NO_DEFAULT_PATH
        REQUIRED
    )
    get_filename_component(LUMEN_TEXT_SKIA_LIB_DIR "${LUMEN_TEXT_SKIA_LIBRARY}" DIRECTORY)
    if(WIN32)
        find_library(LUMEN_TEXT_SKIA_ZLIB NAMES zlib
            PATHS ${LUMEN_SKIA_ROOT}/out/Release-x64
            NO_DEFAULT_PATH
            REQUIRED
        )
        target_link_libraries(lumen-text PRIVATE ${LUMEN_TEXT_SKIA_LIBRARY}
                                                 ${LUMEN_TEXT_SKIA_ZLIB})
        target_link_libraries(lumen-text PRIVATE usp10)
        if(TARGET zlibstatic)
            target_link_libraries(lumen-text PRIVATE zlibstatic)
        endif()
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        file(GLOB LUMEN_TEXT_SKIA_BUNDLED_LIBS
            "${LUMEN_TEXT_SKIA_LIB_DIR}/lib*.a")
        list(REMOVE_ITEM LUMEN_TEXT_SKIA_BUNDLED_LIBS
            "${LUMEN_TEXT_SKIA_LIBRARY}")
        target_link_libraries(lumen-text PRIVATE
            "-Wl,--start-group"
            ${LUMEN_TEXT_SKIA_LIBRARY}
            ${LUMEN_TEXT_SKIA_BUNDLED_LIBS}
            "-Wl,--end-group")
        find_package(Threads REQUIRED)
        target_link_libraries(lumen-text PRIVATE
            Threads::Threads ${CMAKE_DL_LIBS} m)
        find_package(PkgConfig QUIET)
        if(PKG_CONFIG_FOUND)
            pkg_check_modules(FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
            target_link_libraries(lumen-text PRIVATE
                PkgConfig::FONTCONFIG)
        else()
            find_library(FONTCONFIG_LIBRARY NAMES fontconfig REQUIRED)
            target_link_libraries(lumen-text PRIVATE ${FONTCONFIG_LIBRARY})
        endif()
        find_package(ZLIB REQUIRED)
        target_link_libraries(lumen-text PRIVATE ZLIB::ZLIB)
    elseif(APPLE)
        # macOS 同目录全量归档 + CoreText/CoreFoundation/CoreGraphics +
        # 系统库（见 lumen-render；缺 CoreFoundation 即 kCF* 未定义）。
        file(GLOB LUMEN_TEXT_SKIA_BUNDLED_LIBS
            "${LUMEN_TEXT_SKIA_LIB_DIR}/lib*.a")
        list(REMOVE_ITEM LUMEN_TEXT_SKIA_BUNDLED_LIBS
            "${LUMEN_TEXT_SKIA_LIBRARY}")
        target_link_libraries(lumen-text PRIVATE
            ${LUMEN_TEXT_SKIA_LIBRARY}
            ${LUMEN_TEXT_SKIA_BUNDLED_LIBS})
        target_link_libraries(lumen-text PRIVATE
            "-framework CoreText"
            "-framework CoreFoundation"
            "-framework CoreGraphics"
            "-framework CoreServices")
        find_package(Threads REQUIRED)
        target_link_libraries(lumen-text PRIVATE
            Threads::Threads ${CMAKE_DL_LIBS} m)
        find_package(ZLIB REQUIRED)
        target_link_libraries(lumen-text PRIVATE ZLIB::ZLIB)
    else()
        target_link_libraries(lumen-text PRIVATE ${LUMEN_TEXT_SKIA_LIBRARY})
    endif()
endif()
