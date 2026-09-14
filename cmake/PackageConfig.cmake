# M8（自用路线图）：便携包配置。
#
# 三桌面统一 CPack 入口：Windows zip / Linux tar.gz / macOS .app 结构 +
# zip。包内容 = install 规则产物（静态库 + 头 + 示例 + 文档）；包内自述
# （启动说明/诊断开关/已知限制）由 CMAKE_INSTALL_DOCDIR 承载。
# AppImage 属 Linux 增强通道（后续按需追加），不阻塞出口条件。

set(CPACK_PACKAGE_NAME "lumen")
set(CPACK_PACKAGE_VENDOR "lumen self-use tools")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Lumen self-use GUI toolkit + counter/settings examples")
# 版本可追溯：优先 Git 描述，回退固定值（干净检出保证可追溯性）。
find_package(Git QUIET)
if(Git_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE LUMEN_GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()
if(NOT LUMEN_GIT_VERSION)
  set(LUMEN_GIT_VERSION "0.0.0")
endif()
set(CPACK_PACKAGE_VERSION ${LUMEN_GIT_VERSION})
set(CPACK_PACKAGE_INSTALL_DIRECTORY "lumen-${CPACK_PACKAGE_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/README.md)
set(CPACK_STRIP_FILES FALSE)  # 自用包保留符号便于诊断（§8 调试符号归档）

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_SYSTEM_NAME "windows-x64")
elseif(APPLE)
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_SYSTEM_NAME "macos-universal")
  # .app 结构：示例按 macOS 惯例落位（运行由解包方经 bin/ 启动；
  # 完整 .app bundle 打包属后续增强，zip 内保留 bin 布局即可启动）。
else()
  set(CPACK_GENERATOR "TGZ")
  set(CPACK_SYSTEM_NAME "linux-x64")
endif()

set(CPACK_PACKAGE_FILE_NAME
    "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-${CPACK_SYSTEM_NAME}")

include(CPack)
