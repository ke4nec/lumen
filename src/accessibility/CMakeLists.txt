# M13：平台 provider 按平台条件编入（公共契约无 SDK 类型，plan §6）。
set(LUMEN_ACCESSIBILITY_SOURCES semantics.cpp bridge.cpp)
set(LUMEN_ACCESSIBILITY_PRIVATE_LIBS "")
if(LUMEN_ENABLE_ACCESSIBILITY_BRIDGE)
  if(WIN32)
    # Windows UIA：WM_GETOBJECT 子类化 + COM fragment 树（本地真验）。
    list(APPEND LUMEN_ACCESSIBILITY_SOURCES uia_provider.cpp)
    list(APPEND LUMEN_ACCESSIBILITY_PRIVATE_LIBS UIAutomationCore comctl32)
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
