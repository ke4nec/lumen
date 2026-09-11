add_library(lumen-layout STATIC
    layout.cpp
)

target_include_directories(lumen-layout PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

# 文本测量/换行共用 lumen-text（plan §3.2：布局与绘制必须共用同一份
# TextLayout）；布局前经 lumen-style 解析样式（visual-system §4.1）。
target_link_libraries(lumen-layout PUBLIC lumen-core lumen-text lumen-style)
target_compile_features(lumen-layout PUBLIC cxx_std_20)
