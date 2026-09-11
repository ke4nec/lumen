add_subdirectory(core)
# 视觉系统：layout 依赖 style（布局前解析样式），render/widgets 只读
# RenderNode 的 resolved style 或消费 Theme。
add_subdirectory(style)
add_subdirectory(layout)
add_subdirectory(render)
add_subdirectory(text)
add_subdirectory(accessibility)
add_subdirectory(widgets)
# lumen-platform 自行按 LUMEN_BUILD_MOBILE_CORE 排除 SDL 适配源（8E）。
add_subdirectory(platform)
add_subdirectory(platform/mobile)
add_subdirectory(dsl)
