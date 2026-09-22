add_executable(lumen-gallery main.cpp)

target_compile_features(lumen-gallery PRIVATE cxx_std_20)
target_link_libraries(lumen-gallery PRIVATE
    lumen-core
    lumen-app
    lumen-layout
    lumen-render
    lumen-text
    lumen-accessibility
    lumen-widgets
    lumen-platform
    lumen-dsl
    SDL3::SDL3
)

# Gallery 应用图标（design/gallery-icon.html 方向 01；docs/
# lumen-gallery-icon-design.md）：构建期由母版光栅化导出，产物仅落
# 构建/安装树（不提交生成二进制）。路径经 cache 变量暴露给根安装规则。
set(LUMEN_GALLERY_ICON_DIR ${CMAKE_CURRENT_BINARY_DIR}/icons CACHE INTERNAL
    "Generated gallery app icon assets")
add_executable(lumen-gallery-icons icon_tool.cpp gallery_icon.cpp)
target_compile_features(lumen-gallery-icons PRIVATE cxx_std_20)
target_include_directories(lumen-gallery-icons PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(lumen-gallery-icons PRIVATE lumen-stb-image-write)

set(LUMEN_GALLERY_ICON_OUTPUTS
    ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery-16.png
    ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery-20.png
    ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery-24.png
    ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery-32.png
    ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery-48.png
    ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery-64.png
    ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery-128.png
    ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery-256.png
    ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery.ico
    ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery.icns)
add_custom_command(
  OUTPUT ${LUMEN_GALLERY_ICON_OUTPUTS}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${LUMEN_GALLERY_ICON_DIR}
  COMMAND $<TARGET_FILE:lumen-gallery-icons> --out-dir ${LUMEN_GALLERY_ICON_DIR}
  DEPENDS lumen-gallery-icons
  COMMENT "Generating gallery app icons (Core Dark, 16-256 px)"
  VERBATIM)
add_custom_target(lumen-gallery-icon-assets
  DEPENDS ${LUMEN_GALLERY_ICON_OUTPUTS})
# 示例先于资源链接/安装依赖图标（rc 编译、hicolor 安装都消费产物）。
add_dependencies(lumen-gallery lumen-gallery-icon-assets)

if(WIN32)
  # exe 嵌入多尺寸图标（ICO 内 16-256：Explorer/任务栏按需选档，
  # 不放大位图）。rc 在构建树生成，以绝对路径引用同目录的 ico，
  # 规避生成顺序下的相对路径歧义。
  enable_language(RC)
  set(LUMEN_GALLERY_RC ${LUMEN_GALLERY_ICON_DIR}/lumen-gallery.rc)
  file(WRITE ${LUMEN_GALLERY_RC}
       "1 ICON \"${LUMEN_GALLERY_ICON_DIR}/lumen-gallery.ico\"\n")
  set_source_files_properties(${LUMEN_GALLERY_ICON_OUTPUTS} ${LUMEN_GALLERY_RC}
                              PROPERTIES GENERATED TRUE)
  target_sources(lumen-gallery PRIVATE ${LUMEN_GALLERY_RC})
  # SDL3 builds as a shared library; keep the DLL next to the sample so it
  # runs directly from the build tree.
  add_custom_command(
    TARGET lumen-gallery
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3>
            $<TARGET_FILE_DIR:lumen-gallery>
    COMMAND_EXPAND_LISTS
  )
endif()
