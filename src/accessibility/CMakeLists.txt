# M13：平台 provider 按平台条件编入（公共契约无 SDK 类型，plan §6）。
set(LUMEN_ACCESSIBILITY_SOURCES semantics.cpp bridge.cpp)
set(LUMEN_ACCESSIBILITY_PRIVATE_LIBS "")
if(LUMEN_ENABLE_ACCESSIBILITY_BRIDGE)
  if(WIN32)
    # Windows UIA：WM_GETOBJECT 子类化 + COM fragment 树（本地真验）。
    list(APPEND LUMEN_ACCESSIBILITY_SOURCES uia_provider.cpp)
    list(APPEND LUMEN_ACCESSIBILITY_PRIVATE_LIBS UIAutomationCore comctl32)
  elseif(APPLE)
    # AppKit's NSAccessibilityElement tree is implemented in ObjC++ so the
    # public bridge contract remains free of SDK types.
    list(APPEND LUMEN_ACCESSIBILITY_SOURCES nsaccessibility_provider.mm)
    list(APPEND LUMEN_ACCESSIBILITY_PRIVATE_LIBS
         "-framework AppKit" "-framework Foundation")
  elseif(UNIX)
    list(APPEND LUMEN_ACCESSIBILITY_SOURCES atspi_provider.cpp)
  endif()
endif()

add_library(lumen-accessibility STATIC ${LUMEN_ACCESSIBILITY_SOURCES})

target_include_directories(lumen-accessibility PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# 语义树消费 core（RenderNode/FocusManager/InteractionController）；平台
# 桥接是可选目标，不把 SDK 类型带进公共头（plan §6）。
target_link_libraries(lumen-accessibility PUBLIC lumen-core)
target_compile_features(lumen-accessibility PUBLIC cxx_std_20)

if(LUMEN_ENABLE_ACCESSIBILITY_BRIDGE AND WIN32)
  target_compile_definitions(lumen-accessibility PRIVATE
      LUMEN_ACCESSIBILITY_PROVIDER_UIA=1)
  target_link_libraries(lumen-accessibility PRIVATE
      ${LUMEN_ACCESSIBILITY_PRIVATE_LIBS})
endif()

if(LUMEN_ENABLE_ACCESSIBILITY_BRIDGE AND APPLE)
  target_compile_definitions(lumen-accessibility PRIVATE
      LUMEN_ACCESSIBILITY_PROVIDER_NSACCESSIBILITY=1)
  # Static archive does not link; frameworks must propagate to executables
  # (tests/examples) via PUBLIC, otherwise arm64 links miss NS* symbols.
  target_link_libraries(lumen-accessibility PUBLIC
      ${LUMEN_ACCESSIBILITY_PRIVATE_LIBS})
  # Manual retain/release in the provider requires non-ARC; pin it per-file
  # so a future global -fobjc-arc never breaks the build.
  set_source_files_properties(nsaccessibility_provider.mm PROPERTIES
      COMPILE_OPTIONS "-fno-objc-arc")
endif()

if(LUMEN_ENABLE_ACCESSIBILITY_BRIDGE AND UNIX AND NOT APPLE)
  target_compile_definitions(lumen-accessibility PRIVATE
      LUMEN_ACCESSIBILITY_PROVIDER_ATSPI=1)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(AT_SPI_DBUS QUIET dbus-1)
  endif()
  if(AT_SPI_DBUS_FOUND)
    target_compile_definitions(lumen-accessibility PRIVATE LUMEN_HAS_DBUS=1)
    target_include_directories(lumen-accessibility PRIVATE
        ${AT_SPI_DBUS_INCLUDE_DIRS})
    target_link_libraries(lumen-accessibility PRIVATE ${AT_SPI_DBUS_LIBRARIES})
  endif()
endif()
